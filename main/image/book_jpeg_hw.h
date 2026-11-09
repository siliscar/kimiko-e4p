/* SPDX-License-Identifier: Apache-2.0
 * 中文：S31 硬件 JPEG 解码的内存封装。只接收整段内存中的基线 JPEG，输出
 * 带 MCU 填充行宽的 RGB565 或灰阶平面；渐进式、PNG、文件流和缩放都不在这里。
 * English: In-memory hardware JPEG decode wrapper for S31. It accepts a whole
 * baseline JPEG, and returns an RGB565 or gray plane with the MCU-padded stride;
 * progressive, PNG, file streaming and scaling stay outside this layer.
 *
 * 冻结：不处理渐进式/PNG/文件流；不做缩放、缓存或 UI 决策；任何失败都交给软件路径。
 * Frozen: No progressive/PNG/file streaming; no scaling, caching or UI decisions;
 * every failure falls back to the software path.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(BOOK_JPEG_HW_FORCE_SOFTWARE) && BOOK_JPEG_HW_FORCE_SOFTWARE
#define BOOK_JPEG_HW 0
#elif defined(ESP_PLATFORM) && defined(CONFIG_SOC_JPEG_DECODE_SUPPORTED) && CONFIG_SOC_JPEG_DECODE_SUPPORTED
#define BOOK_JPEG_HW 1
#else
#define BOOK_JPEG_HW 0
#endif

/// 硬件解码结果。pixels 为 PSRAM 中 cache 行对齐的整幅缓冲，由 book_jpeg_hw_free 释放。
/// Hardware decode result. pixels is a cache-line-aligned full-frame buffer in PSRAM,
/// released by book_jpeg_hw_free.
typedef struct {
    uint8_t *pixels;  ///< RGB565（2 字节/像素）或 GRAY（1 字节/像素）/ RGB565 or GRAY
    unsigned width;   ///< 可见宽 / visible width
    unsigned height;  ///< 可见高 / visible height
    unsigned stride;  ///< 含 MCU 填充的行宽（像素）/ row stride including MCU padding
    bool gray;        ///< true 表示 pixels 是单字节灰阶 / true when pixels is 8-bit gray
} book_jpeg_hw_image_t;

/// 解码一段内存中的基线 JPEG。budget 是输出缓冲上限；超出、内存不足或硬件不可用时返回 false。
/// 调用者随后必须走原来的软件解码路径，行为与没有硬件时一致。
/// Decode one in-memory baseline JPEG. budget caps the output buffer; returns false when
/// the frame is too large, memory is tight or the codec is unavailable. The caller must
/// then use the original software path, exactly as if hardware did not exist.
bool book_jpeg_hw_decode(const uint8_t *data, size_t size, size_t budget,
                         book_jpeg_hw_image_t *out);

/// 释放 book_jpeg_hw_decode 返回的像素；image 随后清零。
/// Release pixels returned by book_jpeg_hw_decode and clear the image.
void book_jpeg_hw_free(book_jpeg_hw_image_t *image);
