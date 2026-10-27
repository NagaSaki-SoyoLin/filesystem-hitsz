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

/* IDs start with an ASCII letter or digit; subsequent dots are allowed. */
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

/* Only separators are literal underscores; every input underscore is %5F. */
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

int checkpointfs_path(const char *file, const char *id, char *out,
                      size_t capacity) {
    char normalized[CHECKPOINTFS_PATH_MAX];
    char key[CHECKPOINTFS_NAME_MAX + 1];
    int ret, length;
    if (!out || !capacity) return -EINVAL;
    out[0] = '\0';
    if (!valid_id(id)) return -EINVAL;
    ret = bonus_normalize_path(file, normalized, sizeof(normalized));
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
    ret = bonus_normalize_path(path, normalized, sizeof(normalized));
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

/* Split exact tab fields, preserving empty fields instead of strtok skipping. */
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
    if (count == 3) return checkpointfs_create(decoded, fields[2]);
    return checkpointfs_restore(decoded, fields[2], fields[3]);
}

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

int checkpointfs_control_release(struct fuse_file_info *fi) {
    if (!fi) return -EINVAL;
    free((void *)(uintptr_t)fi->fh);
    fi->fh = 0;
    return 0;
}
