#include "newfs.h"

/* 从磁盘 offset 处读 size 字节到 out。
 * offset / size 不需要 512B 对齐，内部自动拆成 512B 块。
 * 成功返回 0，失败返回 -EIO。 */
int newfs_driver_read(int offset, uint8_t *out, int size) {
    uint8_t block[512];
    int align_down = (offset / 512) * 512;                 /* 起点向下对齐 */
    int align_up   = ((offset + size + 511) / 512) * 512;  /* 终点向上对齐 */
    int copied     = 0;                                     /* 已拷出多少字节 */

    for (int cur = align_down; cur < align_up; cur += 512) {
        /* 1. 把当前 512B 块整块读进临时 buffer */
        if (ddriver_seek(super.fd, cur, SEEK_SET) < 0) return -EIO;
        if (ddriver_read(super.fd, (char *)block, 512) < 0) return -EIO;

        /* 2. 算这块里哪一段属于 [offset, offset+size) */
        int start = (offset > cur) ? (offset - cur) : 0;
        int end   = (offset + size < cur + 512) ? (offset + size - cur) : 512;

        /* 3. 只拷出那一段 */
        if (end > start) {
            memcpy(out + copied, block + start, end - start);
            copied += end - start;
        }
    }
    return 0;
}

/* 往磁盘 offset 处写 size 字节。读-改-写：先读整块 → 改中间 → 整块写回。
 * 成功返回 0，失败返回 -EIO。 */
int newfs_driver_write(int offset, uint8_t *in, int size) {
    uint8_t block[512];
    int align_down = (offset / 512) * 512;
    int align_up   = ((offset + size + 511) / 512) * 512;
    int copied     = 0;

    for (int cur = align_down; cur < align_up; cur += 512) {
        /* 1. 先把整块读出来，保留不修改的部分 */
        if (ddriver_seek(super.fd, cur, SEEK_SET) < 0) return -EIO;
        if (ddriver_read(super.fd, (char *)block, 512) < 0) return -EIO;

        /* 2. 算这块里要覆盖的区间（和读函数完全一样） */
        int start = (offset > cur) ? (offset - cur) : 0;
        int end   = (offset + size < cur + 512) ? (offset + size - cur) : 512;

        /* 3. 用 in[] 覆盖 block 的对应部分 */
        if (end > start) {
            memcpy(block + start, in + copied, end - start);
            copied += end - start;
        }

        /* 4. 整块写回 */
        if (ddriver_seek(super.fd, cur, SEEK_SET) < 0) return -EIO;
        if (ddriver_write(super.fd, (char *)block, 512) < 0) return -EIO;
    }
    return 0;
}

/* 路径解析：从根开始逐级找
 * 返回值：找到的 dentry
 * is_find = 1 表示整条路径命中；0 表示没找到（此时返回最后那个有效父目录）
 * is_root = 1 表示 path 就是 "/" */
struct newfs_dentry* newfs_lookup(const char *path, int *is_find, int *is_root) {
    char path_cpy[1024];
    char *fname;
    struct newfs_dentry *cur = super.root_dentry;
    struct newfs_dentry *child;

    *is_find = 0;
    *is_root = 0;

    if (strcmp(path, "/") == 0) {
        if (cur->inode == NULL) cur->inode = newfs_read_inode(cur, cur->ino);
        if (cur->inode == NULL) return cur;
        *is_find = 1;
        *is_root = 1;
        return cur;
    }

    strncpy(path_cpy, path, sizeof(path_cpy) - 1);
    path_cpy[sizeof(path_cpy) - 1] = '\0';

    fname = strtok(path_cpy, "/");
    while (fname) {
        /* 关键：当前项的 inode 还没从磁盘读上来，就先读 */
        if (cur->inode == NULL && cur->ino != 0xFFFFFFFF) {
            cur->inode = newfs_read_inode(cur, cur->ino);
        }
        if (cur->inode == NULL || cur->ftype != NEWFS_DIR) {
            *is_find = 0;
            return cur;
        }

        child = cur->inode->dentrys;
        while (child && strcmp(child->name, fname) != 0) child = child->brother;

        if (child == NULL) {
            *is_find = 0;
            return cur;
        }

        cur   = child;
        fname = strtok(NULL, "/");
    }

    /* 最后一级的 inode 也要保证已加载 */
    if (cur->inode == NULL && cur->ino != 0xFFFFFFFF) {
        cur->inode = newfs_read_inode(cur, cur->ino);
    }
    if (cur->inode == NULL) {
        *is_find = 0;
        return cur;
    }

    *is_find = 1;
    return cur;
}

/* 取路径最后一级的名字。返回的指针指向 path 内部，不要 free */
char* newfs_get_fname(const char *path) {
    const char *p = strrchr(path, '/');
    if (p == NULL || p[1] == '\0') return NULL;
    return (char *)(p + 1);
}

/* 新建一个"孤儿"目录项：字段都填好，但还没挂到父目录下 */
struct newfs_dentry* newfs_new_dentry(const char *name, NEWFS_FILE_TYPE ftype) {
    struct newfs_dentry *dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    strncpy(dentry->name, name, MAX_NAME_LEN - 1);
    dentry->ftype   = ftype;
    dentry->ino     = 0xFFFFFFFF;   /* 还没分配 inode */
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;
}

/* 扫 inode 位图找第一个空闲位，置 1 后返回编号；位图满了返回 -1 */
int newfs_alloc_ino(void) {
    for (int i = 0; i < super.ino_max; i++) {
        int byte = i / 8, bit = i % 8;
        if ((super.map_inode[byte] & (1 << bit)) == 0) {
            super.map_inode[byte] |= (1 << bit);
            return i;
        }
    }
    return -1;
}

/* 分配一个 inode，并和 dentry 双向绑定（dentry->inode <-> inode->dentry） */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry *dentry) {
    int ino = newfs_alloc_ino();
    struct newfs_inode *inode;

    if (ino < 0) return NULL;

    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    memset(inode, 0, sizeof(struct newfs_inode));
    inode->ino     = ino;
    inode->size    = 0;
    inode->dir_cnt = 0;
    for (int i = 0; i < NEWFS_PER_FILE_BLKS; i++)
        inode->block_pointer[i] = -1;      /* -1 = 还没分配数据块 */
    inode->dentry  = dentry;
    inode->dentrys = NULL;

    dentry->inode = inode;
    dentry->ino   = ino;
    return inode;
}

/* 把 dentry 头插进父目录的子项链表中 */
int newfs_alloc_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry) {
    dentry->parent  = inode->dentry;
    dentry->brother = inode->dentrys;
    inode->dentrys  = dentry;
    inode->dir_cnt++;
    return 0;
}

/* 分配一个数据块，返回"数据区内的相对块号"；满了返回 -1 */
int newfs_alloc_blk(void) {
    for (int i = 0; i < super.data_blks; i++) {
        int byte = i / 8, bit = i % 8;
        if ((super.map_data[byte] & (1 << bit)) == 0) {
            super.map_data[byte] |= (1 << bit);
            return i;
        }
    }
    return -1;
}

/* 从磁盘读一个 inode；如果它是目录，顺便把子目录项也读出来挂成链表 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry *dentry, int ino) {
    struct newfs_inode_d inode_d;
    struct newfs_inode *inode;
    int ofs;

    ofs = super.ino_ofs * super.blks_sz + ino * (int)sizeof(struct newfs_inode_d);
    if (newfs_driver_read(ofs, (uint8_t *)&inode_d, sizeof(inode_d)) < 0) return NULL;
    if (inode_d.ino != (uint32_t)ino) return NULL;      /* 号码对不上说明磁盘坏了 */

    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    memset(inode, 0, sizeof(struct newfs_inode));
    inode->ino     = inode_d.ino;
    inode->size    = inode_d.size;
    inode->dir_cnt = 0;
    memcpy(inode->block_pointer, inode_d.block_pointer, sizeof(inode_d.block_pointer));
    inode->dentry  = dentry;
    inode->dentrys = NULL;

    dentry->inode = inode;
    dentry->ino   = inode_d.ino;
    dentry->ftype = inode_d.ftype;

    /* 目录：把数据块里的 dir_cnt 个 dentry_d 读出来，建成子项链表 */
    if (inode_d.ftype == NEWFS_DIR && inode_d.dir_cnt > 0 && inode->block_pointer[0] >= 0) {
        uint8_t blk[1024];

        if (newfs_driver_read((super.data_ofs + inode->block_pointer[0]) * super.blks_sz,
                              blk, super.blks_sz) < 0) return inode;

        for (uint32_t i = 0; i < inode_d.dir_cnt; i++) {
            struct newfs_dentry_d dentry_d;
            struct newfs_dentry *child;

            memcpy(&dentry_d, blk + i * sizeof(dentry_d), sizeof(dentry_d));
            dentry_d.name[MAX_NAME_LEN - 1] = '\0';     /* 防止磁盘上没 '\0' */

            child = newfs_new_dentry(dentry_d.name, dentry_d.ftype);
            child->ino     = dentry_d.ino;
            child->parent  = dentry;
            child->brother = inode->dentrys;            /* 头插进链表 */
            inode->dentrys = child;
            inode->dir_cnt++;
        }
    }
    return inode;
}

/* 把一个 inode 及其整棵子树写回磁盘 */
int newfs_sync_inode(struct newfs_inode *inode) {
    struct newfs_inode_d inode_d;
    struct newfs_dentry *child;
    int ofs;

    if (inode == NULL || inode->dentry == NULL) return -EINVAL;

    memset(&inode_d, 0, sizeof(inode_d));
    inode_d.ino     = inode->ino;
    inode_d.dir_cnt = inode->dir_cnt;
    inode_d.ftype   = inode->dentry->ftype;
    inode_d.size    = inode->size;

    if (inode_d.ftype == NEWFS_DIR) {
        /* 目录大小 = 子项个数 × dentry_d 大小 */
        inode_d.size = inode->dir_cnt * sizeof(struct newfs_dentry_d);
        inode->size  = inode_d.size;

        if (inode->dir_cnt > 0) {
            /* 第一次有子项时才分配数据块 */
            if (inode->block_pointer[0] < 0) {
                inode->block_pointer[0] = newfs_alloc_blk();
                if (inode->block_pointer[0] < 0) return -ENOSPC;
            }

            uint8_t blk[1024];
            int idx = 0;
            memset(blk, 0, sizeof(blk));

            for (child = inode->dentrys; child; child = child->brother) {
                struct newfs_dentry_d dentry_d;
                if ((idx + 1) * sizeof(dentry_d) > super.blks_sz) break;  /* 一块放满为止 */
                memset(&dentry_d, 0, sizeof(dentry_d));
                strncpy(dentry_d.name, child->name, MAX_NAME_LEN - 1);
                dentry_d.ftype = child->ftype;
                dentry_d.ino   = child->ino;
                memcpy(blk + idx * sizeof(dentry_d), &dentry_d, sizeof(dentry_d));
                idx++;
            }
            if (newfs_driver_write((super.data_ofs + inode->block_pointer[0]) * super.blks_sz,
                                   blk, super.blks_sz) < 0) return -EIO;
        }
    }

    /* 把数据块指针也更新进来后，写 inode_d */
    memcpy(inode_d.block_pointer, inode->block_pointer, sizeof(inode_d.block_pointer));
    ofs = super.ino_ofs * super.blks_sz + inode->ino * (int)sizeof(struct newfs_inode_d);
    if (newfs_driver_write(ofs, (uint8_t *)&inode_d, sizeof(inode_d)) < 0) return -EIO;

    /* 递归处理子项 */
    if (inode_d.ftype == NEWFS_DIR) {
        for (child = inode->dentrys; child; child = child->brother) {
            if (child->inode) newfs_sync_inode(child->inode);
        }
    }
    return 0;
}