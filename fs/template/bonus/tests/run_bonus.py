#!/usr/bin/env python3
"""Exercise the student's newfs plus bonus callbacks on a new dedicated disk."""
import argparse
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile
import time

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
from checkpointctl import file_key, request
from searchpath import search_path


class Progress:
    def __init__(self):
        self.current = None
        self.passed = 0

    def start(self, label):
        self.finish()
        self.current = label
        print(f"[步骤 {self.passed + 1}] 开始：{label}", flush=True)

    def finish(self):
        if self.current is not None:
            self.passed += 1
            print(f"pass: 步骤 {self.passed} - {self.current}", flush=True)
            self.current = None

    def fail(self, error):
        label = self.current or '环境准备 / 参数检查'
        print(f"fail: 步骤 {self.passed + 1} - {label}\n原因：{error}", file=sys.stderr, flush=True)
        print(f"结果：已通过 {self.passed} 步，当前步骤失败，后续步骤未执行。", file=sys.stderr)


progress = Progress()


def require(value, message):
    if not value:
        raise AssertionError(message)


def base_scenario(root):
    progress.start('普通功能：准备文件并验证跨块读写')
    directory = root / 'ordinary'
    directory.mkdir()
    source = directory / 'source'
    data = bytes(range(256)) * 24
    source.write_bytes(data)
    require(source.read_bytes() == data, 'ordinary multi-block read/write')
    progress.start('普通功能：硬链接与覆盖重命名')
    alias = directory / 'alias'
    os.link(source, alias)
    require(source.stat().st_ino == alias.stat().st_ino, 'ordinary hardlink identity')
    require(source.stat().st_nlink == 2, 'ordinary hardlink count')
    source.rename(directory / 'renamed')
    require(alias.read_bytes() == data, 'rename preserves hardlink data')
    victim = directory / 'victim'
    victim.write_bytes(b'old target')
    os.replace(directory / 'renamed', victim)
    require(victim.stat().st_ino == alias.stat().st_ino, 'ordinary rename replaces an existing target')
    victim.unlink()
    require(alias.stat().st_nlink == 1, 'unlink decrements link count')
    print('PASS ordinary files: multi-block I/O, link, rename, unlink', flush=True)


def checkpoint_scenario(root):
    progress.start('CheckpointFS：准备普通工作文件')
    work = root / 'model.bin'
    view = root / '.checkpoints' / file_key('/model.bin')
    initial = bytes(range(256)) * 17 + b'copy-tail'
    work.write_bytes(initial)
    progress.start('CheckpointFS create：独立复制及尾块内容')
    request(root, 'create', '/model.bin', 'v1')
    v1 = view / 'v1'
    require(v1.read_bytes() == initial, 'create copies multiple buffers and the final partial chunk')
    require(v1.stat().st_ino != work.stat().st_ino, 'create allocates independent inode')
    progress.start('CheckpointFS create：拒绝重复 ID，历史内容不变')
    work.write_bytes(b'B')
    try:
        request(root, 'create', '/model.bin', 'v1')
    except FileExistsError:
        pass
    else:
        raise AssertionError('create must refuse an existing checkpoint ID')
    require(v1.read_bytes() == initial, 'create checkpoint remains independent')
    progress.start('CheckpointFS restore：复制历史到新检查点并切换 inode')
    request(root, 'restore', '/model.bin', 'v1', 'v2')
    v2 = view / 'v2'
    require(v2.read_bytes() == initial and work.read_bytes() == initial, 'restore copies history, not unsaved work')
    require(v2.stat().st_ino != v1.stat().st_ino, 'new checkpoint is independent of history')
    require(work.stat().st_ino == v2.stat().st_ino, 'working file shares new checkpoint inode')
    progress.start('CheckpointFS：共享写入新分支，历史保持独立')
    work.write_bytes(b'C')
    require(v1.read_bytes() == initial, 'working writes preserve historical checkpoint')
    require(v2.read_bytes() == b'C', 'working writes change only the new branch')
    progress.start('CheckpointFS restore：重复 ID / 历史不存在时不切换')
    for history, new, error in [('v1', 'v2', FileExistsError), ('missing', 'unused', FileNotFoundError)]:
        try:
            request(root, 'restore', '/model.bin', history, new)
        except error:
            pass
        else:
            raise AssertionError('restore must reject existing new ID or missing history')
        require(work.read_bytes() == b'C' and work.stat().st_ino == v2.stat().st_ino,
                'failed restore leaves work and link identity unchanged')
    progress.start('CheckpointFS：再次恢复及复制当前分支')
    request(root, 'restore', '/model.bin', 'v1', 'v3')
    require(work.read_bytes() == initial and v2.read_bytes() == b'C', 'restore again keeps previous branch')
    request(root, 'restore', '/model.bin', 'v3', 'v4')
    require(work.stat().st_ino != (view / 'v3').stat().st_ino, 'even active history is independently copied')
    work.write_bytes(b'D')
    require((view / 'v3').read_bytes() == initial and v1.read_bytes() == initial,
            'active-history restore isolates subsequent work writes')
    require(work.stat().st_nlink == 2, 'new branch has working and checkpoint links')
    progress.start('CheckpointFS：空文件检查点与恢复')
    empty = root / 'empty.bin'
    empty.touch()
    request(root, 'create', '/empty.bin', 'zero')
    request(root, 'restore', '/empty.bin', 'zero', 'branch')
    empty_view = root / '.checkpoints' / file_key('/empty.bin')
    empty.write_bytes(b'E')
    require((empty_view / 'zero').read_bytes() == b'', 'empty historical content remains isolated')
    subprocess.run(['ls', '-li', str(view)], check=True)
    subprocess.run(['cat', str(work)], check=True)
    print('\nPASS CheckpointFS: historical copy, mutable new branch, repeat restore, conflicts and empty files', flush=True)


def checkpoint_persistence(root):
    progress.start('CheckpointFS：重挂载后内容、历史和链接关系')
    work = root / 'model.bin'
    view = root / '.checkpoints' / file_key('/model.bin')
    require(work.read_bytes() == b'D', 'working content survives remount')
    initial = bytes(range(256)) * 17 + b'copy-tail'
    require((view / 'v1').read_bytes() == initial, 'original history survives unchanged')
    require((view / 'v2').read_bytes() == b'C', 'previous branch survives remount')
    require((view / 'v3').read_bytes() == initial, 'active history remains independent')
    require(work.stat().st_ino == (view / 'v4').stat().st_ino, 'new branch link identity persists')
    require(work.stat().st_ino != (view / 'v3').stat().st_ino, 'historical inode remains separate')
    require(work.stat().st_nlink == 2, 'new branch link count persists')
    print('PASS CheckpointFS remount: unchanged history, branch contents and link identity', flush=True)


def vector_scenario(root):
    progress.start('VectorFS：准备文档及子目录同名文件')
    documents = root / 'documents'
    documents.mkdir()
    (documents / 'os').mkdir()
    (documents / 'a.txt').write_text('aaaa', encoding='utf-8')
    (documents / 'os/a.txt').write_text('aaab', encoding='utf-8')
    (documents / 'b.txt').write_text('bbbb', encoding='utf-8')
    progress.start('VectorFS getattr：搜索根和查询目录')
    require(list((root / '.search').iterdir()) == [], 'search root has no stored query entries')
    query = search_path(root, 'a')
    require(query.is_dir(), 'getattr describes the query as a standard directory')
    progress.start('VectorFS readdir：分数与排序')
    names = [p.name for p in query.iterdir()]
    require(names == ['1.000000__00', '0.948683__01', '0.000000__02'], f'query a ranking: {names}')
    expected = ['a.txt', 'os/a.txt', 'b.txt']
    progress.start('VectorFS readlink/getattr：链接目标、长度和原文')
    for name, relative in zip(names, expected):
        result = query / name
        require(result.is_symlink(), 'search result must be a virtual symlink')
        require(os.readlink(result) == '../../documents/' + relative, 'score stays paired with document path')
        require(result.lstat().st_size == len(os.readlink(result).encode()), 'getattr reports symlink target byte length')
        require(result.read_bytes() == (documents / relative).read_bytes(), 'following result reads real document')
    subprocess.run(['ls', '-l', str(query)], check=True)
    subprocess.run(['readlink', str(query / names[0])], check=True)
    subprocess.run(['cat', str(query / names[0])], check=True)
    print(flush=True)
    progress.start('VectorFS：不同查询独立匹配')
    other = search_path(root, 'b') / '1.000000__00'
    require(os.readlink(other) == '../../documents/b.txt', 'second query independently matches current documents')
    progress.start('VectorFS：文档更新、同分路径顺序')
    (documents / 'a.txt').write_text('bbbb', encoding='utf-8')
    require(os.readlink(other) == '../../documents/a.txt', 'changed document is re-embedded; equal scores retain path order')
    progress.start('VectorFS：文档改名和删除后清理旧缓存')
    (documents / 'a.txt').rename(documents / 'z.txt')
    require(os.readlink(other) == '../../documents/b.txt', 'rename invalidates cached source paths and tie order')
    (documents / 'b.txt').unlink()
    require(os.readlink(other) == '../../documents/z.txt', 'deleted document is removed from cached results')
    (documents / 'z.txt').rename(documents / 'a.txt')
    (documents / 'b.txt').write_text('bbbb', encoding='utf-8')
    progress.start('VectorFS：仅展示前十项')
    for index in range(9):
        (documents / f'extra{index}.txt').write_text('bbbb', encoding='utf-8')
    top = list(search_path(root, 'b').iterdir())
    require(len(top) == 10, 'only top ten of twelve documents are exposed')
    require(top[-1].name == '1.000000__09', 'ranking names include stable zero-based rank')
    print('\nPASS VectorFS: query scores/order, nested document paths, symlinks, content, updates and top K', flush=True)


class Mounted:
    def __init__(self, binary, device, root, log):
        self.binary, self.device, self.root, self.log = binary, device, root, log
        self.process = None

    def start(self):
        self.process = subprocess.Popen([
            str(self.binary), '-f', '-s', '-o',
            'use_ino,attr_timeout=0,entry_timeout=0,negative_timeout=0',
            f'--device={self.device}', str(self.root),
        ], stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if os.path.ismount(self.root):
                return
            if self.process.poll() is not None:
                raise RuntimeError(f'filesystem exited before mount ({self.process.returncode})')
            time.sleep(0.05)
        raise RuntimeError('filesystem did not mount within 90 seconds')

    def stop(self):
        if os.path.ismount(self.root):
            subprocess.run(['fusermount', '-u', str(self.root)], check=True, timeout=30)
        if self.process is not None:
            try:
                self.process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=10)
            self.process = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, help='默认使用本工程 build 下的可执行文件')
    parser.add_argument('--suite', choices=('base', 'checkpoint', 'vector', 'all'), default='all')
    parser.add_argument('--device', type=Path, help='可选：尚不存在的专用磁盘；默认自动创建')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2]
    progress.start('环境检查：编译产物、FUSE 与卸载工具')
    if args.binary is None:
        cache = project / 'build/CMakeCache.txt'
        if not cache.is_file():
            raise RuntimeError('未找到构建结果，请运行 bash tests/test.sh checkpoint 或 vector')
        name = next((line.split('=', 1)[1] for line in cache.read_text().splitlines()
                     if line.startswith('CMAKE_PROJECT_NAME:STATIC=')), None)
        if not name:
            raise RuntimeError('CMakeCache 中缺少项目名称，请重新配置工程')
        args.binary = project / 'build' / name
    binary = args.binary.resolve(strict=True)
    if not os.access(binary, os.X_OK):
        raise RuntimeError(f'文件不可执行：{binary}')
    if not Path('/dev/fuse').exists() or not shutil.which('fusermount'):
        raise RuntimeError('需要可用的 /dev/fuse 和 fusermount；请检查 Docker 的 FUSE 权限')
    # Keep automatically created test artifacts inside the project, never use a personal disk.
    with tempfile.TemporaryDirectory(prefix='.lab5-test-', dir=project / 'tests') as temporary:
        device = args.device.absolute() if args.device else Path(temporary) / 'disk'
        default = Path(pwd.getpwuid(os.getuid()).pw_dir) / 'ddriver'
        if device.exists() or device.is_symlink() or device.resolve() == default.resolve():
            raise RuntimeError('测试设备必须尚不存在，不能使用个人默认 ddriver')
        if not device.parent.is_dir() or Path(str(device) + '.log').exists():
            raise RuntimeError('设备父目录须已存在，且不能已有同名日志')
        root = Path(temporary) / 'mnt'
        root.mkdir()
        logpath = Path(temporary) / 'fuse.log'
        fd = os.open(device, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
        os.close(fd)
        with logpath.open('w+') as log:
            mounted = Mounted(binary, device, root, log)
            try:
                progress.start('首次挂载专用测试文件系统')
                mounted.start()
                if args.suite in ('base', 'all'):
                    base_scenario(root)
                if args.suite in ('checkpoint', 'all'):
                    checkpoint_scenario(root)
                if args.suite in ('vector', 'all'):
                    vector_scenario(root)
                progress.start('正常卸载并使用同一磁盘重新挂载')
                mounted.stop()
                mounted.start()
                if args.suite in ('base', 'all'):
                    progress.start('普通文件重挂载持久化')
                    require((root / 'ordinary/alias').read_bytes() == bytes(range(256)) * 24, 'ordinary data persists')
                if args.suite in ('checkpoint', 'all'):
                    checkpoint_persistence(root)
                if args.suite in ('vector', 'all'):
                    progress.start('VectorFS：重挂载后重新查询与读取文档')
                    result = search_path(root, 'b') / '1.000000__00'
                    require(result.read_bytes() == b'bbbb', 'search follows persisted documents after remount')
                progress.start('正常卸载测试文件系统')
                mounted.stop()
                progress.finish()
            except (OSError, AssertionError, RuntimeError, subprocess.SubprocessError):
                log.seek(0)
                details = log.read()
                if details:
                    print('--- 文件系统运行日志 ---\n' + details, file=sys.stderr)
                raise
            finally:
                mounted.stop()
                device.unlink(missing_ok=True)
                Path(str(device) + '.log').unlink(missing_ok=True)
                print('清理完成：测试挂载已卸载，专用磁盘和日志已删除。', flush=True)
    print(f'PASS: {args.suite} 全部通过，共 {progress.passed} 步。', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, AssertionError, RuntimeError, subprocess.SubprocessError) as error:
        progress.fail(error)
        sys.exit(1)
