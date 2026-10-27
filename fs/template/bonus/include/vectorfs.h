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

/* Read-only document data, sorted by relative_path; free after each callback. */
struct vectorfs_document {
    const char *name;
    const char *relative_path;
    float embedding[VECTORFS_DIMENSION];
};

/* Teacher tools: no query-specific ranking is performed here. */
int vectorfs_embed(const char *text, size_t length,
                   float out[VECTORFS_DIMENSION]);
double vectorfs_similarity(const float a[VECTORFS_DIMENSION],
                           const float b[VECTORFS_DIMENSION]);
int vectorfs_load_documents(struct vectorfs_document **out, size_t *count);
void vectorfs_documents_free(struct vectorfs_document *documents, size_t count);
/* Split the full virtual path before decoding the query. Return 0 for
 * /.search, 1 for a query, or negative errno. result is borrowed from path;
 * NULL denotes a directory. query needs VECTORFS_QUERY_MAX bytes. */
int vectorfs_parse_path(const char *path, char query[VECTORFS_QUERY_MAX],
                        const char **result);

/* Three student callbacks: the same FUSE signatures as ordinary newfs. */
int vectorfs_getattr(const char *path, struct stat *st);
int vectorfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi);
int vectorfs_readlink(const char *path, char *buf, size_t size);

#endif
