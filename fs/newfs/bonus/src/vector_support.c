#include "vectorfs.h"
#include "bonus.h"
#include "newfs.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/******************************************************************************
* SECTION: 向量计算辅助
*******************************************************************************/
/* 先累计字节计数，最后统一归一化，避免分块边界影响向量；查询和文件共用。 */
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

/******************************************************************************
* SECTION: 向量工具实现
*******************************************************************************/
/**
 * @brief 将输入字节编码为归一化的八维演示向量
 *
 * @param text 输入数据，length 为 0 时可为 NULL
 * @param length 输入字节数，不以字符串结束符判断长度
 * @param out 接收 VECTORFS_DIMENSION 个分量的数组
 * @return int 成功返回 0，参数非法返回 -EINVAL
 * @note 空输入得到零向量；不进行查询匹配或排序。
 */
int vectorfs_embed(const char *text, size_t length,
                   float out[VECTORFS_DIMENSION]) {
    double bins[VECTORFS_DIMENSION] = {0};
    if (!out || (!text && length)) return -EINVAL;
    embed_update(bins, text, length);
    embed_finish(bins, out);
    return 0;
}

/**
 * @brief 计算两个向量的余弦相似度
 *
 * @param a 第一个 VECTORFS_DIMENSION 维向量
 * @param b 第二个 VECTORFS_DIMENSION 维向量
 * @return double 返回 [-1, 1] 内的分数；空指针、零向量或非有限计算结果返回 0
 */
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

/**
 * @brief 释放加载的文档数组及其路径字符串
 *
 * @param documents vectorfs_load_documents 返回的数组，可为 NULL
 * @param count 数组中的文档数量
 * @return void
 * @note 释放后 name、relative_path 等指针均失效，不要单独释放这些字段。
 */
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
        int got = newfs_read(path, bytes, wanted, offset, &fi);
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
    int ret = newfs_readdir(path, &entries, collect_name, 0, &fi);
    if (ret >= 0) ret = entries.error;
    /* 先收集名字，再递归访问；不能在 filler 内重入可能持锁的文件系统。 */
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
                ret = newfs_getattr(child, &st);
                if (ret == 0 && S_ISDIR(st.st_mode))
                    ret = collect_documents(child, list);
                else if (ret == 0 && S_ISREG(st.st_mode))
                    ret = append_document(list, child,
                                          child + strlen(VECTORFS_DOCUMENTS_DIR) + 1,
                                          st.st_size);
                /* 不跟随软链接，即使它指向普通文档。 */
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

/******************************************************************************
* SECTION: 文档加载工具实现
*******************************************************************************/
/**
 * @brief 从自己的文件系统递归加载 /documents 下的普通文件及其向量
 *
 * @param out 接收按相对路径排序的文档数组，使用后调用 vectorfs_documents_free
 * @param count 接收文档数量
 * @return int 成功返回 0，失败返回负错误码；目录不存在或无文档时成功返回空集合
 * @note 不跟随软链接，不按查询评分或排序；失败时内部释放已分配的数据。
 */
int vectorfs_load_documents(struct vectorfs_document **out, size_t *count) {
    struct document_list list = {0};
    struct stat st;
    int ret;
    if (!out || !count) return -EINVAL;
    *out = NULL;
    *count = 0;
    ret = newfs_getattr(VECTORFS_DOCUMENTS_DIR, &st);
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

/******************************************************************************
* SECTION: 搜索路径解析实现
*******************************************************************************/
/**
 * @brief 解析搜索路径，输出查询文本、结果分数和排名
 *
 * @param path 完整路径，可带末尾斜杠；先分段再解码查询中的百分号编码
 * @param query 接收新分配的查询字符串；返回 0 或失败时为 NULL
 * @param rate 接收结果名中的分数；仅返回 2 时有效，其余为 0
 * @param rank 接收结果名中的排名，从 0 开始；仅返回 2 时有效，其余为 0
 * @return int 0：/.search/；1：/.search/<query>/；
 *             2：/.search/<query>/<rate>__<rank>/；失败返回负错误码
 * @note 用法（包含 <stdlib.h>）：
 *       char *query = NULL;
 *       double rate = 0;
 *       size_t rank = 0;
 *       int kind = vectorfs_parse_path(path, &query, &rate, &rank);
 *       if (kind < 0) return kind;
 *       // kind 为 0/1 时处理目录，为 2 时按 rate/rank 查找实际匹配结果。
 *       // query 仅在 kind 为 1/2 时可用，不要事先分配数组。
 *       free(query); // 所有后续返回路径都须释放；free(NULL) 安全。
 * @note 这里只解析名称，不保证对应结果存在；仍须匹配排序并确认分数、排名。
 */
int vectorfs_parse_path(const char *path, char **query, double *rate, size_t *rank) {
    if (!query || !rate || !rank) return -EINVAL;
    *query = NULL;
    *rate = 0;
    *rank = 0;
    if (!path) return -EINVAL;
    const size_t root_length = sizeof(VECTORFS_SEARCH_DIR) - 1;
    if (strncmp(path, VECTORFS_SEARCH_DIR, root_length) ||
        (path[root_length] && path[root_length] != '/')) return -ENOENT;
    const char *part = path + root_length;
    if (!*part || !part[1]) return 0;
    ++part;
    char encoded[VECTORFS_QUERY_MAX], decoded[VECTORFS_QUERY_MAX];
    size_t length = strcspn(part, "/");
    if (!length) return -ENOENT;
    if (length >= sizeof(encoded)) return -ENAMETOOLONG;
    memcpy(encoded, part, length);
    encoded[length] = '\0';
    if (!strcmp(encoded, ".") || !strcmp(encoded, "..")) return -EINVAL;
    int ret = bonus_percent_decode(encoded, decoded, sizeof(decoded));
    if (ret < 0) return ret;
    if (!*decoded) return -EINVAL;
    part += length;
    int kind = 1;
    double parsed_rate = 0;
    size_t parsed_rank = 0;
    if (*part && part[1]) {
        ++part;
        length = strcspn(part, "/");
        if (part[length] && part[length + 1]) return -ENOTDIR;
        char name[VECTORFS_RESULT_NAME_MAX], canonical[VECTORFS_RESULT_NAME_MAX];
        if (!length || length >= sizeof(name)) return -ENOENT;
        memcpy(name, part, length);
        name[length] = '\0';
        char *end;
        errno = 0;
        parsed_rate = strtod(name, &end);
        if (errno || end == name || !isfinite(parsed_rate) ||
            parsed_rate < -1 || parsed_rate > 1 || strncmp(end, "__", 2)) return -ENOENT;
        const char *digits = end + 2;
        if (*digits < '0' || *digits > '9') return -ENOENT;
        errno = 0;
        unsigned long long value = strtoull(digits, &end, 10);
        if (errno || *end || value > SIZE_MAX) return -ENOENT;
        parsed_rank = (size_t)value;
        int n = snprintf(canonical, sizeof(canonical), "%.6f__%02zu", parsed_rate, parsed_rank);
        if (n < 0 || (size_t)n >= sizeof(canonical) || strcmp(canonical, name)) return -ENOENT;
        kind = 2;
    }
    *query = strdup(decoded);
    if (!*query) return -ENOMEM;
    *rate = parsed_rate;
    *rank = parsed_rank;
    return kind;
}

/******************************************************************************
* SECTION: 教师提供的 query 缓存
*******************************************************************************/
#define VECTORFS_CACHE_BUCKETS 64
struct query_cache_entry {
    struct query_cache_entry *next;
    char *query;
    struct vectorfs_result results[VECTORFS_TOP_K];
    size_t count;
};
static struct query_cache_entry *query_cache[VECTORFS_CACHE_BUCKETS];

void vectorfs_cache_clear(void) {
    for (size_t i = 0; i < VECTORFS_CACHE_BUCKETS; ++i) {
        while (query_cache[i]) {
            struct query_cache_entry *entry = query_cache[i];
            query_cache[i] = entry->next;
            free(entry->query);
            free(entry);
        }
    }
}

int vectorfs_cache_get(const char *query, const struct vectorfs_result **results, size_t *count) {
    if (!query || !results || !count) return -EINVAL;
    *results = NULL;
    *count = 0;
    size_t hash = 5381;
    for (const unsigned char *p = (const unsigned char *)query; *p; ++p)
        hash = hash * 33 + *p;
    size_t bucket = hash % VECTORFS_CACHE_BUCKETS;
    for (struct query_cache_entry *entry = query_cache[bucket]; entry; entry = entry->next) {
        if (!strcmp(entry->query, query)) {
            *results = entry->results;
            *count = entry->count;
            return 0;
        }
    }
    struct query_cache_entry *entry = calloc(1, sizeof(*entry));
    if (!entry) return -ENOMEM;
    entry->query = strdup(query);
    if (!entry->query) { free(entry); return -ENOMEM; }
    int ret = vectorfs_search(query, entry->results, &entry->count);
    if (ret < 0) {
        free(entry->query);
        free(entry);
        return ret;
    }
    entry->next = query_cache[bucket];
    query_cache[bucket] = entry;
    *results = entry->results;
    *count = entry->count;
    return 0;
}

/******************************************************************************
* SECTION: 搜索结果名称与目标路径
*******************************************************************************/
int vectorfs_result_name(const struct vectorfs_result *result, char *out, size_t capacity) {
    if (!out || !capacity) return -EINVAL;
    out[0] = '\0';
    if (!result || !isfinite(result->rate)) return -EINVAL;
    int length = snprintf(out, capacity, "%.6f__%02zu", result->rate, result->rank);
    if (length < 0 || (size_t)length >= capacity) {
        out[0] = '\0';
        return length < 0 ? -EIO : -ENAMETOOLONG;
    }
    return 0;
}

int vectorfs_result_target(const struct vectorfs_result *result, char *out, size_t capacity) {
    if (!out || !capacity) return -EINVAL;
    out[0] = '\0';
    if (!result) return -EINVAL;
    int length = snprintf(out, capacity, "../../documents/%s", result->source);
    if (length < 0 || (size_t)length >= capacity) {
        out[0] = '\0';
        return length < 0 ? -EIO : -ENAMETOOLONG;
    }
    return 0;
}
