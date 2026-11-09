/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：静态 WebP 解码适配，仅供公共图像出口使用。
 * English: Static WebP adapter for the shared image entry point.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// 居中裁剪、缩放并合成白底灰阶；输出与源像素数均有界。
/// Center-crop, scale and composite to white-backed grayscale with bounded source/output pixels.
bool book_webp_grayscale(const uint8_t *data, size_t size, unsigned width,
                         unsigned height, uint8_t *out);
