#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""中文：渐进式 JPEG 经 stb_image 全分辨率解码的清晰度回归。
English: Regress that progressive JPEG decodes at full resolution via stb_image.

关键点：JPEGDEC 对 SOF2 只解 DC 扫描、强制 1/8，会把 1px 细节抹成灰团；
stb_image 全分辨率解码后，1px 黑线的对比度应从「几乎与背景同白」恢复。
The point: JPEGDEC decodes SOF2 from the DC scan at a forced 1/8, smearing 1 px
detail; after the stb_image full-res path, a 1 px black line's contrast recovers.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    developer = Path("/Library/Developer/CommandLineTools")
    if sys.platform == "darwin" and developer.exists():
        import os
        os.environ.setdefault("DEVELOPER_DIR", str(developer))

    # 构造源图：白底 + 1px 黑框 + 1px 网格线（真实图表的细线场景）。
    # Build a source: white background, a 1 px black frame and 1 px grid (real chart lines).
    W, H = 684, 860
    src = bytearray([255]) * (W * H)
    for x in range(60, 621):
        src[110 * W + x] = 0          # 顶框
        src[640 * W + x] = 0          # 底框
    for y in range(110, 641):
        src[y * W + 60] = 0           # 左框
        src[y * W + 620] = 0          # 右框
    for i in range(1, 6):
        y = 110 + i * 88
        for x in range(61, 620):
            src[y * W + x] = 176      # 浅灰网格线

    with tempfile.TemporaryDirectory(prefix="mogo-progjpeg-") as temp:
        work = Path(temp)
        (work / "sdkconfig.h").write_text(
            "#define CONFIG_JD_SZBUF 512\n#define CONFIG_JD_FORMAT 0\n"
            "#define CONFIG_JD_USE_SCALE 1\n#define CONFIG_JD_TBLCLIP 1\n"
            "#define CONFIG_JD_FASTDECODE 0\n")
        # 用 Pillow 压成渐进式（SOF2），与 test_image_files.py 一致，不依赖 cjpeg。
        # Encode progressive (SOF2) with Pillow, matching test_image_files.py, no cjpeg needed.
        from PIL import Image
        Image.frombytes("L", (W, H), bytes(src)).save(work / "prog.jpg",
                                                      "JPEG", quality=95, progressive=True)
        jpg = (work / "prog.jpg").read_bytes()
        sof2 = any(jpg[i] == 0xFF and jpg[i + 1] == 0xC2 for i in range(len(jpg) - 1))
        assert sof2, "expected SOF2 progressive marker"

        test_c = work / "test.c"
        test_c.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book_cover.h"

extern unsigned char test_jpeg[];
extern unsigned int test_jpeg_len;

int main(void) {
    // 内存路径：book_image_grayscale → jpeg_gray → 渐进式 → stb 全分辨率。
    // Memory path: book_image_grayscale -> jpeg_gray -> progressive -> stb full-res.
    uint8_t *out = malloc(684u * 860u);
    assert(out);
    assert(book_image_grayscale(test_jpeg, test_jpeg_len, false, 684, 860, out));

    // 顶框（1px 黑线）的对比度：DC-only 1/8 会把 min 抬到 ~221（接近背景 255）；
    // stb 全分辨率后 min 应接近 0（纯黑）。允许少量 JPEG 有损，阈 128。
    // Contrast of the 1 px top frame: DC-only 1/8 lifts min to ~221 (near background
    // 255); stb full-res should bring min back near 0 (black). Allow JPEG loss, cap 128.
    int mn = 255;
    for (int x = 60; x <= 620; ++x) {
        int v = out[110 * 684 + x];
        if (v < mn) mn = v;
    }
    assert(mn < 128 && "1px black frame washed out -> still DC-only 1/8");

    // 网格线（原 176 灰）应可分辨（明显低于 255 背景）。
    // The grid line (source gray 176) must stay visibly darker than the white background.
    int mn_grid = 255;
    for (int x = 61; x < 620; ++x) {
        int v = out[198 * 684 + x];
        if (v < mn_grid) mn_grid = v;
    }
    assert(mn_grid < 230 && "grid line indistinguishable from background");

    free(out);
    puts("PASS: progressive JPEG decodes at full resolution; 1 px lines keep contrast");
    return 0;
}
''')
        # 把 jpg 作为字节数组嵌入测试。
        arr = ", ".join(str(b) for b in jpg)
        cpp_file = work / "jpeg_blob.c"
        cpp_file.write_text(
            f"unsigned char test_jpeg[] = {{{arr}}};\n"
            f"unsigned int test_jpeg_len = {len(jpg)};\n")

        includes = ["tools/book_epub_stubs", "main/book", "main/image", "components/jpegdec/src",
                    "components/stb_image",
                    "managed_components/espressif__esp_jpeg/tjpgd", "managed_components/espressif__esp_jpeg/include"]
        flags = ["-g", "-O1", "-fno-omit-frame-pointer", "-ffunction-sections", "-fdata-sections"]
        flags += ["-I" + str(ROOT / p) for p in includes] + ["-I" + str(work)]
        png = ROOT / "managed_components/espressif__libpng/libpng"
        if not png.exists():
            raise SystemExit("Build the firmware once to prepare the pinned libpng sources.")
        (work / "pnglibconf.h").write_bytes((png / "scripts/pnglibconf.h.prebuilt").read_bytes())
        flags += ["-I" + str(png), "-DPNG_ARM_NEON_OPT=0", "-DPNG_INTEL_SSE_OPT=0"]
        sources = ["main/image/image_decode.c", "main/image/book_image_header.c",
                   "managed_components/espressif__esp_jpeg/tjpgd/tjpgd.c",
                   "components/jpegdec/src/JPEGDEC.cpp", "components/jpegdec/src/jpegdec_shim.cpp",
                   "components/stb_image/stb_image_impl.c"]
        # 最小 stub：本测试只走渐进式 stb 路径；WebP 与基线 esp_jpeg 符号仅需可链接。
        # Minimal stubs: this test only exercises the progressive stb path; WebP and the
        # baseline esp_jpeg symbols need only to link.
        stub_c = work / "stub.c"
        stub_c.write_text(r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "jpeg_decoder.h"
bool book_webp_grayscale(const uint8_t *d, size_t n, unsigned w, unsigned h, uint8_t *o) {
    (void)d; (void)n; (void)w; (void)h; (void)o; return false;
}
esp_err_t esp_jpeg_get_image_info(esp_jpeg_image_cfg_t *c, esp_jpeg_image_output_t *o) { (void)c; (void)o; return ESP_FAIL; }
esp_err_t esp_jpeg_decode(esp_jpeg_image_cfg_t *c, esp_jpeg_image_output_t *o) { (void)c; (void)o; return ESP_FAIL; }
''')
        subprocess.run(["cc", *flags, "-c", str(stub_c), "-o", str(work / "stub.o")], check=True)

        objects = []
        for i, source in enumerate(sources):
            obj = work / f"part{i}.o"
            cpp = source.endswith(".cpp")
            extra = ["-fno-sanitize=alignment,shift"] if source.endswith("JPEGDEC.cpp") else []
            subprocess.run(["c++" if cpp else "cc", "-std=c++17" if cpp else "-std=c11",
                            *flags, *extra, "-c", str(ROOT / source), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run(["cc", *flags, "-c", str(cpp_file), "-o", str(work / "blob.o")], check=True)
        subprocess.run(["cc", *flags, "-c", str(test_c), "-o", str(work / "test.o")], check=True)
        objects += [str(work / "blob.o"), str(work / "test.o"), str(work / "stub.o")]
        gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        exe = work / "test"
        png_src = [png / (n + ".c") for n in
                   "png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset pngtrans pngwrite pngwio pngwtran pngwutil".split()]
        subprocess.run(["c++", *flags, gc, *objects, *map(str, png_src), "-lz", "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
