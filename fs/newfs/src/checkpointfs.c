#include "checkpointfs.h"
#include "bonus.h"
#include "newfs.h"
#include <errno.h>
#include <string.h>

/******************************************************************************
* SECTION: 路径辅助函数
*******************************************************************************/
/**
 * @brief 取得已规范化绝对路径的父目录
 *
 * @param path 已校验的绝对路径
 * @param parent 输出缓冲区，容量须不小于 path 字符串长度加 1
 * @return void
 */
static void parent_path(const char *path, char *parent) {
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (slash == parent) slash[1] = '\0';
    else *slash = '\0';
}

/******************************************************************************
* SECTION: 文件复制函数
*******************************************************************************/

/**
 * @brief 将源文件完整复制为一个拥有独立 inode 和独立数据块的新文件
 *
 * @param source 源文件的挂载内绝对路径（如 "/models/model.bin" 或内部检查点路径）
 * @param target 目标文件的挂载内绝对路径（调用前须确保其父目录已存在）
 * @return int 成功返回 0，失败返回负错误码（如 -ENOENT、-EISDIR、-EEXIST 等）
 * @note 1. 不覆盖已有目标文件；2. 若创建目标文件后复制中途失败，只清理本次新建的目标文件。
 */
static int copy_file(const char *source, const char *target) {
    /* TODO: 按以下步骤实现通用文件独立复制（不在此函数内编码检查点路径或创建父目录）：
     * Step 1: 调用自己的 newfs_getattr(source, &st) 检查源文件：
     *         若不存在直接返回对应负错误码；若不是普通文件（!S_ISREG(st.st_mode)）返回 -EISDIR。
     * Step 2: 确认 target 尚不存在（避免覆盖已有文件），再调用 newfs_mknod(target, S_IFREG | 0644, 0)
     *         创建独立的目标文件 inode。
     * Step 3: 将 source 的全部字节（共 st.st_size 字节，含空文件和不足一块的尾部）复制到 target：
     *         - 方法 A：用固定大小缓冲区（如 4096 字节）循环调用自己的 newfs_read 和
     *           newfs_write（传入零初始化的 struct fuse_file_info fi = {0};），维护 offset；
     *         - 方法 B：直接在你自己的文件系统内部按数据块复制。
     *         注意：绝不能用硬链接代替复制！若步骤 3 中途出错，须先调用 newfs_unlink(target)
     *         清理本次新建的半成品文件，再返回负错误码。 */
    return -ENOSYS;
}

/******************************************************************************
* SECTION: CheckpointFS 函数实现
*******************************************************************************/

/**
 * @brief 将当前工作文件保存为一个独立的历史检查点（对应命令 create FILE ID）
 *
 * @param source 工作文件的挂载内绝对路径（如 "/models/model.bin"）
 * @param id 新检查点的名称（如 "v0"）
 * @return int 成功返回 0，失败返回负错误码
 */
int checkpointfs_create(const char *source, const char *id) {
    /* TODO: 按以下步骤保存检查点：
     * Step 1: 声明局部路径缓冲区（长度 CHECKPOINTFS_PATH_MAX），调用
     *         CHECK(checkpointfs_path(source, id, ckpt_path, sizeof(ckpt_path))) 生成内部存储路径，
     *         再调用上方的 parent_path(ckpt_path, dir_path) 取得该文件专属的检查点集合目录。
     * Step 2: 固定的内部根目录 CHECKPOINTFS_STORAGE_DIR 已由教师控制层提前建好；
     *         你只需用 newfs_getattr 检查 dir_path 是否存在：
     *         - 若返回 -ENOENT，调用 newfs_mkdir(dir_path, 0755) 创建集合目录；
     *         - 若已存在但不是目录（!S_ISDIR），返回 -ENOTDIR。
     * Step 3: 调用 copy_file(source, ckpt_path) 将工作文件独立复制到检查点路径并返回结果。 */
    return -ENOSYS;
}

/**
 * @brief 在同一工作文件的检查点集合内，将已有检查点 source_id 独立复制为新检查点 target_id
 *
 * @param source 工作文件路径（用于确定检查点所属集合，切勿传入隐藏的内部路径）
 * @param source_id 已有的源检查点名称
 * @param target_id 新的目标检查点名称（不得覆盖已有检查点）
 * @return int 成功返回 0，失败返回负错误码
 */
int checkpointfs_copy(const char *source, const char *source_id,
                      const char *target_id) {
    /* TODO: 按以下步骤复制同一集合内的检查点（不改动工作文件 source）：
     * Step 1: 声明两个长度为 CHECKPOINTFS_PATH_MAX 的局部路径缓冲区，以同一个 source 为第一个参数，
     *         分别调用 checkpointfs_path(source, source_id, ...) 和 checkpointfs_path(source, target_id, ...)
     *         得到源检查点和目标检查点的内部真实路径。
     * Step 2: 直接调用 copy_file(src_ckpt_path, dst_ckpt_path) 完成独立复制并返回结果。 */
    return -ENOSYS;
}

/**
 * @brief 将历史检查点独立复制为新检查点，再让工作路径通过硬链接指向新检查点
 *        （对应命令 restore FILE HIST_CHECKPOINT NEW_CHECKPOINT）
 *
 * @param source 工作文件路径（如 "/models/model.bin"）
 * @param hist_id 已有的历史检查点名称（如 "v0"，恢复后其历史内容保持不变）
 * @param new_id 新生成的检查点名称（如 "resumed"，恢复后与工作文件共享同一个 inode）
 * @return int 成功返回 0，失败返回负错误码
 * @note 恢复前未显式 create 保存的旧工作内容会被丢弃；即使工作文件当前已与 hist_id 同 inode，也必须复制出独立的新检查点。
 */
int checkpointfs_restore(const char *source, const char *hist_id,
                         const char *new_id) {
    /* TODO: 按以下步骤安全恢复工作文件：
     * Step 1: 调用 newfs_getattr(source, &st) 检查工作文件 source 是否存在且为普通文件
     *         （若为目录返回 -EISDIR，不存在则返回对应错误码，不要在 source 非法时创建新检查点）。
     * Step 2: 调用 CHECK(checkpointfs_copy(source, hist_id, new_id)) 将历史检查点独立复制为新检查点；
     *         若 hist_id 不存在或 new_id 已存在，此处会直接返回错误，工作文件 source 保持不变。
     * Step 3: 用 checkpointfs_path(source, new_id, new_ckpt_path, sizeof(new_ckpt_path)) 取得新检查点路径，
     *         先调用 CHECK(newfs_unlink(source)) 移除旧工作路径目录项，
     *         再调用 newfs_link(new_ckpt_path, source) 让工作路径硬链接到新检查点。 */
    return -ENOSYS;
}
