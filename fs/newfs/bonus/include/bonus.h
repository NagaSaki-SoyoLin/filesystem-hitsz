#ifndef LAB5_BONUS_H
#define LAB5_BONUS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>
#include <stddef.h>

/******************************************************************************
* SECTION: 公共错误检查宏
*******************************************************************************/
/**
 * @brief 对表达式 expr 求值一次；若返回值小于 0（负 errno），立即从当前函数返回该错误码
 *
 * @param expr 返回 int 状态码的表达式（如 CHECK(checkpointfs_path(...))）
 * @note 若当前函数已动态分配了局部资源（如 query 字符串、documents 数组）或创建了半成品文件，
 *       请用显式 if (ret < 0) 先完成释放/清理再返回，不要用此宏直接跳过清理逻辑。
 */
#define CHECK(expr) do { \
    int check_status = (expr); \
    if (check_status < 0) return check_status; \
} while (0)

/******************************************************************************
* SECTION: FUSE 路由接入
*******************************************************************************/
/**
 * @brief 为已有 FUSE 操作表接入附加路径分发
 *
 * @param operations 已有操作表，原地安装包装回调；必须包含 getattr/readdir/read
 * @return int 成功返回 0，参数不完整或重复注册返回 -EINVAL
 * @note 由主程序在 fuse_main 前调用；原普通文件函数保持不变，供学生实现直接调用。
 */
int bonus_register(struct fuse_operations *operations);

/******************************************************************************
* SECTION: 通用路径工具
*******************************************************************************/
/**
 * @brief 规范化绝对路径，合并多余斜杠并处理 . 和 ..
 *
 * @param path 输入绝对路径
 * @param out 接收规范化路径的缓冲区
 * @param capacity 缓冲区容量，包含字符串结束符
 * @return int 成功返回 0；非法路径或越过根目录返回 -EINVAL，容量不足返回 -ENAMETOOLONG
 * @note 只处理字符串，不访问文件系统。
 */
int normalize_path(const char *path, char *out, size_t capacity);
/**
 * @brief 解码字符串中的百分号编码
 *
 * @param source 输入编码字符串
 * @param out 接收解码结果的缓冲区
 * @param capacity 缓冲区容量，包含字符串结束符
 * @return int 成功返回 0；非法编码（包括 %00）返回 -EINVAL，容量不足返回 -ENAMETOOLONG
 */
int bonus_percent_decode(const char *source, char *out, size_t capacity);

#endif
