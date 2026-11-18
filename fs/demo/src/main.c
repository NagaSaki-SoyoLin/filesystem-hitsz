/* main.c源码 */
#define _XOPEN_SOURCE 700

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "fuse.h"
#include "../include/ddriver.h"
#include <linux/fs.h>
#include "pwd.h"
#include "unistd.h"
#include "string.h"
#include <errno.h> // EIO

#define DEMO_DEFAULT_PERM        0777


/* 超级块 */
struct demo_super
{
    int     driver_fd;  /* 模拟磁盘的fd */

    int     sz_io;      /* 磁盘IO大小，单位B */
    int     sz_disk;    /* 磁盘容量大小，单位B */
    int     sz_blks;    /* 逻辑块大小，单位B */
};

/* 目录项 */
struct demo_dentry
{
    char    fname[128];
}; 

struct demo_super super;

#define DEVICE_NAME "ddriver"

/* 挂载文件系统 */
static void* demo_mount(struct fuse_conn_info * conn_info){
    // 打开驱动
    char device_path[128] = {0};
    sprintf(device_path, "%s/" DEVICE_NAME, getpwuid(getuid())->pw_dir);
    super.driver_fd = ddriver_open(device_path);

    printf("super.driver_fd: %d\n", super.driver_fd);

    int io_sz = 0, disk_sz = 0;
    ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_IO_SZ, &io_sz); /* 512 */
    ddriver_ioctl(super.driver_fd, IOC_REQ_DEVICE_SIZE, &disk_sz); /* 4194304 */

    /* 填充super信息 */
    super.sz_io = /* TODO */ io_sz;
    super.sz_disk = /* TODO */ disk_sz;
    super.sz_blks = /* TODO */ 2 * io_sz; /* 1024 */

    return 0;
}

/* 卸载文件系统 */
static void demo_umount(void* p){
    // 关闭驱动
    ddriver_close(super.driver_fd);
}

/* 遍历目录 */
static int demo_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi)
{
    // 此处任务一同学无需关注demo_readdir的传入参数，也不要使用到上述参数

    char filename[128] = {0}; // 待填充的

    /* 根据超级块的信息，从第500逻辑块读取一个dentry，ls将只固定显示这个文件名 */
    char block[512]; /* 一个 IO 块，ddriver_read 只能读 512B */
    struct demo_dentry dentry;

    /* TODO: 计算磁盘偏移off，并根据磁盘偏移off调用ddriver_seek移动磁盘头到磁盘偏移off处 */
    /* 注意用 sz_blks，不是 sz_io */
    off_t off = (off_t)500 * super.sz_blks;
    /* 偏移 512000 = 500*1024，本身就是 512 对齐的，直接 seek */
    if (ddriver_seek(super.driver_fd, off, SEEK_SET) < 0)
        return -EIO;

    /* TODO: 调用ddriver_read读出一个磁盘块到内存，512B */
    /* 一次必须读满一个 IO 块：512B（不是 1024） */
    if (ddriver_read(super.driver_fd, block, super.sz_io) < 0)
        return -EIO;

    /* TODO: 使用memcpy拷贝上述512B的前sizeof(demo_dentry)字节构建一个demo_dentry结构 */
    /* 前 sizeof(demo_dentry)=128 字节就是 dentry */
    memcpy(&dentry, block, sizeof(struct demo_dentry));

    /* TODO: 填充filename */
    /* 手动保证字符串有结尾，再拷贝出来 */
    dentry.fname[sizeof(dentry.fname) - 1] = '\0';
    memcpy(filename, dentry.fname, sizeof(filename) - 1);

    // 此处大家先不关注filler，已经帮同学写好，同学填充好filename即可
    return filler(buf, filename, NULL, 0);
}

/* 显示文件属性 */
static int demo_getattr(const char* path, struct stat *stbuf)
{
    if(strcmp(path, "/") == 0)
        stbuf->st_mode = DEMO_DEFAULT_PERM | S_IFDIR;            // 根目录是目录文件
    else
        stbuf->st_mode = /* TODO: 显示为普通文件 */
            DEMO_DEFAULT_PERM | S_IFREG;            // 该文件显示为普通文件
    return 0;
}

/* 根据任务1需求 只用实现前四个钩子函数即可完成ls操作 */
static struct fuse_operations ops = {
	.init = demo_mount,						          /* mount文件系统 */		
	.destroy = demo_umount,							  /* umount文件系统 */
	.getattr = demo_getattr,							  /* 获取文件属性 */
	.readdir = demo_readdir,							  /* 填充dentrys */
};

int main(int argc, char *argv[])
{
    int ret = 0;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    ret = fuse_main(args.argc, args.argv, &ops, NULL);
    fuse_opt_free_args(&args);
    return ret;
}
