#!/usr/bin/env python3
"""构建时无损压缩开机与锁屏共用图。/ Losslessly pack the shared splash/lock image at build time."""

import argparse
from pathlib import Path
import struct
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("lock_4bpp",):
        raw = (args.source / (name + ".bin")).read_bytes()
        if len(raw) != 684 * 1216 // 2:
            parser.error(f"{name}: expected a 684 x 1216 4bpp frame")
        compressed = zlib.compress(raw, level=9)
        # 固定头含原始长度和压缩长度；zlib 尾部校验原像素。/ The fixed header stores both lengths; zlib checks the original pixels.
        packed = struct.pack("<4sII", b"KBI1", len(raw), len(compressed)) + compressed
        destination = args.output / (name + ".kbz")
        if not destination.exists() or destination.read_bytes() != packed:
            destination.write_bytes(packed)


if __name__ == "__main__":
    main()
