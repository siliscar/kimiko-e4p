#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：压缩用户 logo 并生成共用锁屏及默认头像。/ English: Pack the user logo, shared lock art and default avatars."""

import argparse
from io import BytesIO
from pathlib import Path

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/image/kimiko-logo.png"
SCREEN = (684, 1216)
AVATAR_SIDES = (60, 82)


def gray16(image):
    return image.convert("L").point(lambda value: ((value + 8) // 17) * 17)


def png(image):
    output = BytesIO()
    image.quantize(colors=16, dither=Image.Dither.NONE).save(
        output, format="PNG", bits=4, optimize=True
    )
    return output.getvalue()


def packed(image):
    pixels = gray16(image).tobytes()
    assert image.width % 2 == 0
    # 左像素位于低半字节，与 EPD 绘图契约一致。/ The left pixel uses the low nibble, matching EPD drawing.
    return bytes((left >> 4) | (right & 0xF0) for left, right in zip(pixels[::2], pixels[1::2]))


def outputs(logo):
    lock = Image.new("L", SCREEN, 255)
    mark = gray16(logo.resize((588, 588), Image.Resampling.LANCZOS))
    lock.paste(mark, ((SCREEN[0] - mark.width) // 2, (SCREEN[1] - mark.height) // 2))
    avatars = bytearray()
    for side in AVATAR_SIDES:
        avatar = Image.new("L", (side, side), 255)
        inset = side // 12
        mark = gray16(logo.resize((side - 2 * inset, side - 2 * inset), Image.Resampling.LANCZOS))
        avatar.paste(mark, (inset, inset))
        avatars.extend(packed(avatar))
    return {
        ROOT / "assets/image/lock-screen.png": png(lock),
        ROOT / "main/assets/lock_4bpp.bin": packed(lock),
        ROOT / "main/assets/avatar_4bpp.bin": bytes(avatars),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logo", type=Path, help="Import a replacement logo; preserve the original file")
    parser.add_argument("--check", action="store_true", help="Check generated assets without writing")
    args = parser.parse_args()
    if args.logo and args.check:
        parser.error("--logo and --check are mutually exclusive")
    if args.logo:
        with Image.open(args.logo) as original:
            rgba = ImageOps.exif_transpose(original).convert("RGBA")
            image = Image.new("RGBA", rgba.size, "white")
            image.alpha_composite(rgba)
            logo = gray16(ImageOps.pad(image.convert("L"), (600, 600),
                                      method=Image.Resampling.LANCZOS, color=255))
        SOURCE.write_bytes(png(logo))
    with Image.open(SOURCE) as source:
        assert source.size == (600, 600), "Expected a 600 x 600 source logo"
        logo = source.convert("L")
        assert all(value % 17 == 0 for value in logo.tobytes()), "Expected 16-level grayscale"
    for path, data in outputs(logo).items():
        if args.check:
            assert path.read_bytes() == data, f"Regenerate {path.relative_to(ROOT)}"
        elif not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        print(f"{'CHECK' if args.check else 'WRITE'} {path.relative_to(ROOT)}: {len(data)} bytes")


if __name__ == "__main__":
    main()
