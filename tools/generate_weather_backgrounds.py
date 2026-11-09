#!/usr/bin/env python3
"""生成已确认天气背景的只读位图。/ Generate read-only masks for the approved weather backgrounds."""
import argparse
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSET = ROOT / "main/assets/weather_backgrounds.bin"
SCENES = ("sun", "rain", "cloud", "snow", "fog", "storm", "dust", "sleet", "overcast")
SIZE = len(SCENES) * ((612 + 7) // 8) * 448


def host_definition():
    data = ASSET.read_bytes()
    if len(data) != SIZE:
        raise ValueError("Invalid weather background asset size")
    rows = [",".join(str(value) for value in data[i:i + 32]) for i in range(0, len(data), 32)]
    return ('const uint8_t weather_backgrounds_asset[' + str(SIZE) + '] '
            'asm("_binary_weather_backgrounds_bin_start") = {\n' + ",\n".join(rows) + "\n};\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=ASSET)
    args = parser.parse_args()
    packed = bytearray()
    for name in SCENES:
        with Image.open(ROOT / "main/assets/weather" / (name + ".png")) as image:
            if image.mode != "1" or image.size != (612, 448):
                raise ValueError(f"{name}: expected a native-size 1bpp source mask")
            pixels = image.load()
            data = bytearray(77 * 448)
            for y in range(448):
                for x in range(612):
                    if pixels[x, y] == 0:
                        data[y * 77 + x // 8] |= 1 << (x % 8)
            packed.extend(data)
    data = bytes(packed)
    if len(data) != SIZE:
        raise ValueError("Invalid generated weather background size")
    if args.check:
        if not args.output.exists() or args.output.read_bytes() != data:
            raise SystemExit("Weather background asset is outdated; run tools/generate_weather_backgrounds.py")
        print(f"PASS: {len(SCENES)} 1bpp weather masks are reproducible")
    elif not args.output.exists() or args.output.read_bytes() != data:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(f"Generated {len(data)} bytes of weather masks")


if __name__ == "__main__":
    main()
