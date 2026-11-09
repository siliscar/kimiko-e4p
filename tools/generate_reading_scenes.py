#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：校验六幅105x81像素图并按行打包为4bpp常量。/ English: Validate six 105x81 scenes and pack row-aligned 4bpp constants."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "main/assets/reading-scenes"
NAMES = ("slow-train", "corner-bookshop", "paper-garden", "moon-observatory", "vinyl-room", "lighthouse-library")
WIDTH, HEIGHT, SCALE = 105, 81, 2
ROW_BYTES = (WIDTH + 1) // 2
DATA_BYTES = ROW_BYTES * HEIGHT
PALETTE = {17, 68, 136, 187, 221, 255}

def generated():
    manifest = json.loads((ASSETS / "scenes.json").read_text())
    assert [scene["slug"] for scene in manifest["scenes"]] == list(NAMES), "Scene order differs"
    assert {path.stem for path in ASSETS.glob("*.png")} == set(NAMES), "Expected only six scene PNGs"
    lines = [
        "/*",
        " * SPDX-FileCopyrightText: 2026 chopper1026 (custom additions and compositions)",
        " * SPDX-FileCopyrightText: 2026 Patch (PixelFX), 2026 Hugo Duprez (selected effects)",
        " * SPDX-License-Identifier: MIT AND LicenseRef-Kiikoread-Personal-Study-1.0",
        " *",
        " * 中文：自动生成，勿手改；PixelFX部分为MIT，原创构图许可见reading-scenes/README.md。",
        " * English: Generated; do not edit. PixelFX parts are MIT; see reading-scenes/README.md for original compositions.",
        " */",
        "#pragma once", "#include <stdint.h>",
        f"#define READING_SCENE_COUNT {len(NAMES)}",
        f"#define READING_SCENE_WIDTH {WIDTH}",
        f"#define READING_SCENE_HEIGHT {HEIGHT}",
        f"#define READING_SCENE_SCALE {SCALE}",
        f"#define READING_SCENE_ROW_BYTES {ROW_BYTES}",
        f"#define READING_SCENE_DATA_BYTES {DATA_BYTES}",
        "static const uint8_t reading_scene_pixels[READING_SCENE_COUNT][READING_SCENE_DATA_BYTES] = {",
    ]
    for name, scene in zip(NAMES, manifest["scenes"]):
        path = ASSETS / (name + ".png")
        assert hashlib.sha256(path.read_bytes()).hexdigest() == scene["sha256"], (name, "PNG hash differs")
        assert scene["sourceSize"] == [WIDTH, HEIGHT] and scene["displaySize"] == [WIDTH*SCALE, HEIGHT*SCALE], name
        with Image.open(path) as image:
            assert image.size == (WIDTH, HEIGHT), (name, "Wrong size")
            rgba = image.convert("RGBA").tobytes()
            assert all(r == g == b and a == 255 and r in PALETTE for r, g, b, a in zip(rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4])), (name, "Invalid gray/alpha")
            pixels = image.convert("L").tobytes()
        packed = []
        # 奇数宽度逐行补白，左像素位于低半字节。/ Pad odd rows with white; the left pixel occupies the low nibble.
        for row in range(HEIGHT):
            for col in range(0, WIDTH, 2):
                left = pixels[row*WIDTH+col] >> 4
                right = pixels[row*WIDTH+col+1] >> 4 if col+1 < WIDTH else 15
                packed.append(left | (right << 4))
        assert len(packed) == DATA_BYTES, name
        lines.append("    {")
        for offset in range(0, len(packed), 30):
            lines.append("        " + ",".join(f"0x{v:02x}" for v in packed[offset:offset+30]) + ",")
        lines.append("    },")
    return "\n".join(lines + ["};", ""])

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    path = ROOT / "main/assets/reading_scenes.h"
    data = generated()
    if args.check:
        assert path.read_text() == data, "Regenerate reading scenes"
    else:
        path.write_text(data)
    print(f"PASS: six fixed 4bpp {WIDTH}x{HEIGHT} scenes, {ROW_BYTES}-byte rows, {len(NAMES)*DATA_BYTES} bytes")
