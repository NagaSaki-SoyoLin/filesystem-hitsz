#ifndef VECTORFS_H
#define VECTORFS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

#define VECTORFS_DIMENSION 8
#define VECTORFS_TOP_K 10
#define VECTORFS_SEARCH_DIR "/.search"
#define VECTORFS_DOCUMENTS_DIR "/documents"
#define VECTORFS_QUERY_MAX 4096
#define VECTORFS_TARGET_MAX 4096
#define VECTORFS_RESULT_NAME_MAX 64

/******************************************************************************
* SECTION: 只读文档数据（由 vectorfs_load_documents 生成）
*******************************************************************************/
/* 文档数组已按 relative_path 字典序升序排列；使用完毕后统一调用 vectorfs_documents_free 释放。 */
struct vectorfs_document {
    const char *name;                       /* 文档基名，如 "inode.txt" */
    const char *relative_path;              /* 相对于 /documents 的路径（无前导斜杠），如 "os/inode.txt" */
    float embedding[VECTORFS_DIMENSION];    /* 教师已计算好的 8 维归一化向量 */
};

/******************************************************************************
* SECTION: 查询缓存与学生搜索接口
*******************************************************************************/
struct vectorfs_result {
    char source[VECTORFS_TARGET_MAX]; /* /documents 下的相对路径（如 "os/a.txt"，不带 "/documents/" 前缀） */
    double rate;                     /* 原始余弦相似度（[-1.0, 1.0]），排序前不要四舍五入 */
    size_t rank;                     /* 从 0 开始的排名下标（0 .. count-1），对应名称后缀 __00 .. __09 */
};

/**
 * @brief 【学生实现】计算一次查询的前十个匹配结果，供缓存未命中时自动调用
 * @param query 已解码的查询文本（如 "file system"）
 * @param results 教师缓存预分配好的 VECTORFS_TOP_K（10）项数组，学生直接填入，切勿 malloc/free
 * @param count 输出实际填入的结果数量（即 min(文档总数, VECTORFS_TOP_K)，无文档时设为 0）
 * @return int 成功返回 0，失败返回负错误码
 * @note 排序规则：按 rate 降序排列；rate 相同时按 source 相对路径字典序（strcmp）升序排列；
 *       排好后依次填入 results[0 .. *count-1]，并令 results[i].rank = i。
 */
int vectorfs_search(const char *query,
                    struct vectorfs_result results[VECTORFS_TOP_K], size_t *count);

/**
 * @brief 【教师提供】按已解码的 query 查找缓存，未命中时自动调用学生的 vectorfs_search
 * @param query 已解码的查询字符串（按内容区分不同缓存项）
 * @param results 输出指向缓存内部结果数组的只读指针，切勿修改或 free
 * @param count 输出该查询的结果数量（0 <= *count <= VECTORFS_TOP_K），可直接用 results[rank] 索引
 * @return int 成功返回 0，失败返回负错误码（失败结果不会进入缓存）
 * @note 调用示例：
 *       const struct vectorfs_result *results = NULL;
 *       size_t count = 0;
 *       int ret = vectorfs_cache_get(query, &results, &count);
 *       if (ret < 0) { free(query); return ret; }
 */
int vectorfs_cache_get(const char *query, const struct vectorfs_result **results, size_t *count);

/** @brief 【教师提供】清空所有查询缓存；普通文件写入/删除/改名及卸载前由路由层自动调用。 */
void vectorfs_cache_clear(void);

/**
 * @brief 【教师提供】根据结果的 rate 和 rank 生成标准目录项名称（格式 "%.6f__%02zu"，如 "0.948683__01"）
 * @param result 指向单个搜索结果的指针（如 &results[i]）
 * @param out 接收目录项名称的缓冲区（建议大小 VECTORFS_RESULT_NAME_MAX）
 * @param capacity 缓冲区容量（含末尾 '\0'）
 * @return int 成功返回 0，容量不足返回 -ENAMETOOLONG（失败时不会留下截断字符串）
 */
int vectorfs_result_name(const struct vectorfs_result *result, char *out, size_t capacity);

/**
 * @brief 【教师提供】根据结果的 source 生成软链接相对目标路径（格式 "../../documents/%s"）
 * @param result 指向单个搜索结果的指针（如 &results[rank]）
 * @param out 接收目标路径的缓冲区（建议大小 VECTORFS_TARGET_MAX）
 * @param capacity 缓冲区容量（含末尾 '\0'）
 * @return int 成功返回 0，容量不足返回 -ENAMETOOLONG（失败时不会留下截断路径）
 */
int vectorfs_result_target(const struct vectorfs_result *result, char *out, size_t capacity);

/******************************************************************************
* SECTION: 向量与文档工具（教师已实现，学生直接调用）
*******************************************************************************/
/**
 * @brief 【教师提供】将输入字节序列编码为归一化的 8 维教学演示向量
 *
 * @param text 输入字节指针（例如 query 字符串；当 length 为 0 时允许为 NULL）
 * @param length 输入字节数（如 strlen(query)，按显式长度处理）
 * @param out 接收 VECTORFS_DIMENSION（8）个 float 分量的数组
 * @return int 成功返回 0，参数非法返回 -EINVAL
 * @note 空输入（length == 0）得到全零向量。
 */
int vectorfs_embed(const char *text, size_t length,
                   float out[VECTORFS_DIMENSION]);
/**
 * @brief 【教师提供】计算两个 8 维向量的余弦相似度
 *
 * @param a 第一个 VECTORFS_DIMENSION 维向量
 * @param b 第二个 VECTORFS_DIMENSION 维向量
 * @return double 返回 [-1.0, 1.0] 范围内的余弦相似度；零向量或空指针安全返回 0.0
 */
double vectorfs_similarity(const float a[VECTORFS_DIMENSION],
                           const float b[VECTORFS_DIMENSION]);
/**
 * @brief 【教师提供】通过你实现的普通 newfs 接口，递归加载 /documents 下所有普通文件及其向量
 *
 * @param out 输出按 relative_path 升序排列的文档数组指针；使用完毕后必须调用 vectorfs_documents_free 释放
 * @param count 输出加载到的普通文档数量
 * @return int 成功返回 0，失败返回负错误码；当 /documents 目录不存在或为空时，成功返回 0 且 *count = 0
 * @note 自动跳过软链接；若中途读取失败，函数内部会自动清理已分配的内存。
 */
int vectorfs_load_documents(struct vectorfs_document **out, size_t *count);
/**
 * @brief 【教师提供】释放由 vectorfs_load_documents 分配的文档数组及内部路径字符串
 *
 * @param documents vectorfs_load_documents 返回的数组指针（允许传入 NULL）
 * @param count 数组中的文档数量
 * @return void
 * @note 调用后 documents[i].name 和 documents[i].relative_path 均立即失效，如需保留路径请提前拷贝。
 */
void vectorfs_documents_free(struct vectorfs_document *documents, size_t count);
/******************************************************************************
* SECTION: 搜索路径解析（教师已实现，学生直接调用）
*******************************************************************************/
/**
 * @brief 【教师提供】解析 /.search 下的挂载内路径，识别路径类别并提取 query、rate 和 rank
 *
 * @param path FUSE 传入的完整路径（支持末尾带或不带 '/'）
 * @param query 输出新分配并已完成百分号解码的查询字符串；当返回 0 或负错误码时为 NULL
 * @param rate 输出结果名中的 6 位小数得分；仅在返回 2 时有效，其余情况为 0.0
 * @param rank 输出结果名中从 0 开始的排名；仅在返回 2 时有效，其余情况为 0
 * @return int
 *         - 0 : 搜索根目录 "/.search"
 *         - 1 : 查询目录   "/.search/<query>"
 *         - 2 : 结果软链接 "/.search/<query>/<rate>__<rank>"
 *         - 负值 : 路径非法或不属于 /.search（如 -ENOENT、-EINVAL、-ENOTDIR、-ENAMETOOLONG）
 * @note 1. 标准调用范式（需包含 <stdlib.h>）：
 *          char *query = NULL;
 *          double rate = 0;
 *          size_t rank = 0;
 *          int kind = vectorfs_parse_path(path, &query, &rate, &rank);
 *          if (kind < 0) return kind;
 *          // 根据 kind (0 / 1 / 2) 分支处理；切勿用 rank == 0 判断目录（第一名的 rank 就是 0）！
 *          free(query); // 后续所有返回路径（含报错分支）都必须 free(query)；free(NULL) 安全。
 *       2. 本函数仅解析路径字符串格式，不保证第 rank 名结果真实存在；kind == 2 时仍须通过
 *          vectorfs_cache_get 检查 rank < count 并用 vectorfs_result_name 核对名称。
 */
int vectorfs_parse_path(const char *path, char **query, double *rate, size_t *rank);

/******************************************************************************
* SECTION: 学生实现入口
*******************************************************************************/
/* 与普通文件系统使用相同的 FUSE 回调签名，函数说明见 src/vectorfs.c。 */
int vectorfs_getattr(const char *path, struct stat *st);
int vectorfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi);
int vectorfs_readlink(const char *path, char *buf, size_t size);

#endif
