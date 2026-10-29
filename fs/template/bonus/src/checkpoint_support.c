#include "checkpointfs.h"
#include "bonus.h"
#include "PROJECT_NAME.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECKPOINTFS_REQUEST_MAX \
    (3 * (CHECKPOINTFS_PATH_MAX - 1) + 2 * CHECKPOINTFS_NAME_MAX + 32)

struct checkpointfs_session {
    char request[CHECKPOINTFS_REQUEST_MAX + 1];
    size_t used;
    int finished;
};

static int has_prefix(const char *path, const char *prefix) {
    size_t length = strlen(prefix);
    return !strncmp(path, prefix, length) &&
           (path[length] == '\0' || path[length] == '/');
}

static int ascii_alnum(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9');
}

static int key_plain(unsigned char ch) {
    return ascii_alnum(ch) || ch == '-' || ch == '.';
}

/* 检查点名称以 ASCII 字母或数字开头，后续可包含点号。 */
static int valid_id(const char *id) {
    size_t i, length;
    if (!id || !ascii_alnum((unsigned char)id[0])) return 0;
    length = strlen(id);
    if (length > CHECKPOINTFS_NAME_MAX) return 0;
    for (i = 1; i < length; ++i) {
        unsigned char ch = (unsigned char)id[i];
        if (!ascii_alnum(ch) && ch != '-' && ch != '_' && ch != '.') return 0;
    }
    return 1;
}

/* 只有路径分隔符编码为下划线，输入中的下划线一律编码为 %5F。 */
static int make_key(const char *path, char *key, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    const char *last = strrchr(path, '/');
    const unsigned char *p = (const unsigned char *)path + 1;
    size_t used = 0;
    if (last == path) {
        if (capacity < 3) return -ENAMETOOLONG;
        key[used++] = '_';
        key[used++] = '_';
    }
    for (; *p; ++p) {
        size_t count = *p == '/' ? ((const char *)p == last ? 2 : 1) :
                       (key_plain(*p) ? 1 : 3);
        if (count >= capacity - used) return -ENAMETOOLONG;
        if (*p == '/') {
            key[used++] = '_';
            if ((const char *)p == last) key[used++] = '_';
        } else if (key_plain(*p)) {
            key[used++] = (char)*p;
        } else {
            key[used++] = '%';
            key[used++] = hex[*p >> 4];
            key[used++] = hex[*p & 15];
        }
    }
    key[used] = '\0';
    return 0;
}

static int join_path(const char *parent, const char *name,
                     char *out, size_t capacity) {
    int length = snprintf(out, capacity, "%s/%s", parent, name);
    return length < 0 || (size_t)length >= capacity ? -ENAMETOOLONG : 0;
}

/******************************************************************************
* SECTION: 检查点路径工具实现
*******************************************************************************/
/**
 * @brief 生成指定工作文件和检查点名称对应的内部存储路径
 *
 * @param file 工作文件的绝对路径，不能是根目录
 * @param id 检查点名称，以 ASCII 字母或数字开头，后续可用字母、数字、-、_、.
 * @param out 接收内部路径的缓冲区
 * @param capacity 缓冲区容量，包含字符串结束符
 * @return int 成功返回 0，路径、名称非法或容量不足时返回负错误码
 * @note 只进行规范化、校验和编码，不查文件、不创建目录、不复制内容。
 */
int checkpointfs_path(const char *file, const char *id, char *out,
                      size_t capacity) {
    char normalized[CHECKPOINTFS_PATH_MAX];
    char key[CHECKPOINTFS_NAME_MAX + 1];
    int ret, length;
    if (!out || !capacity) return -EINVAL;
    out[0] = '\0';
    if (!valid_id(id)) return -EINVAL;
    ret = normalize_path(file, normalized, sizeof(normalized));
    if (ret < 0) return ret;
    if (!strcmp(normalized, "/")) return -EISDIR;
    ret = make_key(normalized, key, sizeof(key));
    if (ret < 0) return ret;
    length = snprintf(out, capacity, "%s/%s/%s", CHECKPOINTFS_STORAGE_DIR,
                      key, id);
    if (length < 0 || (size_t)length >= capacity) {
        out[0] = '\0';
        return -ENAMETOOLONG;
    }
    return 0;
}

static int upper_hex(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int valid_key_component(const char *text, size_t size) {
    size_t i;
    if (!size || (size == 1 && text[0] == '.') ||
        (size == 2 && text[0] == '.' && text[1] == '.')) return 0;
    for (i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (key_plain(ch)) continue;
        if (ch == '%' && i + 2 < size) {
            int hi = upper_hex((unsigned char)text[i + 1]);
            int lo = upper_hex((unsigned char)text[i + 2]);
            if (hi < 0 || lo < 0) return 0;
            ch = (unsigned char)((hi << 4) | lo);
            if (!ch || ch == '/' || key_plain(ch)) return 0;
            i += 2;
        } else {
            return 0;
        }
    }
    return 1;
}

static int valid_key(const char *key) {
    const char *separator = strstr(key, "__");
    const char *component = key;
    const char *p;
    if (!separator || strlen(key) > CHECKPOINTFS_NAME_MAX ||
        !valid_key_component(separator + 2, strlen(separator + 2))) return 0;
    if (separator == key) return 1;
    for (p = key; p < separator; ++p) {
        if (*p != '_') continue;
        if (!valid_key_component(component, (size_t)(p - component))) return 0;
        component = p + 1;
    }
    return valid_key_component(component, (size_t)(separator - component));
}

static int view_resolve(const char *path, char *storage, size_t size) {
    char normalized[CHECKPOINTFS_PATH_MAX];
    char *key, *id;
    int ret;
    if (!storage || !size) return -EINVAL;
    storage[0] = '\0';
    ret = normalize_path(path, normalized, sizeof(normalized));
    if (ret < 0) return ret;
    if (!has_prefix(normalized, CHECKPOINTFS_VIEW_DIR)) return -ENOENT;
    key = normalized + strlen(CHECKPOINTFS_VIEW_DIR);
    if (!*key) {
        size_t length = strlen(CHECKPOINTFS_STORAGE_DIR);
        if (length >= size) return -ENAMETOOLONG;
        memcpy(storage, CHECKPOINTFS_STORAGE_DIR, length + 1);
        return 0;
    }
    ++key;
    id = strchr(key, '/');
    if (id) *id++ = '\0';
    if (!valid_key(key) || (id && !valid_id(id))) return -ENOENT;
    if (id) id[-1] = '/';
    return join_path(CHECKPOINTFS_STORAGE_DIR, key, storage, size);
}

static void view_stat(struct stat *st) {
    st->st_mode = S_ISDIR(st->st_mode) ? S_IFDIR | 0555 : S_IFREG | 0444;
}

static int view_lookup(const char *path, char *storage, struct stat *st) {
    int ret = view_resolve(path, storage, CHECKPOINTFS_PATH_MAX);
    if (ret < 0) return ret;
    ret = PROJECT_NAME_getattr(storage, st);
    if (ret == -ENOENT && !strcmp(storage, CHECKPOINTFS_STORAGE_DIR)) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0555;
        st->st_ino = (ino_t)-3;
        st->st_nlink = 2;
        st->st_uid = getuid();
        st->st_gid = getgid();
        return 0;
    }
    if (ret < 0) return ret;
    if (!S_ISDIR(st->st_mode) && !S_ISREG(st->st_mode)) return -ENOENT;
    return 0;
}

/******************************************************************************
* SECTION: 检查点只读视图实现
*******************************************************************************/
/**
 * @brief 通过普通文件属性操作读取检查点只读视图的属性
 *
 * @param path /.checkpoints 下的完整路径
 * @param st 接收文件属性的结构体
 * @return int 成功返回 0，失败返回负错误码
 * @note 目录权限显示为 0555，普通文件显示为 0444，不改变底层 inode。
 */
int checkpointfs_view_getattr(const char *path, struct stat *st) {
    char storage[CHECKPOINTFS_PATH_MAX];
    int ret;
    if (!st) return -EINVAL;
    memset(st, 0, sizeof(*st));
    ret = view_lookup(path, storage, st);
    if (ret < 0) return ret;
    view_stat(st);
    return 0;
}

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
                           off_t offset) {
    char storage[CHECKPOINTFS_PATH_MAX];
    struct stat st;
    struct fuse_file_info fi = {0};
    int ret;
    if (offset < 0) return -EINVAL;
    ret = view_lookup(path, storage, &st);
    if (ret < 0) return ret;
    if (!S_ISREG(st.st_mode)) return -EISDIR;
    if (size > INT_MAX) size = INT_MAX;
    fi.flags = O_RDONLY;
    return PROJECT_NAME_read(storage, buf, size, offset, &fi);
}

struct view_listing {
    void *buf;
    fuse_fill_dir_t filler;
    const char *storage;
    int root;
    int error;
};

static int view_fill(void *opaque, const char *name, const struct stat *source,
                     off_t offset) {
    struct view_listing *listing = opaque;
    char path[CHECKPOINTFS_PATH_MAX];
    struct stat st;
    int ret;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return listing->filler(listing->buf, name, NULL, offset);
    if (listing->root ? !valid_key(name) : !valid_id(name)) return 0;
    if (source) {
        st = *source;
    } else {
        ret = join_path(listing->storage, name, path, sizeof(path));
        if (ret == 0) ret = PROJECT_NAME_getattr(path, &st);
        if (ret < 0) {
            listing->error = ret;
            return 1;
        }
    }
    if (listing->root ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) return 0;
    view_stat(&st);
    return listing->filler(listing->buf, name, &st, offset);
}

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
                              fuse_fill_dir_t filler, off_t offset) {
    char storage[CHECKPOINTFS_PATH_MAX];
    struct stat st;
    struct fuse_file_info fi = {0};
    struct view_listing listing = {buf, filler, storage, 0, 0};
    int ret;
    if (offset < 0 || !filler) return -EINVAL;
    ret = view_lookup(path, storage, &st);
    if (ret < 0) return ret;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
    listing.root = !strcmp(storage, CHECKPOINTFS_STORAGE_DIR);
    fi.flags = O_RDONLY;
    ret = PROJECT_NAME_readdir(storage, &listing, view_fill, offset, &fi);
    if (ret == -ENOENT && listing.root) {
        if (offset == 0 && filler(buf, ".", NULL, 1)) return 0;
        if (offset <= 1) filler(buf, "..", NULL, 2);
        return 0;
    }
    return listing.error ? listing.error : ret;
}

/* 按制表符精确拆分并保留空字段，避免 strtok 跳过空字段。 */
static int execute_request(struct checkpointfs_session *session) {
    char *fields[4];
    char decoded[CHECKPOINTFS_PATH_MAX];
    char *p;
    size_t count = 1;
    int ret;
    session->request[session->used - 1] = '\0';
    fields[0] = session->request;
    for (p = session->request; *p; ++p) {
        if (*p != '\t') continue;
        if (count == 4) return -EINVAL;
        *p = '\0';
        fields[count++] = p + 1;
    }
    if ((count != 3 || strcmp(fields[0], "create")) &&
        (count != 4 || strcmp(fields[0], "restore"))) return -EINVAL;
    ret = bonus_percent_decode(fields[1], decoded, sizeof(decoded));
    if (ret < 0) return ret;
    /* 固定内部根目录由教师准备；学生只创建各源文件的检查点目录。 */
    struct stat storage;
    ret = PROJECT_NAME_getattr(CHECKPOINTFS_STORAGE_DIR, &storage);
    if (ret == -ENOENT) {
        ret = PROJECT_NAME_mkdir(CHECKPOINTFS_STORAGE_DIR, 0755);
    } else if (ret == 0 && !S_ISDIR(storage.st_mode)) {
        ret = -ENOTDIR;
    }
    if (ret < 0) return ret;
    if (count == 3) return checkpointfs_create(decoded, fields[2]);
    return checkpointfs_restore(decoded, fields[2], fields[3]);
}

/******************************************************************************
* SECTION: 检查点控制文件实现
*******************************************************************************/
/**
 * @brief 打开只写控制文件并分配请求会话
 *
 * @param fi 文件信息；flags 须为 O_WRONLY，会话保存到 fh
 * @return int 成功返回 0，失败返回负错误码
 * @note 成功后须由 checkpointfs_control_release 释放会话。
 */
int checkpointfs_control_open(struct fuse_file_info *fi) {
    struct checkpointfs_session *session;
    if (!fi) return -EINVAL;
    if ((fi->flags & O_ACCMODE) != O_WRONLY) return -EACCES;
    session = calloc(1, sizeof(*session));
    if (!session) return -ENOMEM;
    fi->fh = (uint64_t)(uintptr_t)session;
    fi->direct_io = 1;
    return 0;
}

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
                               struct fuse_file_info *fi) {
    struct checkpointfs_session *session;
    const char *newline;
    int ret;
    if (!fi || !fi->fh || (!buf && size)) return -EINVAL;
    session = (struct checkpointfs_session *)(uintptr_t)fi->fh;
    if (session->finished) return -EALREADY;
    if (offset < 0 || (uint64_t)offset != session->used) return -EINVAL;
    if (!size) return 0;
    if (size > CHECKPOINTFS_REQUEST_MAX - session->used) {
        session->finished = 1;
        return -E2BIG;
    }
    newline = memchr(buf, '\n', size);
    if (memchr(buf, '\0', size) || (newline && newline != buf + size - 1)) {
        session->finished = 1;
        return -EINVAL;
    }
    memcpy(session->request + session->used, buf, size);
    session->used += size;
    session->request[session->used] = '\0';
    if (!newline) return (int)size;
    session->finished = 1;
    ret = execute_request(session);
    return ret < 0 ? ret : (int)size;
}

/**
 * @brief 释放控制请求会话并清空文件句柄
 *
 * @param fi 保存请求会话的文件信息
 * @return int 成功返回 0，fi 为 NULL 时返回 -EINVAL
 * @note 未完成的请求直接丢弃，不在关闭时执行。
 */
int checkpointfs_control_release(struct fuse_file_info *fi) {
    if (!fi) return -EINVAL;
    free((void *)(uintptr_t)fi->fh);
    fi->fh = 0;
    return 0;
}
