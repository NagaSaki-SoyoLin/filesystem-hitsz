#include "checkpointfs.h"
#include "bonus.h"
#include "PROJECT_NAME.h"
#include <errno.h>
#include <string.h>

/* path is an absolute, validated path; parent has the same buffer capacity. */
static void parent_path(const char *path, char *parent) {
    strcpy(parent, path);
    char *slash = strrchr(parent, '/');
    if (slash == parent) slash[1] = '\0';
    else *slash = '\0';
}

int checkpointfs_create(const char *file, const char *id) {
    char work[CHECKPOINTFS_PATH_MAX];       /* 工作文件，如 /models/model.bin */
    char checkpoint[CHECKPOINTFS_PATH_MAX]; /* 新副本的最终路径，如 /.checkpointfs-data/models__model.bin/v1 */
    char directory[CHECKPOINTFS_PATH_MAX];  /* 新副本所在的 checkpoint 集合目录 */

    CHECK(bonus_normalize_path(file, work, sizeof(work)));
    CHECK(checkpointfs_path(work, id, checkpoint, sizeof(checkpoint)));
    parent_path(checkpoint, directory);

    /* Only real paths are prepared above; no filesystem entries are created. */
    /* TODO(CHECKPOINTFS_CREATE): Validate the source and unused checkpoint path
     * using your own PROJECT_NAME_getattr. Create the internal directories with
     * PROJECT_NAME_mkdir, then exclusively allocate an independent inode directly at
     * checkpoint with PROJECT_NAME_mknod. Copy bytes from work in a bounded read/write
     * offset loop, handling short I/O. Never replace an existing ID. On copy
     * failure, unlink only the checkpoint you created and return the original
     * error; do not use CHECK where returning would bypass this cleanup. */
    return -ENOSYS;
}

int checkpointfs_restore(const char *file, const char *backup_id,
                         const char *target_id) {
    char work[CHECKPOINTFS_PATH_MAX];       /* 要恢复的工作文件，如 /models/model.bin */
    char backup[CHECKPOINTFS_PATH_MAX];     /* 恢复前内容的新副本，BACKUP_ID 对应的内部路径 */
    char target[CHECKPOINTFS_PATH_MAX];     /* 要恢复到的已有 checkpoint，TARGET_ID 对应的内部路径 */

    CHECK(bonus_normalize_path(file, work, sizeof(work)));
    CHECK(checkpointfs_path(work, backup_id, backup, sizeof(backup)));
    CHECK(checkpointfs_path(work, target_id, target, sizeof(target)));

    /* Only real paths are prepared; backup must be created by your create function. */
    /* TODO(CHECKPOINTFS_RESTORE): Validate work, target and the unused backup
     * path before making changes. Save current bytes using your create function.
     * If work and target already share an inode, the backup is still required
     * but switching is unnecessary. Otherwise use your own PROJECT_NAME_unlink on work,
     * then PROJECT_NAME_link(target, work). This course operation is not atomic: if the
     * final link fails, return its error and keep the completed backup; work may
     * remain absent. */
    return -ENOSYS;
}
