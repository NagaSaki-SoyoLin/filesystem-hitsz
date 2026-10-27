#ifndef LAB5_BONUS_H
#define LAB5_BONUS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>
#include <stddef.h>

/* Evaluate once and return negative errno. Do not bypass resource cleanup. */
#define CHECK(expr) do { \
    int check_status = (expr); \
    if (check_status < 0) return check_status; \
} while (0)

/* Teacher wiring: wrap the existing standard operations table. The original
 * newfs_* functions remain ordinary filesystem operations for student reuse. */
int bonus_register(struct fuse_operations *operations);

/* Path text only: these helpers never create, copy, link or replace files. */
int bonus_normalize_path(const char *path, char *out, size_t capacity);
int bonus_percent_decode(const char *source, char *out, size_t capacity);

#endif
