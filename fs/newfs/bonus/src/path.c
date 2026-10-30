#include "bonus.h"
#include <errno.h>
#include <string.h>


/******************************************************************************
* SECTION: 通用路径工具实现
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
int normalize_path(const char *path, char *out, size_t capacity) {
    size_t used = 1;
    if (!path || !out || capacity < 2 || path[0] != '/') return -EINVAL;
    out[0] = '/';
    while (*path) {
        while (*path == '/') ++path;
        const char *start = path;
        while (*path && *path != '/') ++path;
        size_t length = (size_t)(path - start);
        if (!length || (length == 1 && start[0] == '.')) continue;
        if (length == 2 && start[0] == '.' && start[1] == '.') {
            if (used == 1) { out[0] = '\0'; return -EINVAL; }
            while (used > 1 && out[used - 1] != '/') --used;
            if (used > 1) --used;
            continue;
        }
        size_t separator = used > 1;
        if (length + separator >= capacity - used) {
            out[0] = '\0';
            return -ENAMETOOLONG;
        }
        if (separator) out[used++] = '/';
        memcpy(out + used, start, length);
        used += length;
    }
    out[used] = '\0';
    return 0;
}

/**
 * @brief 将单个十六进制字符转换为数值
 *
 * @param c 待转换的字符
 * @return int 合法字符返回 0 到 15，其他字符返回 -1
 */
static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief 解码字符串中的百分号编码
 *
 * @param source 输入编码字符串
 * @param out 接收解码结果的缓冲区
 * @param capacity 缓冲区容量，包含字符串结束符
 * @return int 成功返回 0；非法编码（包括 %00）返回 -EINVAL，容量不足返回 -ENAMETOOLONG
 */
int bonus_percent_decode(const char *source, char *out, size_t capacity) {
    size_t used = 0;
    if (!source || !out || !capacity) return -EINVAL;
    out[0] = '\0';
    while (*source) {
        unsigned char c = (unsigned char)*source++;
        if (c == '%') {
            if (!source[0] || !source[1]) return -EINVAL;
            int high = hex_value((unsigned char)source[0]);
            int low = hex_value((unsigned char)source[1]);
            if (high < 0 || low < 0 || !(high || low)) return -EINVAL;
            c = (unsigned char)((high << 4) | low);
            source += 2;
        }
        if (used + 1 >= capacity) { out[0] = '\0'; return -ENAMETOOLONG; }
        out[used++] = (char)c;
        out[used] = '\0';
    }
    return 0;
}
