#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>

#define MAX_NAME_LEN    128     
#define NEWFS_PER_FILE_BLKS 6 /* 每个文件最多 6 个直接块 */

typedef enum newfs_file_type {
    NEWFS_REG_FILE, /* 普通文件 */
    NEWFS_DIR /* 目录 */
} NEWFS_FILE_TYPE;

struct custom_options {
	const char*        device;
};


/* ========== 磁盘结构：只存必须落盘的字段 ========== */

struct newfs_super_d {
    uint32_t magic;
    uint32_t blk_size;      uint32_t blk_nums;

    uint32_t ino_map_ofs;   uint32_t ino_map_blks;
    uint32_t blk_map_ofs;   uint32_t blk_map_blks;
    uint32_t ino_ofs;       uint32_t ino_blks;
    uint32_t data_ofs;      uint32_t data_blks;

    uint32_t ino_max;
    uint32_t root_ino;
};

struct newfs_inode_d {
    uint32_t        ino;
    uint32_t        size;
    uint32_t        dir_cnt;
    NEWFS_FILE_TYPE ftype;
    int32_t         block_pointer[NEWFS_PER_FILE_BLKS];
};

struct newfs_dentry_d {
    char            name[MAX_NAME_LEN];
    NEWFS_FILE_TYPE ftype;
    uint32_t        ino;
};


/* ========== 内存结构：运行时方便用 ========== */
struct newfs_dentry;
struct newfs_inode;

struct newfs_super {
    uint32_t magic;
    int      fd;
    /* TODO: Define yourself */
    int      sz_io;         /* 512  */
    int      sz_disk;       /* 4MB  */
    int      blks_sz;       /* 1024 */
    int      blks_nums;     /* 4096 */

    int      ino_map_ofs;   int ino_map_blks;
    int      blk_map_ofs;   int blk_map_blks;
    int      ino_ofs;       int ino_blks;
    int      data_ofs;      int data_blks;
    int      ino_max;
    int      root_ino;

    uint8_t *map_inode;     /* inode 位图的内存副本 */
    uint8_t *map_data;      /* 数据块位图的内存副本 */

    struct newfs_dentry *root_dentry;
    int      is_mounted;
};

struct newfs_inode {
    uint32_t ino;
    /* TODO: Define yourself */
    uint32_t size;
    uint32_t dir_cnt;
    int32_t block_pointer[NEWFS_PER_FILE_BLKS];

    struct newfs_dentry *dentry;    /* 指向“自己”的目录项 */
    struct newfs_dentry *dentrys;   /* 第一个子项（目录才有） */
};

struct newfs_dentry {
    char     name[MAX_NAME_LEN];
    uint32_t ino;
    /* TODO: Define yourself */
    NEWFS_FILE_TYPE ftype;

    struct newfs_dentry *parent;     /* 父目录项 */
    struct newfs_dentry *brother;    /* 下一个兄弟 */
    struct newfs_inode  *inode;      /* 指向 inode */
};

#endif /* _TYPES_H_ */