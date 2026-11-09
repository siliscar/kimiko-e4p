/* SPDX-License-Identifier: Apache-2.0
 * 中文：公共图像探测、缩放与灰阶解码；不访问书籍缓存、页面或字库。
 * English: Shared image probing, scaling and grayscale decoding; no book caches, pages or fonts.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 按填满目标、居中裁剪计算的源图窗口；不拉伸。/ Source window for aspect fill and centered cropping, without stretching.
typedef struct {
    unsigned x, y, width, height; ///< 源窗口坐标与尺寸 / Source window coordinates and dimensions
} book_crop_t;

/// 兼容既有调用名；仅计算窗口，不读取文件。/ Retain the existing API name; calculate the crop without file I/O.
book_crop_t book_cover_crop(unsigned src_width, unsigned src_height,
                            unsigned dst_width, unsigned dst_height);
/// JPEG/PNG/静态 WebP 解码为每像素一字节灰度；WebP 按签名识别，png 仅标记 PNG。
/// Decode JPEG/PNG/static WebP to byte grayscale, bounded to one panel; WebP is signature-detected.
bool book_image_grayscale(const uint8_t *data, size_t size, bool png,
                          unsigned out_width, unsigned out_height, uint8_t *out);
/// 读取图像头尺寸；不分配像素。/ Read header dimensions without pixel allocations.
bool book_image_dimensions(const uint8_t *data, size_t size, bool png,
                           unsigned *width, unsigned *height);
/// JPEG 帧头同时支持基线与渐进式。/ Probe both baseline and progressive JPEG frame headers.
bool book_jpeg_frame(const uint8_t *data, size_t size, uint8_t *sof, unsigned *width, unsigned *height);

/// 文件流输入上限为 50 MiB，输出仍限制为一屏。/ Stream inputs up to 50 MiB, with output bounded to one panel.
#define BOOK_IMAGE_FILE_MAX (50u * 1024u * 1024u)
/// 仅读取文件头，不解码像素。/ Probe file headers without decoding pixels.
bool book_image_file_dimensions(const char *path, bool png, unsigned *width, unsigned *height);
/// 文件流解码为调用方持有的灰阶缓冲。/ Decode a file stream into caller-owned grayscale storage.
bool book_image_file_grayscale(const char *path, bool png, unsigned width, unsigned height, uint8_t *out);
/// PNG 预乘灰度和 alpha，各由调用方提供 width*height 字节。/ Premultiplied PNG gray and alpha, each in caller-owned width*height bytes.
bool book_png_file_layer(const char *path,unsigned width,unsigned height,uint8_t *gray,uint8_t *alpha);
