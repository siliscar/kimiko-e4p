/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
// Preparation performs bounded file IO; painting never reads files or allocates.
// 原尺寸4位背景单份最多415872B，正文不重缩放。
// Native-resolution 4-bit wallpaper (<=415872B); no second resident cache or per-page image rescale.
// 正文固定屏幕坐标，纸面白点归一后与预览共用同一份灰度。
// Blended reading backgrounds use fixed screen coordinates; paper is white-point clamped and preview shares the cached tones.
bool ui_reader_background_prepare(const char *path);
void ui_reader_background_release(void);
void ui_reader_background_paint(uint8_t *fb, EpdRect area, bool blend);
