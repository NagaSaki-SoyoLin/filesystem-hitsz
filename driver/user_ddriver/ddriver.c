#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "string.h"
#include <linux/fs.h>
#include "ddriver_ctl.h"
#include "stdio.h"
#include "errno.h"
#include <pwd.h>
#include <time.h>

extern int errno;

#define USER_INFO     "INFO: "
#define USER_ALERT    "WARNING: "

#define USER_PANIC    "PANIC: "
/******************************************************************************
* SECTION: Macro definitions
*******************************************************************************/   
#define DEVICE_NAME   "ddriver"
#define DEVICE_LOG    "ddriver_log"

#define user_info(fmt, ...)\
	do {\
		printf(USER_INFO DEVICE_NAME " " fmt "\n", ##__VA_ARGS__);\
        fprintf(debugf, USER_PANIC  " " fmt "\n", ##__VA_ARGS__);\
	} while(0)\

#define user_alert(fmt, ...)\
	do {\
		printf(USER_ALERT DEVICE_NAME " " fmt "\n", ##__VA_ARGS__);\
        fprintf(debugf, USER_PANIC  " " fmt "\n", ##__VA_ARGS__);\
	} while(0)\

#define user_panic(fmt, ...)\
    do {\
        printf(USER_PANIC  " " fmt "\n", ##__VA_ARGS__);\
    } while (0)\

#define DRIVER_AUTHOR   "Deadpool <deadpoolmine@qq.com>"
#define DRIVER_DESC     "A Fake disk driver in user space"
#define DRIVER_VERSION  "0.1.0"

#define CONFIG_DISK_SZ  (4 * 1024 * 1024)
#define CONFIG_BLOCK_SZ (512)
/******************************************************************************
* SECTION: Macro Functions 
*******************************************************************************/
#define IGNORE_ARG(arg)         ((void)arg)
#define IS_ADDR_ALIGN(addr)     (addr % CONFIG_BLOCK_SZ == 0)
#define ADDR_ROUND_UP(addr)     ((addr / CONFIG_BLOCK_SZ) * CONFIG_BLOCK_SZ)

#define INC_READCNT(disk)       (disk.read_cnt++)
#define INC_WRITECNT(disk)      (disk.write_cnt++)
#define INC_SEEKCNT(disk)       (disk.seek_cnt++)

#define RW_DELAY(disk, rw_ops)  (usleep(disk.rw_ops##_lat * 1000))
/******************************************************************************
* SECTION: Type definitions
*******************************************************************************/
struct ddriver
{
    int  ddriver_fd;                                 /* Disk ddriver_fd */
    int  read_cnt;
    int  write_cnt;
    int  seek_cnt;
    int  read_lat;
    int  write_lat;
    int  seek_lat;
    int  track_num;
    int  major_num;
    int  layout_size;
    int  iounit_size;
};
/******************************************************************************
* SECTION: Global Variable
*******************************************************************************/
/* reference: https://en.wikipedia.org/wiki/Hard_disk_drive_performance_characteristics */
struct ddriver disk = {
    .read_cnt    = 0,
    .write_cnt   = 0,
    .seek_cnt    = 0,
    .read_lat    = 2,       /* 2ms */       
    .write_lat   = 1,       /* 1ms */
    .seek_lat    = 4,       /* 4.17ms per 360 degree */
    .major_num   = 0,
    .track_num   = 100,
    .layout_size = CONFIG_DISK_SZ,
    .iounit_size = CONFIG_BLOCK_SZ
};

FILE *debugf = NULL;
/******************************************************************************
* SECTION: Helper Functions
*******************************************************************************/
int check_valid(size_t size) {
    if (size != CONFIG_BLOCK_SZ){
        user_alert("io size %ld should align to %d", size, CONFIG_BLOCK_SZ);
        return -EIO;
    }
    return 0;
}

int emulate_rotate(int fd, off_t start, off_t end) {
    int bytes_per_track = disk.layout_size / disk.track_num;
    int lat_per_track = disk.seek_lat;
    int distance = abs(end - start) % bytes_per_track; 
    
    if (distance == 0) {
        return 0;
    }

    usleep(distance * lat_per_track / bytes_per_track * 1000);
    return 0;
}
/******************************************************************************
* SECTION: Global Function Implementation
*******************************************************************************/
/**
 * @brief 打开驱动
 * 
 * @return int 文件描述符
 */
int ddriver_open(char *path) {
    char default_path[4096];
    char log_path[4096];
    struct stat st;
    if (!path || !*path) {
        struct passwd *pw = getpwuid(getuid());
        if (!pw) return -ENOENT;
        int n = snprintf(default_path, sizeof(default_path), "%s/" DEVICE_NAME, pw->pw_dir);
        if (n < 0 || (size_t)n >= sizeof(default_path)) return -ENAMETOOLONG;
        path = default_path;
    }
    int n = snprintf(log_path, sizeof(log_path), "%s.log", path);
    if (n < 0 || (size_t)n >= sizeof(log_path)) return -ENAMETOOLONG;
    int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0) return -errno;
    if (fstat(fd, &st) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    if (!S_ISREG(st.st_mode) || (st.st_size && st.st_size != CONFIG_DISK_SZ)) {
        close(fd);
        return -EINVAL;
    }
    if (!st.st_size) {
        int error = posix_fallocate(fd, 0, CONFIG_DISK_SZ);
        if (error) { close(fd); return -error; }
    }
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (log_fd < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    debugf = fdopen(log_fd, "w");
    if (!debugf) {
        int error = errno;
        close(log_fd);
        close(fd);
        return -error;
    }
    return fd;
}
/**
 * @brief 关闭驱动
 * 
 * @param fd 
 * @return int 
 */
int ddriver_close(int fd) {
    int ret = close(fd) < 0 ? -errno : 0;
    if (debugf && fclose(debugf) != 0 && !ret) ret = -errno;
    debugf = NULL;
    return ret;
}
/**
 * @brief 磁盘头SEEK
 * 
 * @param fd 
 * @param offset 
 * @param whence 
 * @return int 
 */
int ddriver_seek(int fd, off_t offset, int whence){
    int ret = 0;
    int cur = 0;

    if (!IS_ADDR_ALIGN(offset)) {
        user_alert("offset %ld must be aligned to block size %d", 
                      offset, CONFIG_BLOCK_SZ);
        return -EINVAL;
    }

    INC_SEEKCNT(disk);
    cur = lseek(fd, 0, SEEK_CUR);
    ret = lseek(fd, offset, whence);
    if (ret < 0) {
        user_panic("seek error: %s", strerror(errno));
        return ret;
    }
    emulate_rotate(fd, cur, ret);
    return ret;
}
/**
 * @brief 磁盘写入，写入大小可通过IOCTL查询
 * 
 * @param fd 
 * @param buf 
 * @param size 
 * @return int 
 */
int ddriver_write(int fd, char *buf, size_t size){
    int res = check_valid(size);
    if(res < 0)
        return res;
        
    RW_DELAY(disk, write);
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, buf + done, size - done);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        if (!n) return -EIO;
        done += (size_t)n;
    }
    INC_WRITECNT(disk);
    return (int)done;
}
/**
 * @brief 
 * 
 * @param fd 
 * @param buf 
 * @param size 
 * @return int 
 */
int ddriver_read(int fd, char *buf, size_t size){
    int res = check_valid(size);
    if(res < 0)
        return res;

    RW_DELAY(disk, read);
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, buf + done, size - done);
        if (n < 0) { if (errno == EINTR) continue; return -errno; }
        if (!n) return -EIO;
        done += (size_t)n;
    }
    INC_READCNT(disk);
    return (int)done;
}
/**
 * @brief 
 * 
 * @param fd 
 * @param cmd 
 * @param arg 
 * @return int 
 */
int ddriver_ioctl(int fd, unsigned long cmd, void *arg){
    struct ddriver_state state;
    switch (cmd)
    {
    case IOC_REQ_DEVICE_SIZE:                         /* Device Size */
        memcpy(arg, &disk.layout_size, sizeof(int));
        break;
    case IOC_REQ_DEVICE_STATE:                        /* Device State */
        state.read_cnt = disk.read_cnt;
        state.write_cnt = disk.write_cnt;
        state.seek_cnt = disk.seek_cnt;
        memcpy(arg, &state, sizeof(struct ddriver_state));
        break;
    case IOC_REQ_DEVICE_RESET:                        /* Reset Device */
        lseek(fd, 0, SEEK_SET);
        char buf[4096] = {'\0'};
        for (size_t i = 0; i < CONFIG_DISK_SZ; i += 4096)
        {
            write(fd, buf, 4096);
        }
        lseek(fd, 0, SEEK_SET);
        disk.read_cnt = 0;
        disk.write_cnt = 0;
        disk.seek_cnt = 0;
        break;
    case IOC_REQ_DEVICE_IO_SZ:
        memcpy(arg, &disk.iounit_size, sizeof(int));
        break;
    default:
        break;
    }
    return 0;
}