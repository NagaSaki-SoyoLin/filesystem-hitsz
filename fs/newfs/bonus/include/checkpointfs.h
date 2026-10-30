#ifndef CHECKPOINTFS_H
#define CHECKPOINTFS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

#define CHECKPOINTFS_CONTROL_DIR "/.checkpoint"
#define CHECKPOINTFS_VIEW_DIR "/.checkpoints"
#define CHECKPOINTFS_STORAGE_DIR "/.checkpointfs-data"
#define CHECKPOINTFS_PATH_MAX 512
#define CHECKPOINTFS_NAME_MAX 127


/******************************************************************************
* SECTION: 学生实现入口（在 src/checkpointfs.c 中实现）
*******************************************************************************/
/* 参数字符串仅在本次函数调用期间借用；详细分步说明见 src/checkpointfs.c。 */
int checkpointfs_create(const char *source, const char *id);
int checkpointfs_copy(const char *source, const char *source_id,
                      const char *target_id);
int checkpointfs_restore(const char *source, const char *hist_id,
                         const char *new_id);

/******************************************************************************
* SECTION: 检查点路径工具（教师已实现，学生直接调用）
*******************************************************************************/
/**
 * @brief 生成指定工作文件和检查点名称对应的内部真实存储路径
 *
 * @param file 工作文件的挂载内绝对路径（如 "/models/model.bin"，不能是根目录 "/"）
 * @param id 检查点名称（以 ASCII 字母或数字开头，后续可含字母、数字、'-'、'_'、'.'）
 * @param out 接收内部存储路径的缓冲区（建议大小 CHECKPOINTFS_PATH_MAX）
 * @param capacity 缓冲区容量（包含末尾 '\0'）
 * @return int 成功返回 0；路径/名称非法返回 -EINVAL（根目录返回 -EISDIR），容量不足返回 -ENAMETOOLONG
 * @note 1. 仅做字符串规范化、校验和编码，不查磁盘、不创建目录、不复制文件内容。
 *       2. 调用示例：
 *          char ckpt_path[CHECKPOINTFS_PATH_MAX];
 *          CHECK(checkpointfs_path(source, id, ckpt_path, sizeof(ckpt_path)));
 *          // 例如 source="/models/model.bin", id="v0" -> ckpt_path="/.checkpointfs-data/models__model.bin/v0"
 *       3. 定位同一工作文件的历史检查点与新检查点时，第一个参数 file 始终传工作文件路径 source。
 */
int checkpointfs_path(const char *file, const char *id, char *out,
                      size_t capacity);

/******************************************************************************
* SECTION: 控制文件与只读视图适配
*******************************************************************************/
/* 以下由教师提供，外部保留路径的写保护由路由层负责。 */
/**
 * @brief 打开只写控制文件并分配请求会话
 *
 * @param fi 文件信息；flags 须为 O_WRONLY，会话保存到 fh
 * @return int 成功返回 0，失败返回负错误码
 * @note 成功后须由 checkpointfs_control_release 释放会话。
 */
int checkpointfs_control_open(struct fuse_file_info *fi);
/**
 * @brief 连续接收控制请求，遇到末尾换行后执行一次
 *
 * @param buf 本次写入的请求片段
 * @param size 本次片段的字节数
 * @param offset 须等于会话此前累计接收的字节数
 * @param fi 保存已打开请求会话的文件信息
 * @return int 成功返回本次接收字节数，失败返回负错误码
 * @note 尚未收到换行时只缓存；请求完成后不能再次提交。
 */
int checkpointfs_control_write(const char *buf, size_t size, off_t offset,
                               struct fuse_file_info *fi);
/**
 * @brief 释放控制请求会话并清空文件句柄
 *
 * @param fi 保存请求会话的文件信息
 * @return int 成功返回 0，fi 为 NULL 时返回 -EINVAL
 * @note 未完成的请求直接丢弃，不在关闭时执行。
 */
int checkpointfs_control_release(struct fuse_file_info *fi);
/**
 * @brief 通过普通文件属性操作读取检查点只读视图的属性
 *
 * @param path /.checkpoints 下的完整路径
 * @param st 接收文件属性的结构体
 * @return int 成功返回 0，失败返回负错误码
 * @note 目录权限显示为 0555，普通文件显示为 0444，不改变底层 inode。
 */
int checkpointfs_view_getattr(const char *path, struct stat *st);
/**
 * @brief 通过普通目录遍历操作列出检查点只读视图
 *
 * @param path 只读视图中目录的完整路径
 * @param buf 传给 filler 的目录项缓冲区
 * @param filler 填充目录项的 FUSE 回调
 * @param offset 非负目录项偏移
 * @return int 成功返回 0，失败返回负错误码
 */
int checkpointfs_view_readdir(const char *path, void *buf,
                              fuse_fill_dir_t filler, off_t offset);
/**
 * @brief 通过普通文件读取操作读取检查点内容
 *
 * @param path 只读视图中文件的完整路径
 * @param buf 接收文件内容的缓冲区
 * @param size 最多读取的字节数
 * @param offset 非负文件偏移
 * @return int 成功返回读取字节数，失败返回负错误码
 */
int checkpointfs_view_read(const char *path, char *buf, size_t size,
                           off_t offset);

#endif
