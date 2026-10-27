#ifndef CHECKPOINTFS_H
#define CHECKPOINTFS_H

#ifndef FUSE_USE_VERSION
#define FUSE_USE_VERSION 26
#endif
#include <fuse.h>
#include <stddef.h>
#include <sys/stat.h>

#define CHECKPOINTFS_CONTROL_DIR "/.checkpoint"
#define CHECKPOINTFS_VIEW_DIR "/.checkpoints"
#define CHECKPOINTFS_STORAGE_DIR "/.checkpointfs-data"
#define CHECKPOINTFS_PATH_MAX 512
#define CHECKPOINTFS_NAME_MAX 127


/* The two student responsibilities. Strings are borrowed for this call only. */
int checkpointfs_create(const char *file, const char *id);
int checkpointfs_restore(const char *file, const char *backup_id,
                         const char *target_id);

/* Pure path encoding/validation: no filesystem access or directory creation. */
int checkpointfs_path(const char *file, const char *id, char *out,
                      size_t capacity);

/* Teacher FUSE adapters; the dispatcher owns namespace write protection. */
int checkpointfs_control_open(struct fuse_file_info *fi);
int checkpointfs_control_write(const char *buf, size_t size, off_t offset,
                               struct fuse_file_info *fi);
int checkpointfs_control_release(struct fuse_file_info *fi);
int checkpointfs_view_getattr(const char *path, struct stat *st);
int checkpointfs_view_readdir(const char *path, void *buf,
                              fuse_fill_dir_t filler, off_t offset);
int checkpointfs_view_read(const char *path, char *buf, size_t size,
                           off_t offset);

#endif
