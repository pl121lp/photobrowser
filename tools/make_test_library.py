#!/usr/bin/env python3
"""Build a large synthetic photo library from a few real photos, for scale tests.

Creates <dst>/<0000..>/IMG_<n>.jpg as symbolic links (hard links on Windows) to the
JPEGs in <src>, round-robin. Links keep the source data in the OS file cache, so
use a real library to measure cold-disk indexing time.

    python3 tools/make_test_library.py photos /path/to/lib10k --count 10000
"""
import argparse
import os
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path, help="folder with source JPEGs")
    ap.add_argument("dst", type=Path, help="folder to create (must not exist or be empty)")
    ap.add_argument("--count", type=int, default=10000)
    ap.add_argument("--per-folder", type=int, default=1000)
    args = ap.parse_args()

    sources = sorted(p.resolve() for p in args.src.iterdir() if p.suffix.lower() in (".jpg", ".jpeg"))
    if not sources:
        print(f"no JPEGs in {args.src}", file=sys.stderr)
        return 1
    if args.dst.exists() and any(args.dst.iterdir()):
        print(f"{args.dst} is not empty", file=sys.stderr)
        return 1
    link = os.link if os.name == "nt" else os.symlink
    for n in range(args.count):
        folder = args.dst / f"{n // args.per_folder:04d}"
        folder.mkdir(parents=True, exist_ok=True)
        link(sources[n % len(sources)], folder / f"IMG_{n:05d}.jpg")
    print(f"created {args.count} links in {args.dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
