#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 chopper1026
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：固定 WebP 解码器回归，可直接读取私有原书，不复制入库。
English: Regress the pinned WebP decoder and optionally read private EPUBs in place.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    developer = Path("/Library/Developer/CommandLineTools")
    if sys.platform == "darwin" and developer.exists():
        os.environ.setdefault("DEVELOPER_DIR", str(developer))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("epubs", nargs="*", type=Path)
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mogo-webp-") as temp:
        work = Path(temp)
        (work / "sdkconfig.h").write_text(
            "#define CONFIG_JD_SZBUF 512\n#define CONFIG_JD_FORMAT 0\n"
            "#define CONFIG_JD_USE_SCALE 1\n#define CONFIG_JD_TBLCLIP 1\n"
            "#define CONFIG_JD_FASTDECODE 0\n")
        vendor = ROOT / "components/webp_decoder/libwebp"
        png = ROOT / "managed_components/espressif__libpng/libpng"
        if not png.exists():
            raise SystemExit("Build the firmware once to prepare the pinned libpng sources.")
        (work / "pnglibconf.h").write_bytes((png / "scripts/pnglibconf.h.prebuilt").read_bytes())
        includes = ["tools/webp_test_stubs", "tools/book_epub_stubs", "tools/zip_host_stubs",
                    "main/book", "main/image", "components/jpegdec/src",
                    "managed_components/espressif__esp_jpeg/tjpgd",
                    "managed_components/espressif__esp_jpeg/include",
                    "components/stb_image",
                    "components/webp_decoder/include", "components/webp_decoder/libwebp",
                    "components/webp_decoder/libwebp/src"]
        flags = ["-g", "-O1", "-pthread", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 "-ffunction-sections", "-fdata-sections", "-I" + str(work)]
        if sys.platform.startswith("linux"):
            flags.append("-D__LINUX__")
        flags += ["-I" + str(ROOT / path) for path in includes]
        flags += ["-I" + str(png), "-DPNG_ARM_NEON_OPT=0", "-DPNG_INTEL_SSE_OPT=0"]
        sources = ["tools/webp_host_test.c", "main/book/book_epub.c", "main/book/zip_reader.c",
                   "main/book/html_text.c", "main/book/book_index_cache.c",
                   "main/image/book_image_header.c", "main/image/image_decode.c", "main/image/image_webp.c",
                   "managed_components/espressif__esp_jpeg/tjpgd/tjpgd.c",
                   "components/jpegdec/src/JPEGDEC.cpp", "components/jpegdec/src/jpegdec_shim.cpp",
                   "components/stb_image/stb_image_impl.c"]
        sources = [ROOT / path for path in sources]
        sources += sorted(path for folder in ("dec", "dsp", "utils")
                          for path in (vendor / "src" / folder).glob("*.c"))
        sources += [png / (name + ".c") for name in
                    "png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwrite pngwio pngwtran pngwutil".split()]
        objects = []
        for i, source in enumerate(sources):
            obj = work / f"part{i}.o"
            cpp = source.suffix == ".cpp"
            extra = []
            if vendor in source.parents:
                extra = ["-DHAVE_CONFIG_H", "-DWEBP_MAX_ALLOCABLE_MEMORY=8388608",
                         "-include", str(ROOT / "components/webp_decoder/webp_alloc.h")]
            elif source.name == "JPEGDEC.cpp":
                extra = ["-fno-sanitize=alignment,shift"]
            subprocess.run(["c++" if cpp else "cc", "-std=c++17" if cpp else "-std=c11",
                            *flags, *extra, "-c", str(source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        exe = work / "test"
        subprocess.run(["c++", *flags, gc, *objects, "-lz", "-lm", "-o", str(exe)], check=True)
        good = []
        for name, mode, color, lossless, metadata in (
                ("lossy", "RGB", (128, 128, 128), False, {}),
                ("lossless", "RGB", (128, 128, 128), True, {}),
                ("alpha", "RGBA", (128, 128, 128, 128), True, {}),
                ("extended", "RGB", (128, 128, 128), False, {"icc_profile": b"test"})):
            path = work / (name + ".webp")
            Image.new(mode, (17, 13), color).save(path, lossless=lossless, **metadata)
            good.append(path)
        subprocess.run([str(exe), "concurrent", *map(str, good)], check=True)
        subprocess.run([str(exe), "decode", *map(str, good)], check=True)
        animation = work / "animation.webp"
        Image.new("RGB", (17, 13), "black").save(animation, save_all=True,
            append_images=[Image.new("RGB", (17, 13), "white")], duration=100, loop=0)
        too_large = work / "too-large.webp"
        too_large.write_bytes(b"RIFF" + struct.pack("<I", 22) + b"WEBPVP8X" +
                             struct.pack("<I", 10) + b"\0" * 4 +
                             (4095).to_bytes(3, "little") * 2)
        subprocess.run([str(exe), "reject", str(animation), str(too_large)], check=True)
        book = work / "synthetic.epub"
        container = '<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>'
        opf = ('<package><metadata><meta name="cover" content="art"/></metadata><manifest>'
               '<item id="body" href="text/_**::chapter.xhtml" media-type="application/xhtml+xml"/>'
               '<item id="art" href="images/_**::cover.webp" media-type="image/webp"/>'
               '<item id="wrap" href="images/wrapper.svg" media-type="image/svg+xml"/>'
               '</manifest><spine><itemref idref="body"/></spine></package>')
        with zipfile.ZipFile(book, "w", compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("mimetype", "application/epub+zip")
            z.writestr("META-INF/container.xml", container)
            z.writestr("OPS/book.opf", opf)
            z.writestr("OPS/text/_**::chapter.xhtml", '<html><head><link rel="stylesheet" href="../style/nested/book.css"/></head>'
                       '<body class="plate"><h1>Chapter 1</h1>'
                       '<img src="../images/_**::cover.webp"/>'
                       '<img src="../images/wrapper.svg"/></body></html>')
            z.writestr("OPS/style/nested/book.css", "body.plate{background-position:top;/* comment */"
                       "background-image:url('../../images/_**::cover.webp')}")
            z.writestr("OPS/images/_**::cover.webp", good[0].read_bytes())
            z.writestr("OPS/images/wrapper.svg", '<svg><image href="_**::cover.webp"/></svg>')
        subprocess.run([str(exe), "epub", str(book), *map(str, options.epubs)], check=True)
        print("PASS: pinned portable WebP decoder, real image entry point, EPUB cover/images and safety bounds")


if __name__ == "__main__":
    main()
