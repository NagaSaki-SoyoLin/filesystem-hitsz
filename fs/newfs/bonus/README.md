# Lab5 附加题二（CheckpointFS）与附加题三（VectorFS）开发速查

本目录随 `./setenv.sh` 生成任务二工程时一并复制到 `fs/newfs/bonus/`，CMake 与 `main()` 中的 `bonus_register(&operations)` 路由已预先接好。

- **你的普通文件系统代码**：位于 `src/newfs.c` 和 `include/newfs.h`。
- **你的附加题填空代码（共 8 个 TODO）**：位于 `src/checkpointfs.c`（4 个函数）和 `src/vectorfs.c`（4 个函数），初始均返回 `-ENOSYS`。
- **教师提供的支持代码（无需修改）**：位于 `bonus/include/`（头文件声明）、`bonus/src/`（路由、路径工具、控制会话、查询缓存、文档加载与向量计算）、`bonus/tools/`（命令行工具）和 `bonus/tests/`（自动验收脚本）。

---

## 1. 动手前的普通文件系统自查清单

CheckpointFS 和 VectorFS 都直接建立在你自己实现的 `newfs` 普通文件操作之上（**不要在 `newfs_*` 普通函数内部再次判断或转发 `/.checkpoint*` 或 `/.search` 路径**）：

| 选做题目 | 必须先在 `src/newfs.c` 中实现并挂载到 `operations` 表的钩子 |
|---|---|
| **CheckpointFS** | `mkdir`、`mknod`、`getattr`、`readdir`、`read`、`write`、`truncate`、`unlink`、**`link`（硬链接，需在 `include/newfs.h` 声明并挂到 `operations.link`）**，以及卸载重挂载持久化 |
| **VectorFS** | `mkdir`、`mknod`、`getattr`、`readdir`、`read`、`write`、`truncate`、`unlink`、`rename`，以及卸载重挂载持久化（不需要先实现 `link` 或 CheckpointFS） |

> **关键提醒**：实现硬链接时，`newfs_link(source, target)` 应新增一个指向源 `inode` 的目录项并将 `nlink` 加 1（不复制数据块）；`newfs_unlink(path)` 删除目录项并将 `nlink` 减 1，**只有当 `nlink == 0` 时才真正回收 `inode` 和数据块**。

---

## 2. 附加题二：CheckpointFS 速查（`src/checkpointfs.c`）

### 2.1 教师已提供的工具函数
- `int checkpointfs_path(const char *file, const char *id, char *out, size_t capacity)`（声明于 `bonus/include/checkpointfs.h`）：
  - 输入工作文件绝对路径 `file`（如 `"/models/model.bin"`）和检查点名 `id`（如 `"v0"`），在 `out` 中生成内部存储路径 `"/.checkpointfs-data/models__model.bin/v0"`。
  - **注意**：它只做字符串校验与编码，不查磁盘、不建目录；第一个参数永远传工作文件路径 `source`。
- `static void parent_path(const char *path, char *parent)`（已定义在 `src/checkpointfs.c` 顶部）：
  - 提取 `path` 的父目录路径（如得到集合目录 `"/.checkpointfs-data/models__model.bin"`）。
- 固定内部根目录 `CHECKPOINTFS_STORAGE_DIR`（`"/.checkpointfs-data"`）由教师控制层在调用你的函数前自动创建。

### 2.2 四个待实现函数的契约
1. **`static int copy_file(const char *source, const char *target)`**
   - 接收两个真实内部路径（假设 `target` 父目录已存在），确认 `source` 是已存在的普通文件，排他创建 `target`（不覆盖已有文件），将 `source` 的全部内容复制到 `target`（拥有独立 `inode` 和独立数据块，文件大小与内容完全一致，支持空文件、跨块文件及不足一块的尾部）。
   - 可循环调用自己的 `newfs_read` / `newfs_write`（传入零初始化的 `struct fuse_file_info fi = {0};`），也可在内部按块复制。若创建 `target` 后复制中途失败，调用 `newfs_unlink(target)` 清理本次新建文件再返回负错误码。
2. **`int checkpointfs_create(const char *source, const char *id)`**
   - 用 `checkpointfs_path(source, id, ...)` 和 `parent_path(...)` 算出目标检查点路径与集合目录路径；
   - 若集合目录尚不存在（`newfs_getattr` 返回 `-ENOENT`），调用 `newfs_mkdir` 创建它；
   - 调用 `copy_file(source, target_path)` 完成保存。
3. **`int checkpointfs_copy(const char *source, const char *source_id, const char *target_id)`**
   - 用同一个工作文件路径 `source` 分别调用 `checkpointfs_path` 算出 `source_id` 和 `target_id` 的内部路径，再调用 `copy_file`。（注：这是供 `restore` 调用的公开 C 函数，命令行工具不新增 `copy` 子命令。）
4. **`int checkpointfs_restore(const char *source, const char *hist_id, const char *new_id)`**
   - 先用 `newfs_getattr` 确认工作文件 `source` 存在且为普通文件；
   - 调用 `checkpointfs_copy(source, hist_id, new_id)` 将历史检查点独立复制为新检查点 `new_id`（即使当前工作文件已与 `hist_id` 共享 `inode` 也必须复制，不可跳过；若复制失败则直接返回错误，工作文件保持不变）；
   - 复制成功后，依次调用 `newfs_unlink(source)` 和 `newfs_link(new_ckpt_path, source)`，让工作路径与新检查点共享同一个 `inode`。

### 2.3 手动命令行工具用法

在 `fs/newfs` 目录下（设 `$M` 为已挂载目录）：
```sh
python3 bonus/tools/checkpointctl.py "$M" create /model.bin v1
python3 bonus/tools/checkpointctl.py "$M" restore /model.bin v1 resumed
ls -li "$M/.checkpoints/__model.bin"
```

---

## 3. 附加题三：VectorFS 速查（`src/vectorfs.c`）

### 3.1 教师已提供的工具函数（声明于 `bonus/include/vectorfs.h`）
- **路径解析**：`int kind = vectorfs_parse_path(path, &query, &rate, &rank);`
  - 返回 `0` 表示搜索根 `"/.search"`（`query == NULL`）；返回 `1` 表示查询目录 `"/.search/<query>"`；返回 `2` 表示结果项 `"/.search/<query>/<rate>__<rank>"`；负值为错误码。
  - `kind` 为 `1` 或 `2` 时，`query` 是动态分配的字符串，**在函数的所有后续返回分支中都必须调用 `free(query)`**（`free(NULL)` 同样安全）。
  - `rank` 从 `0` 开始（第一名为 `0`，对应后缀 `__00`），**必须用 `kind` 区分目录与结果项，不能用 `rank == 0` 判断目录**。
- **文档加载与向量计算**：
  - `vectorfs_load_documents(&docs, &doc_count)`：加载 `/documents` 下所有普通文档（含子目录）及其 8 维向量，`docs[i].relative_path` 为不带 `/documents/` 前缀的相对路径（如 `"os/a.txt"`）；用完后必须调用 `vectorfs_documents_free(docs, doc_count)` 释放。
  - `vectorfs_embed(query, strlen(query), query_vec)`：计算查询词的 8 维向量。
  - `vectorfs_similarity(query_vec, docs[i].embedding)`：计算余弦相似度（`double`）。
- **查询缓存与结果格式化**：
  - `vectorfs_cache_get(query, &results, &count)`：按 `query` 查找缓存；未命中时自动调用你的 `vectorfs_search` 填充缓存。返回的 `results`（`const struct vectorfs_result *`）归缓存所有，**只读借用，切勿修改或 `free(results)`**。
  - `vectorfs_result_name(&results[i], name, sizeof(name))`：生成 `"%.6f__%02zu"` 格式的目录项名（如 `"0.948683__01"`）。
  - `vectorfs_result_target(&results[i], target, sizeof(target))`：生成 `"../../documents/<source>"` 格式的软链接目标路径。

### 3.2 四个待实现函数的契约
1. **`int vectorfs_search(const char *query, struct vectorfs_result results[VECTORFS_TOP_K], size_t *count)`**
   - 加载文档，若无文档则令 `*count = 0`、释放文档列表并返回 `0`；
   - 计算 `query` 向量并与每个文档计算 `rate`，按 **`rate` 降序、同分 `source` 相对路径升序** 排序；
   - 取前 `min(文档数, VECTORFS_TOP_K)`（最多 10）项填入教师预分配的 `results` 数组（复制 `source` 字符串、填 `rate`、令 `results[i].rank = i`），设置 `*count` 为实际填入项数，释放文档列表后返回 `0`（不要 `malloc`/`free` `results` 数组）。
2. **`int vectorfs_getattr(const char *path, struct stat *st)`**
   - 清零 `st`，解析路径：`kind == 0` 或 `1` 时填目录属性（`S_IFDIR | 0555`，`st_nlink = 2`）；
   - `kind == 2` 时调用 `vectorfs_cache_get`，检查 `rank < count` 且 `results[rank]` 的名称/6 位小数分数与路径匹配（否则返回 `-ENOENT`），填软链接属性（`S_IFLNK | 0777`，`st_nlink = 1`，`st_size` 为 `vectorfs_result_target` 生成的目标路径字节长度）。
3. **`int vectorfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)`**
   - `offset < 0` 返回 `-EINVAL`；`kind == 2` 返回 `-ENOTDIR`；
   - `kind == 0` 只列 `"."` 和 `".."`；`kind == 1` 先列 `"."`、`".."`，再从 `vectorfs_cache_get` 拿到结果，依次用 `vectorfs_result_name` 生成名字并从 `offset` 起调用 `filler(buf, name, NULL, pos + 1)`（注意 `pos` 是含 `.` 和 `..` 的目录项总序号；`filler` 返回非零时立即停止并返回 `0`）。
4. **`int vectorfs_readlink(const char *path, char *buf, size_t size)`**
   - `!buf || size == 0` 或 `kind != 2` 返回 `-EINVAL`；
   - 从 `vectorfs_cache_get` 核对 `rank < count` 及结果名称匹配后，用 `vectorfs_result_target(&results[rank], ...)` 生成目标路径，最多复制 `size - 1` 字节到 `buf` 并补 `'\0'`，成功返回 `0`。

---

## 4. 一键编译与自动验收

在 `fs/newfs` 工程目录下按选做范围执行（也可在仓库根目录执行 `bash fs/newfs/tests/test.sh ...`）：

```sh
bash tests/test.sh checkpoint
bash tests/test.sh vector
bash tests/test.sh all
```

脚本会自动编译工程，在 `tests/.lab5-test-*` 下创建独占临时磁盘完成挂载、测试与重挂载验证，并在结束后自动清理，不会影响你的个人默认 `ddriver` 磁盘。
