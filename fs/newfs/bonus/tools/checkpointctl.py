#!/usr/bin/env python3
"""Send one CheckpointFS request; successful close is not a commit acknowledgement."""
import argparse
import os
from pathlib import Path
import sys
from urllib.parse import quote


def normalize_file(file):
    if not file.startswith('/'):
        raise ValueError('FILE must be an absolute path inside the mount, e.g. /model.bin')
    parts = []
    for component in file.split('/'):
        if component in ('', '.'):
            continue
        if component == '..':
            if not parts:
                raise ValueError('FILE escapes the mount root')
            parts.pop()
        else:
            parts.append(component)
    return '/' + '/'.join(parts)


def file_key(file):
    parts = normalize_file(file).strip('/').split('/')
    def encode(part):
        return ''.join(chr(b) if (48 <= b <= 57 or 65 <= b <= 90 or 97 <= b <= 122 or b in (45, 46)) else f'%{b:02X}' for b in part.encode('utf-8'))
    return '_'.join(encode(p) for p in parts[:-1]) + '__' + encode(parts[-1])


def request(mountpoint, operation, file, id, new_id=None):
    if operation not in ('create', 'restore'):
        raise ValueError('operation must be create or restore')
    if (operation == 'restore') != (new_id is not None):
        raise ValueError('restore requires HIST_CHECKPOINT and NEW_CHECKPOINT')
    fields = [operation, quote(normalize_file(file), safe=''), id]
    if new_id is not None:
        fields.append(new_id)
    if any('\n' in field or '\t' in field or '\x00' in field for field in fields):
        raise ValueError('invalid protocol field')
    data = ('\t'.join(fields) + '\n').encode('utf-8')
    fd = os.open(Path(mountpoint) / '.checkpoint/control', os.O_WRONLY)
    try:
        offset = 0
        while offset < len(data):
            size = os.write(fd, data[offset:])
            if size <= 0:
                raise OSError('control write made no progress')
            offset += size
    finally:
        os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mountpoint')
    commands = parser.add_subparsers(dest='operation', required=True)
    for operation in ('create', 'restore'):
        command = commands.add_parser(operation)
        command.add_argument('file', help='mount-relative absolute path, e.g. /model.bin')
        if operation == 'create':
            command.add_argument('id', help='new independent checkpoint ID')
        else:
            command.add_argument('hist_id', metavar='HIST_CHECKPOINT', help='existing historical checkpoint')
            command.add_argument('new_id', metavar='NEW_CHECKPOINT', help='new copy to share with FILE; history stays unchanged')
    args = parser.parse_args()
    first_id = args.id if args.operation == 'create' else args.hist_id
    request(args.mountpoint, args.operation, args.file, first_id, getattr(args, 'new_id', None))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(f'checkpointctl: {error}', file=sys.stderr)
        sys.exit(1)
