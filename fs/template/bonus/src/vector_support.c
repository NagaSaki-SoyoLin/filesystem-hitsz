#include "vectorfs.h"
#include "bonus.h"
#include "PROJECT_NAME.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Accumulate bytes, not per-chunk normalized vectors: chunk boundaries must
 * not change the embedding. The same primitives serve queries and files. */
static void embed_update(double bins[VECTORFS_DIMENSION], const char *bytes,
                         size_t length) {
    for (size_t i = 0; i < length; ++i)
        bins[(unsigned char)bytes[i] % VECTORFS_DIMENSION] += 1.0;
}

static void embed_finish(const double bins[VECTORFS_DIMENSION],
                         float out[VECTORFS_DIMENSION]) {
    double norm = 0.0;
    for (size_t i = 0; i < VECTORFS_DIMENSION; ++i)
        norm += bins[i] * bins[i];
    norm = sqrt(norm);
    for (size_t i = 0; i < VECTORFS_DIMENSION; ++i)
        out[i] = norm == 0.0 ? 0.0f : (float)(bins[i] / norm);
}

int vectorfs_embed(const char *text, size_t length,
                   float out[VECTORFS_DIMENSION]) {
    double bins[VECTORFS_DIMENSION] = {0};
    if (!out || (!text && length)) return -EINVAL;
    embed_update(bins, text, length);
    embed_finish(bins, out);
    return 0;
}

double vectorfs_similarity(const float a[VECTORFS_DIMENSION],
                           const float b[VECTORFS_DIMENSION]) {
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    if (!a || !b) return 0.0;
    for (size_t i = 0; i < VECTORFS_DIMENSION; ++i) {
        dot += (double)a[i] * b[i];
        norm_a += (double)a[i] * a[i];
        norm_b += (double)b[i] * b[i];
    }
    if (norm_a == 0.0 || norm_b == 0.0) return 0.0;
    double score = dot / (sqrt(norm_a) * sqrt(norm_b));
    if (!isfinite(score)) return 0.0;
    return fmax(-1.0, fmin(1.0, score));
}

void vectorfs_documents_free(struct vectorfs_document *documents, size_t count) {
    if (!documents) return;
    for (size_t i = 0; i < count; ++i)
        free((void *)documents[i].relative_path);
    free(documents);
}

struct document_list {
    struct vectorfs_document *items;
    size_t count;
    size_t capacity;
};

static int append_document(struct document_list *list, const char *path,
                           const char *relative_path, off_t size) {
    double bins[VECTORFS_DIMENSION] = {0};
    char bytes[4096];
    off_t offset = 0;
    struct fuse_file_info fi = {.flags = O_RDONLY};
    if (size < 0) return -EIO;
    while (offset < size) {
        size_t wanted = size - offset < (off_t)sizeof(bytes)
                      ? (size_t)(size - offset) : sizeof(bytes);
        int got = PROJECT_NAME_read(path, bytes, wanted, offset, &fi);
        if (got < 0) return (int)got;
        if (got == 0 || (size_t)got > wanted) return -EIO;
        embed_update(bins, bytes, (size_t)got);
        offset += (size_t)got;
    }
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 16;
        if (capacity < list->capacity ||
            capacity > SIZE_MAX / sizeof(*list->items)) return -ENOMEM;
        struct vectorfs_document *items =
            realloc(list->items, capacity * sizeof(*items));
        if (!items) return -ENOMEM;
        list->items = items;
        list->capacity = capacity;
    }
    char *relative = strdup(relative_path);
    if (!relative) return -ENOMEM;
    struct vectorfs_document *document = &list->items[list->count++];
    document->relative_path = relative;
    const char *slash = strrchr(relative, '/');
    document->name = slash ? slash + 1 : relative;
    embed_finish(bins, document->embedding);
    return 0;
}

struct directory_entry {
    struct directory_entry *next;
    char name[];
};

struct directory_list {
    struct directory_entry *head;
    int error;
};

static int collect_name(void *buf, const char *name, const struct stat *st,
                        off_t offset) {
    struct directory_list *entries = buf;
    (void)st;
    (void)offset;
    if (entries->error) return 1;
    if (!name || !*name || strchr(name, '/')) {
        entries->error = -EIO;
        return 1;
    }
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 0;
    size_t length = strlen(name);
    if (length >= VECTORFS_TARGET_MAX) {
        entries->error = -ENAMETOOLONG;
        return 1;
    }
    struct directory_entry *entry = malloc(sizeof(*entry) + length + 1);
    if (!entry) {
        entries->error = -ENOMEM;
        return 1;
    }
    memcpy(entry->name, name, length + 1);
    entry->next = entries->head;
    entries->head = entry;
    return 0;
}

static int collect_documents(const char *path, struct document_list *list) {
    struct directory_list entries = {0};
    struct fuse_file_info fi = {0};
    int ret = PROJECT_NAME_readdir(path, &entries, collect_name, 0, &fi);
    if (ret >= 0) ret = entries.error;
    /* Do not call newfs recursively from its filler: readdir may hold locks. */
    while (entries.head) {
        struct directory_entry *entry = entries.head;
        entries.head = entry->next;
        if (ret == 0) {
            char child[VECTORFS_TARGET_MAX];
            struct stat st;
            int length = snprintf(child, sizeof(child), "%s/%s", path, entry->name);
            if (length < 0 || (size_t)length >= sizeof(child))
                ret = -ENAMETOOLONG;
            else {
                ret = PROJECT_NAME_getattr(child, &st);
                if (ret == 0 && S_ISDIR(st.st_mode))
                    ret = collect_documents(child, list);
                else if (ret == 0 && S_ISREG(st.st_mode))
                    ret = append_document(list, child,
                                          child + strlen(VECTORFS_DOCUMENTS_DIR) + 1,
                                          st.st_size);
                /* Never follow symlinks, even to regular documents. */
            }
        }
        free(entry);
    }
    return ret;
}

static int compare_document_paths(const void *left, const void *right) {
    const struct vectorfs_document *a = left, *b = right;
    return strcmp(a->relative_path, b->relative_path);
}

int vectorfs_load_documents(struct vectorfs_document **out, size_t *count) {
    struct document_list list = {0};
    struct stat st;
    int ret;
    if (!out || !count) return -EINVAL;
    *out = NULL;
    *count = 0;
    ret = PROJECT_NAME_getattr(VECTORFS_DOCUMENTS_DIR, &st);
    if (ret == -ENOENT) return 0;
    if (ret < 0) return ret;
    if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
    ret = collect_documents(VECTORFS_DOCUMENTS_DIR, &list);
    if (ret < 0) {
        vectorfs_documents_free(list.items, list.count);
        return ret;
    }
    if (list.count > 1)
        qsort(list.items, list.count, sizeof(*list.items), compare_document_paths);
    *out = list.items;
    *count = list.count;
    return 0;
}

/* Split before decoding: an encoded slash is query text, not a path separator. */
int vectorfs_parse_path(const char *path, char query[VECTORFS_QUERY_MAX],
                        const char **result) {
    const size_t root_length = sizeof(VECTORFS_SEARCH_DIR) - 1;
    char encoded[VECTORFS_QUERY_MAX];
    if (!path || !query || !result) return -EINVAL;
    *result = NULL;
    if (strncmp(path, VECTORFS_SEARCH_DIR, root_length) ||
        (path[root_length] && path[root_length] != '/')) return -ENOENT;
    const char *part = path + root_length;
    if (!*part || !part[1]) return 0;
    ++part;
    size_t length = strcspn(part, "/");
    if (!length) return -ENOENT;
    if (length >= sizeof(encoded)) return -ENAMETOOLONG;
    memcpy(encoded, part, length);
    encoded[length] = '\0';
    if (!strcmp(encoded, ".") || !strcmp(encoded, "..")) return -EINVAL;
    int ret = bonus_percent_decode(encoded, query, VECTORFS_QUERY_MAX);
    if (ret < 0) return ret;
    if (!*query) return -EINVAL;
    part += length;
    if (*part && part[1]) {
        ++part;
        if (strchr(part, '/')) return -ENOTDIR;
        if (strlen(part) >= VECTORFS_RESULT_NAME_MAX) return -ENOENT;
        *result = part;
    }
    return 1;
}
