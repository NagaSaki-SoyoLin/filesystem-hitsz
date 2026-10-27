#!/usr/bin/env python3
"""Print an encoded dynamic VectorFS query path."""
import argparse
from pathlib import Path
from urllib.parse import quote


def search_path(mountpoint, query):
    if not query or '\x00' in query:
        raise ValueError('query must be nonempty and contain no NUL')
    return Path(mountpoint) / '.search' / quote(query, safe='')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mountpoint')
    parser.add_argument('query')
    args = parser.parse_args()
    try:
        print(search_path(args.mountpoint, args.query))
    except ValueError as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
