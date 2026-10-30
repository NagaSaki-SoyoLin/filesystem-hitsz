#include "bonus.h"
#include "checkpointfs.h"
#include "vectorfs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/******************************************************************************
* SECTION: FUSE 路由状态
*******************************************************************************/
/* 这里只分发命名空间；普通存储仍由学生实现，不提供复制或替换原语。 */
static struct fuse_operations ordinary;
enum route { BASE, CONTROL, VIEW, SEARCH, INTERNAL };
#define PATH_CAPACITY 8192

static int prefix(const char *path, const char *root) {
    size_t n = strlen(root);
    return !strncmp(path, root, n) && (!path[n] || path[n] == '/');
}

static enum route route(const char *path) {
    if (prefix(path, "/.checkpointfs-data")) return INTERNAL;
    if (prefix(path, "/.checkpoint")) return CONTROL;
    if (prefix(path, "/.checkpoints")) return VIEW;
    if (prefix(path, "/.search")) return SEARCH;
    return BASE;
}

static int writable(const char *path) {
    switch (route(path)) {
    case INTERNAL: case CONTROL: return -EPERM;
    case VIEW: case SEARCH: return -EROFS;
    default: return 0;
    }
}

#define NORMALIZE(path, name) \
    char name[PATH_CAPACITY]; \
    int normalized_##name = normalize_path(path, name, sizeof(name)); \
    if (normalized_##name < 0) return normalized_##name
#define MUTABLE(path, name) \
    NORMALIZE(path, name); \
    int gated_##name = writable(name); \
    if (gated_##name < 0) return gated_##name

static int routed_getattr(const char *path, struct stat *st) {
    NORMALIZE(path, p);
    switch (route(p)) {
    case INTERNAL: return -ENOENT;
    case VIEW: return checkpointfs_view_getattr(p, st);
    case SEARCH: return vectorfs_getattr(p, st);
    case BASE: return ordinary.getattr(p, st);
    case CONTROL:
        memset(st, 0, sizeof(*st));
        if (!strcmp(p, "/.checkpoint")) {
            st->st_mode = S_IFDIR | 0555;
            st->st_nlink = 2;
        } else if (!strcmp(p, "/.checkpoint/control")) {
            st->st_mode = S_IFREG | 0222;
            st->st_nlink = 1;
        } else return -ENOENT;
        st->st_uid = getuid();
        st->st_gid = getgid();
        return 0;
    }
    return -EINVAL;
}

struct root_listing {
    void *buf;
    fuse_fill_dir_t filler;
    off_t offset, position;
};

static int root_filler(void *buf, const char *name, const struct stat *st, off_t cookie) {
    struct root_listing *listing = buf;
    (void)cookie;
    if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".checkpointfs-data")) return 0;
    off_t position = listing->position++;
    return position < listing->offset ? 0 : listing->filler(listing->buf, name, st, position + 1);
}

static int routed_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    if (offset < 0) return -EINVAL;
    switch (route(p)) {
    case INTERNAL: return -ENOENT;
    case VIEW: return checkpointfs_view_readdir(p, buf, filler, offset);
    case SEARCH: return vectorfs_readdir(p, buf, filler, offset, fi);
    case CONTROL: {
        if (strcmp(p, "/.checkpoint")) return -ENOTDIR;
        const char *names[] = {".", "..", "control"};
        for (off_t i = offset; i < 3; ++i)
            if (filler(buf, names[i], NULL, i + 1)) break;
        return 0;
    }
    case BASE: break;
    }
    if (strcmp(p, "/")) return ordinary.readdir(p, buf, filler, offset, fi);
    const char *names[] = {".", "..", ".checkpoint", ".checkpoints", ".search"};
    for (off_t i = offset; i < 5; ++i)
        if (filler(buf, names[i], NULL, i + 1)) return 0;
    struct root_listing listing = {buf, filler, offset, 5};
    return ordinary.readdir(p, &listing, root_filler, 0, fi);
}

static int routed_open(const char *path, struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    enum route kind = route(p);
    if (kind == INTERNAL) return -ENOENT;
    if (!strcmp(p, "/.checkpoint/control")) return checkpointfs_control_open(fi);
    if ((fi->flags & O_ACCMODE) != O_RDONLY || (fi->flags & O_TRUNC)) {
        int ret = writable(p);
        if (ret < 0) return ret;
    }
    int ret;
    if (kind == BASE) ret = ordinary.open ? ordinary.open(p, fi) : 0;
    else {
        struct stat st;
        ret = routed_getattr(p, &st);
        if (!ret && !S_ISREG(st.st_mode)) ret = S_ISDIR(st.st_mode) ? -EISDIR : -ELOOP;
    }
    if (!ret) fi->direct_io = 1;
    return ret;
}

static int routed_read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    switch (route(p)) {
    case INTERNAL: return -ENOENT;
    case VIEW: return checkpointfs_view_read(p, buf, size, offset);
    case BASE: return ordinary.read(p, buf, size, offset, fi);
    default: return -EACCES;
    }
}

static int routed_write(const char *path, const char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    vectorfs_cache_clear();
    if (!strcmp(p, "/.checkpoint/control")) return checkpointfs_control_write(buf, size, offset, fi);
    int ret = writable(p);
    if (ret < 0) return ret;
    return ordinary.write ? ordinary.write(p, buf, size, offset, fi) : -ENOSYS;
}

static int routed_release(const char *path, struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    if (!strcmp(p, "/.checkpoint/control")) return checkpointfs_control_release(fi);
    return route(p) == BASE && ordinary.release ? ordinary.release(p, fi) : 0;
}

static int routed_opendir(const char *path, struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    if (route(p) == BASE && ordinary.opendir) return ordinary.opendir(p, fi);
    struct stat st;
    int ret = routed_getattr(p, &st);
    return ret < 0 ? ret : S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}

static int routed_releasedir(const char *path, struct fuse_file_info *fi) {
    NORMALIZE(path, p);
    return route(p) == BASE && ordinary.releasedir ? ordinary.releasedir(p, fi) : 0;
}

static int routed_readlink(const char *path, char *buf, size_t size) {
    NORMALIZE(path, p);
    if (route(p) == INTERNAL) return -ENOENT;
    if (route(p) == SEARCH) return vectorfs_readlink(p, buf, size);
    return route(p) == BASE && ordinary.readlink ? ordinary.readlink(p, buf, size) : -EINVAL;
}

static int routed_access(const char *path, int mask) {
    NORMALIZE(path, p);
    if (route(p) == BASE && ordinary.access) return ordinary.access(p, mask);
    if ((mask & W_OK) && strcmp(p, "/.checkpoint/control")) {
        int ret = writable(p);
        if (ret < 0) return ret;
    }
    struct stat st;
    int ret = routed_getattr(p, &st);
    if (ret < 0) return ret;
    if (((mask & R_OK) && !(st.st_mode & 0444)) ||
        ((mask & W_OK) && !(st.st_mode & 0222)) ||
        ((mask & X_OK) && !(st.st_mode & 0111))) return -EACCES;
    return 0;
}

static int routed_mkdir(const char *path, mode_t mode) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.mkdir ? ordinary.mkdir(p, mode) : -ENOSYS;
}
static int routed_mknod(const char *path, mode_t mode, dev_t dev) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.mknod ? ordinary.mknod(p, mode, dev) : -ENOSYS;
}
static int routed_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    int ret = ordinary.create(p, mode, fi);
    if (!ret) fi->direct_io = 1;
    return ret;
}
static int routed_truncate(const char *path, off_t size) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.truncate ? ordinary.truncate(p, size) : -ENOSYS;
}
static int routed_unlink(const char *path) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.unlink ? ordinary.unlink(p) : -ENOSYS;
}
static int routed_rmdir(const char *path) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.rmdir ? ordinary.rmdir(p) : -ENOSYS;
}
static int routed_link(const char *source, const char *target) {
    MUTABLE(source, from);
    MUTABLE(target, to);
    vectorfs_cache_clear();
    return ordinary.link ? ordinary.link(from, to) : -ENOSYS;
}
static int routed_rename(const char *source, const char *target) {
    MUTABLE(source, from);
    MUTABLE(target, to);
    vectorfs_cache_clear();
    return ordinary.rename ? ordinary.rename(from, to) : -ENOSYS;
}
static int routed_utimens(const char *path, const struct timespec times[2]) {
    MUTABLE(path, p);
    return ordinary.utimens ? ordinary.utimens(p, times) : -ENOSYS;
}
static int routed_chmod(const char *path, mode_t mode) {
    MUTABLE(path, p);
    return ordinary.chmod ? ordinary.chmod(p, mode) : -ENOSYS;
}
static int routed_chown(const char *path, uid_t uid, gid_t gid) {
    MUTABLE(path, p);
    return ordinary.chown ? ordinary.chown(p, uid, gid) : -ENOSYS;
}
static int routed_symlink(const char *target, const char *path) {
    MUTABLE(path, p);
    vectorfs_cache_clear();
    return ordinary.symlink ? ordinary.symlink(target, p) : -ENOSYS;
}

static void *routed_init(struct fuse_conn_info *connection) {
    vectorfs_cache_clear();
    void *data = ordinary.init ? ordinary.init(connection) : NULL;
    const char *reserved[] = {"/.checkpoint", "/.checkpoints", "/.search"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        struct stat st;
        int ret = ordinary.getattr(reserved[i], &st);
        if (ret != -ENOENT) {
            fprintf(stderr, "bonus: reserved path unavailable: %s (%d)\n", reserved[i], ret);
            fuse_exit(fuse_get_context()->fuse);
            break;
        }
    }
    return data;
}

static void routed_destroy(void *data) {
    vectorfs_cache_clear();
    if (ordinary.destroy) ordinary.destroy(data);
}

/******************************************************************************
* SECTION: FUSE 路由接入实现
*******************************************************************************/
/**
 * @brief 为已有 FUSE 操作表接入附加路径分发
 *
 * @param operations 已有操作表，原地安装包装回调；必须包含 getattr/readdir/read
 * @return int 成功返回 0，参数不完整或重复注册返回 -EINVAL
 * @note 由主程序在 fuse_main 前调用；原普通文件函数保持不变，供学生实现直接调用。
 */
int bonus_register(struct fuse_operations *operations) {
    if (!operations || !operations->getattr || !operations->readdir || !operations->read ||
        operations->getattr == routed_getattr) return -EINVAL;
    ordinary = *operations;
    operations->init = routed_init;
    operations->destroy = routed_destroy;
    operations->getattr = routed_getattr;
    operations->readdir = routed_readdir;
    operations->open = routed_open;
    operations->read = routed_read;
    operations->write = routed_write;
    operations->release = routed_release;
    operations->opendir = routed_opendir;
    operations->releasedir = routed_releasedir;
    operations->readlink = routed_readlink;
    operations->access = routed_access;
    operations->mkdir = routed_mkdir;
    operations->mknod = routed_mknod;
    if (ordinary.create) operations->create = routed_create;
    operations->truncate = routed_truncate;
    operations->unlink = routed_unlink;
    operations->rmdir = routed_rmdir;
    operations->link = routed_link;
    operations->rename = routed_rename;
    operations->utimens = routed_utimens;
    operations->chmod = routed_chmod;
    operations->chown = routed_chown;
    operations->symlink = routed_symlink;
    return 0;
}
