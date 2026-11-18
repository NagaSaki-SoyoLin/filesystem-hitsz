#define _XOPEN_SOURCE 700

#include "newfs.h"
#include "bonus.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options newfs_options;			 /* 全局选项 */
struct newfs_super super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = newfs_init,						 /* mount文件系统 */		
	.destroy = newfs_destroy,				 /* umount文件系统 */
	.mkdir = newfs_mkdir,					 /* 建目录，mkdir */
	.getattr = newfs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = newfs_readdir,				 /* 填充dentrys */
	.mknod = newfs_mknod,					 /* 创建文件，touch相关 */
	.write = NULL,								  	 /* 写入文件 */
	.read = newfs_read,                         /* 附加题一：未完成时返回 ENOSYS */
	.utimens = newfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 *
 * @param conn_info 可忽略，一些建立连接相关的信息
 * @return void*
 */
void *newfs_init(struct fuse_conn_info *conn_info) {
  /* TODO: 在这里进行挂载 */

  /* 下面是一个控制设备的示例 */
  // super.fd = ddriver_open(newfs_options.device);

  struct newfs_super_d super_d;
  struct newfs_inode_d root_inode_d;
  struct newfs_inode *root_inode;
  struct newfs_dentry *root_dentry;

  /* ---------- 1. 打开虚拟磁盘 ---------- */
  super.fd = ddriver_open((char *)newfs_options.device);
  if (super.fd < 0) {
    printf("newfs: open device failed (%d)\n", super.fd);
    return NULL;
  }

  /* ---------- 2. 查容量，算逻辑块参数 ---------- */
  ddriver_ioctl(super.fd, IOC_REQ_DEVICE_IO_SZ, &super.sz_io);  /* 512 */
  ddriver_ioctl(super.fd, IOC_REQ_DEVICE_SIZE, &super.sz_disk); /* 4194304 */
  super.blks_sz = super.sz_io * 2;                              /* 1024 */
  super.blks_nums = super.sz_disk / super.blks_sz;              /* 4096 */

  /* ---------- 3. 读磁盘第 0 块，判断是否第一次挂载 ---------- */
  if (newfs_driver_read(0, (uint8_t *)&super_d, sizeof(super_d)) < 0) {
    printf("newfs: read super_d failed\n");
    return NULL;
  }
  if (super_d.magic != NEWFS_MAGIC) {

    /* ================= 4. 第一次挂载：初始化空文件系统 ================ */
    memset(&super_d, 0, sizeof(super_d));
    super_d.magic = NEWFS_MAGIC;
    super_d.blk_size = super.blks_sz;
    super_d.blk_nums = super.blks_nums; /* 五区布局：Super | Inode Map | DATA
                                           Map | INODE | DATA */
    super_d.ino_map_ofs = 1;
    super_d.ino_map_blks = 1;
    super_d.blk_map_ofs = super_d.ino_map_ofs + super_d.ino_map_blks;
    super_d.blk_map_blks = 1;
    super_d.ino_ofs = super_d.blk_map_ofs + super_d.blk_map_blks;
    super_d.ino_blks = 1;
    super_d.data_ofs = super_d.ino_ofs + super_d.ino_blks;
    super_d.data_blks = super.blks_nums - super_d.data_ofs;
    super_d.ino_max = super_d.ino_blks * super.blks_sz /
                      sizeof(struct newfs_inode_d); /* 1024/40 = 25 */
    super_d.root_ino =
        0; /* 位图全清 0，再标占：根 inode(0) + 根目录的数据块(数据区第 0 块) */
    super.map_inode = (uint8_t *)malloc(super_d.ino_map_blks * super.blks_sz);
    super.map_data = (uint8_t *)malloc(super_d.blk_map_blks * super.blks_sz);
    memset(super.map_inode, 0, super_d.ino_map_blks * super.blks_sz);
    memset(super.map_data, 0, super_d.blk_map_blks * super.blks_sz);
    super.map_inode[0] |= (1 << 0);
    super.map_data[0] |= (1 << 0); /* 根目录的 inode_d */
    memset(&root_inode_d, 0, sizeof(root_inode_d));
    root_inode_d.ino = super_d.root_ino;
    root_inode_d.size = 0;
    root_inode_d.dir_cnt = 0;
    root_inode_d.ftype = NEWFS_DIR;
    root_inode_d.block_pointer[0] = 0; /* 数据区第 0 块 */
    for (int i = 1; i < NEWFS_PER_FILE_BLKS; i++)
      root_inode_d.block_pointer[i] =
          -1; /* 落盘：根 inode_d、根目录数据块(清 0)、super_d、两张位图 */
    uint8_t zero_blk[1024] = {0};
    if (newfs_driver_write(super_d.ino_ofs * super.blks_sz,
                           (uint8_t *)&root_inode_d, sizeof(root_inode_d)) < 0)
      return NULL;
    if (newfs_driver_write(super_d.data_ofs * super.blks_sz, zero_blk,
                           super.blks_sz) < 0)
      return NULL;
    if (newfs_driver_write(0, (uint8_t *)&super_d, sizeof(super_d)) < 0)
      return NULL;
    if (newfs_driver_write(super_d.ino_map_ofs * super.blks_sz, super.map_inode,
                           super_d.ino_map_blks * super.blks_sz) < 0)
      return NULL;
    if (newfs_driver_write(super_d.blk_map_ofs * super.blks_sz, super.map_data,
                           super_d.blk_map_blks * super.blks_sz) < 0)
      return NULL;
    printf("newfs: first mount, filesystem initialized\n");
  } else {

  	/* ================= 5. 再次挂载：按 super_d 恢复 ================== */
    super.map_inode = (uint8_t *)malloc(super_d.ino_map_blks * super.blks_sz);
    super.map_data = (uint8_t *)malloc(super_d.blk_map_blks * super.blks_sz);
    newfs_driver_read(super_d.ino_map_ofs * super.blks_sz, super.map_inode,
                      super_d.ino_map_blks * super.blks_sz);
    newfs_driver_read(super_d.blk_map_ofs * super.blks_sz, super.map_data,
                      super_d.blk_map_blks * super.blks_sz);
    printf("newfs: remount, filesystem restored\n");
  }

  /* ---------- 6. 把布局信息搬进内存 super ---------- */
  super.magic = super_d.magic;
  super.blks_sz = super_d.blk_size;
  super.blks_nums = super_d.blk_nums;
  super.ino_map_ofs = super_d.ino_map_ofs;
  super.ino_map_blks = super_d.ino_map_blks;
  super.blk_map_ofs = super_d.blk_map_ofs;
  super.blk_map_blks = super_d.blk_map_blks;
  super.ino_ofs = super_d.ino_ofs;
  super.ino_blks = super_d.ino_blks;
  super.data_ofs = super_d.data_ofs;
  super.data_blks = super_d.data_blks;
  super.ino_max = super_d.ino_max;
  super.root_ino = super_d.root_ino;

  /* ---------- 7. 建根 dentry，从磁盘读根 inode（二次挂载时会连子项一起加载）---------- */
  root_dentry = newfs_new_dentry("/", NEWFS_DIR);
  root_dentry->ino = super.root_ino;

  root_inode = newfs_read_inode(root_dentry, super.root_ino);
  if (root_inode == NULL) {
    printf("newfs: read root inode failed\n");
    return NULL;
  }

  super.root_dentry = root_dentry;
  super.is_mounted = 1;

  printf("newfs: mounted. ino_map(%d,%d) blk_map(%d,%d) inode(%d,%d) "
         "data(%d,%d) ino_max=%d\n",
         super.ino_map_ofs, super.ino_map_blks, super.blk_map_ofs,
         super.blk_map_blks, super.ino_ofs, super.ino_blks, super.data_ofs,
         super.data_blks, super.ino_max);
  return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	
	// ddriver_close(super.fd);

	// return;

	struct newfs_super_d super_d;
	(void)p;

	if (!super.is_mounted) {
		if (super.fd > 0) { ddriver_close(super.fd); super.fd = -1; }
		return;
	}

	/* 1. 整棵目录树递归刷回（从根开始） */
	if (super.root_dentry && super.root_dentry->inode) {
		newfs_sync_inode(super.root_dentry->inode);
	}

	/* 2. 超级块写回 */
	memset(&super_d, 0, sizeof(super_d));
	super_d.magic        = NEWFS_MAGIC;
	super_d.blk_size     = super.blks_sz;
	super_d.blk_nums     = super.blks_nums;
	super_d.ino_map_ofs  = super.ino_map_ofs;
	super_d.ino_map_blks = super.ino_map_blks;
	super_d.blk_map_ofs  = super.blk_map_ofs;
	super_d.blk_map_blks = super.blk_map_blks;
	super_d.ino_ofs      = super.ino_ofs;
	super_d.ino_blks     = super.ino_blks;
	super_d.data_ofs     = super.data_ofs;
	super_d.data_blks    = super.data_blks;
	super_d.ino_max      = super.ino_max;
	super_d.root_ino     = super.root_ino;
	newfs_driver_write(0, (uint8_t *)&super_d, sizeof(super_d));

	/* 3. 两块位图写回 */
	newfs_driver_write(super.ino_map_ofs * super.blks_sz,
			   super.map_inode, super.ino_map_blks * super.blks_sz);
	newfs_driver_write(super.blk_map_ofs * super.blks_sz,
			   super.map_data, super.blk_map_blks * super.blks_sz);

	/* 4. 关设备 */
	ddriver_close(super.fd);
	super.fd = -1;
	super.is_mounted = 0;
	printf("newfs: unmounted, all synced\n");
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */

	int is_find = 0, is_root = 0;
	struct newfs_dentry *parent, *dentry;
	struct newfs_inode  *inode;
	char *fname;

	parent = newfs_lookup(path, &is_find, &is_root);
	if (is_find) return -EEXIST;                         /* 已经存在 */
	if (parent == NULL || parent->inode == NULL) return -ENOENT;
	if (parent->ftype != NEWFS_DIR) return -ENOTDIR;     /* 父项不是目录 */

	fname = newfs_get_fname(path);
	if (fname == NULL) return -EINVAL;

	dentry = newfs_new_dentry(fname, NEWFS_DIR);
	inode  = newfs_alloc_inode(dentry);
	if (inode == NULL) return -ENOSPC;

	newfs_alloc_dentry(parent->inode, dentry);

	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则返回对应错误号
 */
int newfs_getattr(const char* path, struct stat * newfs_stat) {
	/* TODO: 解析路径，获取Inode，填充newfs_stat，可参考/fs/simplefs/sfs.c的sfs_getattr()函数实现 */

	int is_find = 0, is_root = 0;
	struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);

	if (!is_find || dentry->inode == NULL) return -ENOENT;

	memset(newfs_stat, 0, sizeof(struct stat));

	if (dentry->ftype == NEWFS_DIR) {
		newfs_stat->st_mode  = S_IFDIR | NEWFS_DEFAULT_PERM;
		newfs_stat->st_size  = dentry->inode->dir_cnt * sizeof(struct newfs_dentry_d);
		newfs_stat->st_nlink = is_root ? 2 : 1;      /* 根目录约定 nlink=2 */
	} else {
		newfs_stat->st_mode  = S_IFREG | NEWFS_DEFAULT_PERM;
		newfs_stat->st_size  = dentry->inode->size;
		newfs_stat->st_nlink = 1;
	}

	newfs_stat->st_uid     = getuid();
	newfs_stat->st_gid     = getgid();
	newfs_stat->st_blksize = super.blks_sz;

	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */

	int is_find = 0, is_root = 0;
	struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry *child;
	int idx = 0;

	if (!is_find || dentry->inode == NULL) return -ENOENT;
	if (dentry->ftype != NEWFS_DIR)        return -ENOTDIR;

	child = dentry->inode->dentrys;          /* 第一个子项 */
	while (child) {
		if (idx >= offset) {
			/* 把名字交给 FUSE；off 传"下一个位置"；filler 返回非 0 = 缓冲区满了 */
			if (filler(buf, child->name, NULL, idx + 1) != 0) break;
		}
		idx++;
		child = child->brother;              /* 顺着兄弟链走 */
	}

	return 0;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则返回对应错误号
 */
int newfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */

	int is_find = 0, is_root = 0;
	struct newfs_dentry *parent, *dentry;
	struct newfs_inode  *inode;
	char *fname;

	parent = newfs_lookup(path, &is_find, &is_root);
	if (is_find) return -EEXIST;
	if (parent == NULL || parent->inode == NULL) return -ENOENT;
	if (parent->ftype != NEWFS_DIR) return -ENOTDIR;

	fname = newfs_get_fname(path);
	if (fname == NULL) return -EINVAL;

	dentry = newfs_new_dentry(fname, NEWFS_REG_FILE); /* 只有这里和 mkdir 不同 */
	inode  = newfs_alloc_inode(dentry);
	if (inode == NULL) return -ENOSPC;

	newfs_alloc_dentry(parent->inode, dentry);
	return 0;

	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则返回对应错误号
 */
int newfs_utimens(const char* path, const struct timespec tv[2]) {
	return 0;
}
/******************************************************************************
* SECTION: 选做函数实现
*******************************************************************************/
/**
 * @brief 写入文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int newfs_write(const char* path, const char* buf, size_t size, off_t offset,
		        struct fuse_file_info* fi) {
	/* 选做 */
	return size;
}

/**
 * @brief 读取文件
 * 
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int newfs_read(const char* path, char* buf, size_t size, off_t offset,
		       struct fuse_file_info* fi) {
	/* 附加题一：完成普通文件读取，供 FUSE 和附加题复用。 */
	return -ENOSYS;
}

/**
 * @brief 删除文件
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_unlink(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 删除目录
 * 
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 * 
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rmdir(const char* path) {
	/* 选做 */
	return 0;
}

/**
 * @brief 重命名文件 
 * 
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则返回对应错误号
 */
int newfs_rename(const char* from, const char* to) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_open(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 打开目录文件
 * 
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则返回对应错误号
 */
int newfs_opendir(const char* path, struct fuse_file_info* fi) {
	/* 选做 */
	return 0;
}

/**
 * @brief 改变文件大小
 * 
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则返回对应错误号
 */
int newfs_truncate(const char* path, off_t offset) {
	/* 选做 */
	return 0;
}


/**
 * @brief 访问文件，因为读写文件时需要查看权限
 * 
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission. 
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence. 
 * 
 * @return int 0成功，否则返回对应错误号
 */
int newfs_access(const char* path, int type) {
	/* 选做: 解析路径，判断是否存在 */
	return 0;
}	
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	char device_path[256] = {0};
	snprintf(device_path, sizeof(device_path), "%s/ddriver", getpwuid(getuid())->pw_dir);
	newfs_options.device = strdup(device_path); /* TODO: 这里填写你的ddriver设备路径 */

	if (fuse_opt_parse(&args, &newfs_options, option_spec, NULL) == -1)
		return -1;
	
	/* 教师已接好附加题分发；普通路径仍调用上面的学生回调。 */
	ret = bonus_register(&operations);
	if (ret < 0) {
		fprintf(stderr, "Cannot register bonus callbacks: %s\n", strerror(-ret));
		fuse_opt_free_args(&args);
		return 1;
	}

	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}