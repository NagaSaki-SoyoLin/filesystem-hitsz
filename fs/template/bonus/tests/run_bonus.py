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


def require(value, message):
    if not value:
        raise AssertionError(message)


def base_scenario(root):
    directory = root / 'ordinary'
    directory.mkdir()
    source = directory / 'source'
    data = bytes(range(256)) * 24
    source.write_bytes(data)
    require(source.read_bytes() == data, 'ordinary multi-block read/write')
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
    work = root / 'model.bin'
    view = root / '.checkpoints' / file_key('/model.bin')
    initial = bytes(range(256)) * 17 + b'copy-tail'
    work.write_bytes(initial)
    request(root, 'create', '/model.bin', 'v1')
    v1 = view / 'v1'
    require(v1.read_bytes() == initial, 'create copies multiple buffers and the final partial chunk')
    require(v1.stat().st_ino != work.stat().st_ino, 'create allocates independent inode')
    work.write_bytes(b'B')
    try:
        request(root, 'create', '/model.bin', 'v1')
    except FileExistsError:
        pass
    else:
        raise AssertionError('create must refuse an existing checkpoint ID')
    require(v1.read_bytes() == initial, 'create backup remains independent')
    request(root, 'restore', '/model.bin', 'v2', 'v1')
    require((view / 'v2').read_bytes() == b'B', 'restore backs up current content first')
    require(work.read_bytes() == initial, 'restore reads selected checkpoint')
    require(work.stat().st_ino == v1.stat().st_ino, 'restore shares target inode')
    work.write_bytes(b'C')
    require(v1.read_bytes() == b'C', 'shared write changes target checkpoint')
    require((view / 'v2').read_bytes() == b'B', 'backup is not a hardlink to working inode')
    request(root, 'restore', '/model.bin', 'v3', 'v2')
    require((view / 'v3').read_bytes() == b'C', 'second restore preserves pre-restore content')
    require(work.read_bytes() == b'B', 'second restore recovers original backup')
    require(work.stat().st_ino == (view / 'v2').stat().st_ino, 'second restore shares chosen target')
    request(root, 'restore', '/model.bin', 'v4', 'v2')
    require((view / 'v4').read_bytes() == b'B', 'restoring the current target still saves a backup')
    require(work.stat().st_nlink == 2, 'same-inode restore preserves the two existing links')
    subprocess.run(['ls', '-li', str(view)], check=True)
    subprocess.run(['cat', str(work)], check=True)
    print('\nPASS CheckpointFS: independent copy, backup-before-restore, shared write, restore again', flush=True)


def checkpoint_persistence(root):
    work = root / 'model.bin'
    view = root / '.checkpoints' / file_key('/model.bin')
    require(work.read_bytes() == b'B', 'working content survives remount')
    require((view / 'v1').read_bytes() == b'C', 'shared checkpoint content survives remount')
    require((view / 'v3').read_bytes() == b'C', 'independent backup survives remount')
    require(work.stat().st_ino == (view / 'v2').stat().st_ino, 'hardlink identity survives remount')
    require(work.stat().st_nlink == 2, 'hardlink count survives remount')
    print('PASS CheckpointFS remount: content, inode identity and link count', flush=True)


def vector_scenario(root):
    documents = root / 'documents'
    documents.mkdir()
    (documents / 'os').mkdir()
    (documents / 'a.txt').write_text('aaaa', encoding='utf-8')
    (documents / 'os/a.txt').write_text('aaab', encoding='utf-8')
    (documents / 'b.txt').write_text('bbbb', encoding='utf-8')
    require(list((root / '.search').iterdir()) == [], 'search root has no stored query entries')
    query = search_path(root, 'a')
    require(query.is_dir(), 'getattr describes the query as a standard directory')
    names = [p.name for p in query.iterdir()]
    require(names == ['1.000000__01', '0.948683__02', '0.000000__03'], f'query a ranking: {names}')
    expected = ['a.txt', 'os/a.txt', 'b.txt']
    for name, relative in zip(names, expected):
        result = query / name
        require(result.is_symlink(), 'search result must be a virtual symlink')
        require(os.readlink(result) == '../../documents/' + relative, 'score stays paired with document path')
        require(result.lstat().st_size == len(os.readlink(result).encode()), 'getattr reports symlink target byte length')
        require(result.read_bytes() == (documents / relative).read_bytes(), 'following result reads real document')
    subprocess.run(['ls', '-l', str(query)], check=True)
    subprocess.run(['readlink', str(query / names[0])], check=True)
    subprocess.run(['cat', str(query / names[0])], check=True)
    other = search_path(root, 'b') / '1.000000__01'
    require(os.readlink(other) == '../../documents/b.txt', 'second query independently matches current documents')
    (documents / 'a.txt').write_text('bbbb', encoding='utf-8')
    require(os.readlink(other) == '../../documents/a.txt', 'changed document is re-embedded; equal scores retain path order')
    for index in range(9):
        (documents / f'extra{index}.txt').write_text('bbbb', encoding='utf-8')
    top = list(search_path(root, 'b').iterdir())
    require(len(top) == 10, 'only top ten of twelve documents are exposed')
    require(top[-1].name == '1.000000__10', 'ranking names include stable one-based rank')
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
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--suite', choices=('base', 'checkpoint', 'vector', 'all'), default='all')
    parser.add_argument('--device', type=Path, required=True, help='new dedicated path; existing files are refused')
    args = parser.parse_args()
    device = args.device.absolute()
    default = Path(pwd.getpwuid(os.getuid()).pw_dir) / 'ddriver'
    if device.exists() or device.is_symlink() or device.resolve() == default.resolve():
        parser.error('use a new dedicated device path, never an existing disk or the default ddriver')
    if not device.parent.is_dir() or Path(str(device) + '.log').exists():
        parser.error('device parent must exist and device log path must not already exist')
    binary = args.binary.resolve(strict=True)
    if not Path('/dev/fuse').exists() or not shutil.which('fusermount'):
        parser.error('environment blocked: /dev/fuse and fusermount are required')
    fd = os.open(device, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
    os.close(fd)
    with tempfile.TemporaryDirectory(prefix='lab5-bonus-') as temporary:
        root = Path(temporary) / 'mnt'
        root.mkdir()
        logpath = Path(temporary) / 'fuse.log'
        with logpath.open('w+') as log:
            mounted = Mounted(binary, device, root, log)
            try:
                mounted.start()
                if args.suite in ('base', 'all'):
                    base_scenario(root)
                if args.suite in ('checkpoint', 'all'):
                    checkpoint_scenario(root)
                if args.suite in ('vector', 'all'):
                    vector_scenario(root)
                mounted.stop()
                mounted.start()
                if args.suite in ('base', 'all'):
                    require((root / 'ordinary/alias').read_bytes() == bytes(range(256)) * 24, 'ordinary data persists')
                if args.suite in ('checkpoint', 'all'):
                    checkpoint_persistence(root)
                if args.suite in ('vector', 'all'):
                    result = search_path(root, 'b') / '1.000000__01'
                    require(result.read_bytes() == b'bbbb', 'search follows persisted documents after remount')
                    print('PASS VectorFS remount: ordinary documents remain searchable', flush=True)
            finally:
                mounted.stop()
                log.seek(0)
                if sys.exc_info()[0] is not None:
                    print(log.read(), file=sys.stderr)
                device.unlink(missing_ok=True)
                Path(str(device) + '.log').unlink(missing_ok=True)
    print(f'PASS suite={args.suite}: real FUSE mount and remount; temporary disk removed')


if __name__ == '__main__':
    try:
        main()
    except (OSError, AssertionError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
