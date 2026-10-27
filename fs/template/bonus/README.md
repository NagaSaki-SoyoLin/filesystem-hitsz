# 在自己的 newfs 上完成两个附加实验

本目录是**随基础模板提供的附加题代码，不是独立文件系统**。普通 `setenv.sh` 生成任务二工程时就会一并复制本目录，构建与 FUSE 路由已经连接。先完成必做题以及所选实验需要的普通文件功能，再直接编辑附加题 TODO；教师不会提供另一份磁盘、inode 或读写实现来绕过前面的实验。

## 前置功能与学生工作

| 实验 | 复用自己的前置功能 | 本题实现 |
|---|---|---|
| CheckpointFS | 创建文件/目录、getattr、read/write、truncate、unlink、硬链接与链接计数 | 自己编写复制循环；保存恢复前内容；用自己的 unlink/link 操作切换工作路径 |
| VectorFS | getattr、readdir、普通文件读取；用普通写入操作准备文档 | 标准 FUSE getattr/readdir/readlink，计算排名并将结果呈现为目录和软链接 |

CheckpointFS 所需的 `newfs_link` 必须是你自己的实现：新增目录项指向已有 inode，增加 nlink；unlink 减少 nlink，最后一个链接消失才释放内容。不是把硬链接当作另一份内容拷贝。若此前尚未实现硬链接，先在自己的 newfs 中完成；普通 rename 仍用于移动文件，但不是 CheckpointFS 的前提。VectorFS 不要求先完成 CheckpointFS 或硬链接功能。

## 接入已有工程

这里只说明生成工程中已有的连接，不需要另行安装或手动注册。按基础实验的正常流程，在仓库根目录运行一次 `./setenv.sh` 并填写工作目录与项目名称，即从 `fs/template` 生成学生工作目录。以下以工作目录和项目名均为 `newfs` 为例；已有该版模板生成的工程无需重复生成。

发放源码位于 `fs/template/bonus`，实际作业请编辑生成后的 `fs/newfs/bonus`。普通操作声明在生成工程的 `include/newfs.h`，附加接口和练习位置如下：

```text
fs/newfs/bonus/
├── include/
│   ├── bonus.h           # 公共辅助接口
│   ├── checkpointfs.h    # 两个检查点入口
│   └── vectorfs.h        # 三个标准 FUSE 回调及辅助接口
├── src/
│   ├── checkpointfs.c   # 两个学生 TODO
│   ├── vectorfs.c       # 三个学生 TODO
│   └── …（教师辅助源码）
├── tools/
├── tests/
└── README.md
```

生成工程的 CMake 已包含 `bonus/include` 和相关源码，主程序也已在 `fuse_main` 前连接虚拟路径分发，学生不需要补写 CMake include 或注册调用。保留并完成自己的 `newfs_*` 普通函数，**不要在这些函数内部再转发回 bonus**；附加题需要直接复用它们。分发器只包装传给 FUSE 的标准操作表，不引入新的存储后端接口。

先完成所选题目的普通前置功能，再直接编辑上述五个 TODO，继续构建同一个工程。以下构建命令在仓库根目录执行：

```sh
cmake -S fs/newfs -B fs/newfs/build -DCMAKE_BUILD_TYPE=Debug
cmake --build fs/newfs/build
```

编译沿用你原来的 FUSE2/ddriver 配置。扩展只有五个学生入口：`checkpointfs_create/restore`，以及以下三个标准 FUSE2 回调。初始 TODO 明确返回 `-ENOSYS`。

## CheckpointFS：操作自己实现的文件

`bonus/src/checkpointfs.c` 中的两个函数分别实现：

- `create FILE ID`：检查源文件及新名称，用自己的 mkdir/mknod 在新 ID 的最终路径排他创建独立 inode；直接循环 read/write，处理 offset、短写、负返回值和零进展。失败只 unlink 本次新建的 checkpoint，并保留原始错误。不使用临时文件或发布步骤，不保证隐藏半复制内容或崩溃恢复。
- `restore FILE BACKUP_ID TARGET_ID`：先检查工作文件、目标和未使用的备份 ID，再通过自己的 create 独立保存当前内容。备份失败停止；同 inode 时备份后直接结束，否则先 `newfs_unlink(work)` 再 `newfs_link(target,work)`。这是非原子切换，最终 link 失败返回错误并保留备份，工作路径可能不存在；不使用临时链接、rename 或回滚。

模板已预先计算真实路径并注释用途：create 的 `work/checkpoint/directory`，restore 的 `work/backup/target`。create 的 directory 是 checkpoint 集合目录；路径准备不创建文件或链接，最终 checkpoint 仍需用自己的 mknod 排他创建，不能覆盖已有 ID。

CheckpointFS 与 VectorFS 使用共享 `bonus.h` 中的 `CHECK(expr)`：do-while 宏只执行表达式一次，负返回值立即从当前函数返回。它适合无需清理即可返回的位置；新 checkpoint 创建后的读写错误应显式处理、清理后返回原始错误，不能跳过清理。Vector readdir 用 `CHECK(ret = vectorfs_parse_path(...))` 保留搜索根的 0 与查询路径的 1，文档和匹配数据仍须正确释放。合法参数仍到达 `-ENOSYS` TODO，非法路径或 ID 会在准备阶段返回相应错误。

没有 `prepare` 计划对象、copy 函数或专用 replacement API。路径编码工具只计算名称；控制协议和只读展示不替你创建目录、拷贝文件或调整 inode 关系。

恢复后工作路径与目标 checkpoint 共享 inode，因此继续写工作文件会改变该目标；恢复前备份是独立副本。这不是不可变快照。

工具位于生成工程的 `bonus/tools/`。下面的工具命令和 VectorFS 示例均在 `fs/newfs` 工作目录内执行：

```sh
python3 bonus/tools/checkpointctl.py "$M" create /model.bin v1
python3 bonus/tools/checkpointctl.py "$M" restore /model.bin before_restore v1
ls -li "$M/.checkpoints/__model.bin"
```

`M` 必须是已挂载目录。文件参数为挂载内路径，不是宿主根路径。

## VectorFS：同样的 FUSE API，不同的数据来源

```c
int vectorfs_getattr(const char *path, struct stat *st);
int vectorfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                     off_t offset, struct fuse_file_info *fi);
int vectorfs_readlink(const char *path, char *buf, size_t size);
```

与前面的 `newfs_getattr/newfs_readdir` 参数完全对齐：输入是完整路径，输出是标准 `struct stat`、filler 目录项和 readlink 缓冲区。没有先由另一层转换成自定义参数列表的逻辑回调，也不要求填写 `vectorfs_match` 输出结构。

普通 newfs 从磁盘 inode/目录项取得结果；这里根据 `/.search/<query>/` 动态计算结果。你自己执行查询嵌入、逐项比较与排序，可在自己文件内提取静态共用函数。教师提供的数学工具和只读文档列表不替你查询、排序或填写 FUSE 返回值。

- 查询目录：`S_IFDIR | 0555`。
- 结果项：`S_IFLNK | 0777`，大小为链接目标字节数。
- readdir：调用标准 filler 输出 `.`、`..` 及前 10 个结果；处理 offset 与 filler 已满。
- readlink：将 `../../documents/<相对路径>` 写入调用者缓冲区，不返回文档内容。

文档保存在自己的 `/documents` 普通目录中。每次需要结果都重新匹配，不要求缓存；同分按文档相对路径顺序排列。演示编码器不承诺自然语言语义质量。

```sh
Q=$(python3 bonus/tools/searchpath.py "$M" 'file system')
ls -l "$Q"
# 使用上一步实际列出的结果名观察 readlink 和 cat。
```

## 验收

在已有 `fs/newfs` 工程内：

```sh
python3 bonus/tests/run_bonus.py --binary build/newfs --suite checkpoint --device /tmp/lab5-checkpoint-new-device
python3 bonus/tests/run_bonus.py --binary build/newfs --suite vector --device /tmp/lab5-vector-new-device
```

`--suite base` 检查普通读写/硬链接/覆盖重命名等能力（rename 并非 CheckpointFS 前提），`--suite all` 验收两题。测试实际挂载并重挂载你的二进制，拒绝已有设备和默认个人 ddriver，只清理自己创建的设备。需支持显式 `--device` 专用路径；不要运行会擦除个人磁盘的旧脚本。

评分关注文件语义与标准回调行为，不要求额外的事务、故障注入、缓存或复杂路径协议。完整附加题参考答案保留在教师私有工程，不随公开模板发放；本目录也不提供普通 newfs 文件系统答案，普通能力由学生实现。
