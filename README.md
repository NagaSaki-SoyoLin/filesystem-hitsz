# user-land-filesystem

操作系统课程文件系统实验。`fs/template` 提供基础工程和随附的 `bonus/` 支持文件，`fs/simplefs` 提供阅读参考；普通 newfs 仍由学生完成。

## Lab5 附加实验

四项附加题均为选做：附加题一“扩展功能”可独立提交，也是后续题目的基础；附加题二 CheckpointFS、附加题三 VectorFS、附加题四“在自制 FS 上训练简单模型”只需先完成各自依赖的普通功能，不要求全部选做。两个文件接口实验建立在学生自己的 `newfs` 上，不提供独立的完整文件系统底座：

- **CheckpointFS**：先完成普通读写、删除、硬链接/链接计数，不要求 rename。学生直接复制到新 checkpoint；恢复先备份，再 unlink 工作路径、link 目标。切换非原子，link 失败保留备份但工作路径可能不存在。
- **VectorFS**：先完成普通文件和目录访问。学生实现与 FUSE2 完全一致的 `getattr/readdir/readlink`，填写 `struct stat`、调用目录 filler、返回软链接目标。

模型训练依赖自己的普通读写、持久化和训练所需的大文件支持，不要求先完成 CheckpointFS 或 VectorFS。

## 从 clone 到生成工程

从远端获取课程包：

```sh
git clone https://gitee.com/ftutorials/user-land-filesystem.git
cd user-land-filesystem
```

教师在本地验收尚未发布的改造时，应先把待测提交放入临时分支或临时仓库；`git clone` 只复制已提交内容，不会带走原工作树的未提交修改。

进入仓库根目录后运行一次环境配置和工程生成：

```sh
./setenv.sh
```

脚本会检查并安装 `make`、`cmake`、FUSE2 开发包，编译用户态 ddriver，并询问工作目录和项目名称。以下以二者均为 `newfs` 为例。首次运行后重新打开终端，或执行 `source ~/.bashrc`，使 `ddriver` 命令进入当前 shell。

随后构建生成工程：

```sh
cmake -S fs/newfs -B fs/newfs/build -DCMAKE_BUILD_TYPE=Debug
cmake --build fs/newfs/build
ddriver -v
test -f fs/newfs/bonus/include/checkpointfs.h
test -x fs/newfs/build/newfs
```

该流程从 `fs/template` 生成 `fs/newfs`，同时复制 `fs/template/bonus` 到 `fs/newfs/bonus`。附加题头文件从一开始就在 `fs/newfs/bonus/include/{bonus,checkpointfs,vectorfs}.h`，对应学生源码在 `bonus/src/`，工具与测试在 `bonus/tools/`、`bonus/tests/`。生成工程的 CMake 和主程序 FUSE 路由已经接好，无需再次生成、另行安装或手动接线；继续构建同一个 newfs 即可。文件与责任边界见 [附加题说明](fs/template/bonus/README.md)。

先实现自己的普通文件系统以及所选附加题依赖的扩展，再直接编辑 `fs/newfs/bonus/src/checkpointfs.c`、`vectorfs.c` 中的五个学生 TODO（两个检查点函数、三个标准 FUSE 回调）。这些入口初始返回 `-ENOSYS`；没有存储后端 ops 表、教师字节拷贝函数或独立 newfs 实现。路径/控制传输和向量数学工具不代替学生完成文件语义与匹配排序。

公开支持文件位于 `fs/template/bonus`；完整参考实现、构建对象和磁盘不在公开包中。测试使用明确指定的新建专用设备，不清空已有个人磁盘。

本地工作副本基于上游 `main` 的 `ac63332ede0add6e26416e4262f7fb60038a5604`；新增改造尚未发布到远端。
