# user-land-filesystem

操作系统课程用户态文件系统（FUSE）实验仓库。

- `driver/`：虚拟磁盘驱动 `ddriver`；
- `fs/demo/`：必做任务一（过渡小实验）；
- `fs/simplefs/`：供阅读参考的示例文件系统（固定块分配）；
- `fs/template/`：必做任务二及选做附加题的工程生成模板（运行 `./setenv.sh` 后生成学生工作目录，如 `fs/newfs/`）。

---

## 1. 快速开始：从克隆仓库到生成工程

获取仓库并进入根目录：

```sh
git clone https://gitee.com/ftutorials/user-land-filesystem.git
cd user-land-filesystem
```

运行环境配置与工程生成脚本（只需运行一次）：

```sh
./setenv.sh
```

脚本会安装必要的编译依赖与 `ddriver` 驱动，并提示你输入**工作目录名称**与**项目名称**（均不能为空；下文以两者皆填 `newfs` 为例）。首次运行后请重新打开终端或执行 `source ~/.bashrc` 使 `ddriver` 命令生效。

生成后可直接编译 `fs/newfs` 工程：

```sh
cmake -S fs/newfs -B fs/newfs/build -DCMAKE_BUILD_TYPE=Debug
cmake --build fs/newfs/build -j"$(nproc)"
```

---

## 2. 选做附加题概览

在完成必做任务一、任务二的基础上，实验提供四项选做附加题（详见实验指导书 `part7.md` 与 [fs/template/bonus/README.md](fs/template/bonus/README.md)）：

1. **附加题一：扩展功能**——在自己的 `newfs` 中实现普通文件读写（`read`/`write`/`truncate`）、删除（`unlink`/`rmdir`）、移动（`rename`）、硬链接（`link`）或大文件扩展。本题可独立提交，也是后续附加题的基础。
2. **附加题二：CheckpointFS**——复用你自己的普通文件读写、目录创建、删除与硬链接能力，在 `fs/newfs/src/checkpointfs.c` 中完成 4 个函数（`copy_file`、`checkpointfs_create`、`checkpointfs_copy`、`checkpointfs_restore`），实现将工作文件保存为独立检查点，以及“先将历史检查点独立复制为新检查点、再将工作路径硬链接到新检查点”的版本恢复机制。
3. **附加题三：VectorFS**——复用你自己的普通文件读写与目录遍历能力，在 `fs/newfs/src/vectorfs.c` 中完成 4 个函数（`vectorfs_search` 与标准 FUSE2 回调 `vectorfs_getattr`、`vectorfs_readdir`、`vectorfs_readlink`），将 `/.search/<query>` 呈现为动态查询目录，将前 10 名匹配结果呈现为指向 `../../documents/<相对路径>` 的虚拟软链接。
4. **附加题四：在自制 FS 上训练简单模型**——切换到 `model` 分支，在支持普通文件读写、持久化和 `1MB+` 单文件容量的基础上运行 PyTorch 训练负载。

### 附加题目录结构与自动测试

`./setenv.sh` 生成 `fs/newfs` 时，已将附加题学生源码生成到 `fs/newfs/src/{checkpointfs,vectorfs}.c`，将教师辅助代码、头文件、CLI 工具与测试脚本复制到 `fs/newfs/bonus/`，并预先接好 CMake 与 `main()` 中的 `bonus_register(&operations)` 路由分发。

当你完成所选附加题及其依赖的普通文件功能后，可在仓库根目录直接运行自动验收脚本：

```sh
# 单独测试 CheckpointFS
bash fs/newfs/tests/test.sh checkpoint

# 单独测试 VectorFS
bash fs/newfs/tests/test.sh vector

# 同时测试普通扩展 + CheckpointFS + VectorFS
bash fs/newfs/tests/test.sh all
```

（在 `fs/newfs` 目录内也可执行 `bash tests/test.sh checkpoint|vector|all`；不带参数的 `bash tests/test.sh` 保持原基础实验交互测试流程。）

上述测试命令会自动编译当前工程，在 `fs/newfs/tests/.lab5-test-*` 下创建独占的临时虚拟磁盘与挂载点完成挂载、测试与重挂载持久化检验，并在结束后自动卸载清理临时文件，不会擦除你个人的 `$HOME/ddriver` 磁盘。
