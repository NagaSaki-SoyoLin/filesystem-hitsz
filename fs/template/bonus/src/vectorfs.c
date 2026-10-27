#include "vectorfs.h"
#include "bonus.h"
#include <errno.h>

/* These callbacks receive full paths, just like your ordinary newfs callbacks.
 * The fixed router selects them for /.search; ordinary paths still use newfs.
 * You may add static helpers and choose your own private result representation.
 * Shared matching work belongs here, not in a new public callback interface:
 * load documents, embed the decoded query, compare every document embedding,
 * keep scores paired with document indices, and sort by descending score then
 * ascending document index. The document list is already sorted by path.
 * Recompute on each callback; never cache results in fi->fh or global state.
 * Free the document list and your temporary results on every exit, including
 * errors. An empty document list is valid; no result array need be allocated. */

int vectorfs_getattr(const char *path, struct stat *st) {
    /* TODO(VECTORFS_GETATTR): Prepare query/result_name and parse with
     * CHECK(vectorfs_parse_path(path, query, &result_name));
     * A NULL result name means a directory. Zero st, then fill struct stat exactly
     * as in newfs_getattr. Root/query directories: S_IFDIR | 0555, nlink = 2.
     * For a result path, load documents and perform your shared matching work.
     * Find its name among the first min(count, VECTORFS_TOP_K) results:
     * snprintf(name, capacity, "%.6f__%02zu", score, one_based_rank).
     * Return -ENOENT if absent. Format the relative target with
     * snprintf(target, capacity, "../../documents/%s", relative_path).
     * Check snprintf's return value for failure/truncation. Set S_IFLNK | 0777,
     * nlink = 1, st_size = strlen(target), not document size.
     * This callback reports link metadata, never follows or creates the link. */
    (void)path;
    (void)st;
    return -ENOSYS;
}

int vectorfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi) {
    /* TODO(VECTORFS_READDIR): Keep the successful parser status:
     * int kind; CHECK(kind = vectorfs_parse_path(path, query, &result_name));
     * kind is 0 for /.search, 1 for a query; errors already returned by CHECK.
     * Reject result paths with -ENOTDIR and negative offsets with -EINVAL. /.search lists
     * only '.' and '..': queries are dynamic, not stored directory entries.
     * A query directory additionally exposes its current top ten result names
     * after loading documents and recomputing your matching/sorting work.
     * Use the normal newfs_readdir filler idiom; do not return a custom list:
     *   for (off_t i = offset; i < (off_t)entry_count; ++i) {
     *       ... select '.', '..', or a formatted result name ...
     *       if (filler(buf, name, NULL, i + 1)) break;
     *   }
     * A full filler is successful completion. Respect its stop request and
     * the supplied offset. fi is unused: there are no cached search handles. */
    (void)path;
    (void)buf;
    (void)filler;
    (void)offset;
    (void)fi;
    return -ENOSYS;
}

int vectorfs_readlink(const char *path, char *buf, size_t size) {
    /* TODO(VECTORFS_READLINK): Parse with
     * CHECK(vectorfs_parse_path(path, query, &result_name));
     * Directories return -EINVAL.
     * Load documents and recompute the same matching used by getattr/readdir.
     * Find the result among the top ten names or return -ENOENT. Format its
     * target using snprintf(target, capacity, "../../documents/%s", relative_path)
     * and check for failure/truncation. Copy at most size - 1 bytes to buf and
     * append '\0'. Reject a NULL buf or zero size with -EINVAL.
     * Return 0 on success, even when truncated. This is a standard FUSE
     * readlink reply, not a document index, byte count, or document contents. */
    (void)path;
    (void)buf;
    (void)size;
    return -ENOSYS;
}
