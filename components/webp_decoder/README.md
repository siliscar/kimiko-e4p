# WebP Decoder

EPUB 静态 WebP 封面及插图使用 libwebp 1.6.0 的可移植 C 解码后端，支持 VP8、VP8L 和透明度；不引入编码器、动画播放器或 SIMD。启用上游 pthread 互斥以保护共享表初始化，但不启动解码工作线程。源图每轴最多 8192 且不超过 4,194,304 像素，单次上游内存分配最多 8 MiB，分配使用 PSRAM；输出由公共图像层限制为一屏，透明像素合成到白底。超限、动画、损坏或内存不足时沿用图片加载失败路径。

Static EPUB covers and illustrations use libwebp 1.6.0's portable C decoder for VP8, VP8L and alpha, without encoders, animation playback or SIMD. Upstream pthread mutexes protect shared table initialization; decoding worker threads remain disabled. Source images are limited to 8192 per axis and 4,194,304 pixels; each upstream allocation is limited to 8 MiB in PSRAM. The shared image layer bounds output to one panel and composites alpha onto white. Oversized, animated, corrupt or out-of-memory inputs use the existing image failure path.

## Source And License

- Source: <https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz>
- SHA-256: `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`
- `libwebp/` contains the unmodified decoder/common C sources and required headers; encoder and platform SIMD sources are omitted. Original [COPYING](libwebp/COPYING), [PATENTS](libwebp/PATENTS) and [AUTHORS](libwebp/AUTHORS) are retained. These upstream sources keep their BSD-style license and patent grant.
- Local CMake, configuration and allocation adapters are original material under `LicenseRef-Kiikoread-Personal-Study-1.0`, not a relicensing of libwebp.

Regression: `python3 tools/webp_host_test.py`; optional private EPUB paths exercise real opening, every chapter and all referenced images without copying books into the repository. Host checks do not replace physical-device acceptance.
