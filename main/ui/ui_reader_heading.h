/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：正文和设置预览共用的题头几何与纯绘制入口。
 * English: Shared heading geometry and paint-only entry for body pages and settings previews.
 */
#pragma once
#include "epdiy.h"

// 横排章节题头的整块高度：绘制与排版预留共用。样式与字号都只改绘制、不改这里，
// 所以两者换档都只重画当前页；加高的部分由正文在章节首页让出。
// Height of the horizontal chapter heading band, shared by painting and the layout reservation:
// both the style and the size change the painting only, never this height, so either one repaints
// the current page; the body yields the added room on a chapter's first page.
#define READER_HEADING_BAND_HEIGHT 294
// 基准档的题头块：纵向坐标都写在块内（[4, 180]，高 176），块在带内垂直居中，字号缩放以块为基准。
// The heading block at the reference level: every vertical coordinate is written inside it
// ([4, 180], 176 tall), the block is centred in the band, and the size scale measures from it.
#define READER_HEADING_BLOCK_TOP 4
#define READER_HEADING_BLOCK_HEIGHT 176
// 章名合成加粗：覆盖率膨胀让墨迹向四周各多出约一像素，所以排版截短时先让出这一像素的宽度。
// The title is synthetically bolded with a coverage dilation that grows the ink by about one pixel
// on every side, so fitting yields that pixel of width before truncating.
#define READER_HEADING_BOLD_PX 1

#define READER_HEADING_TITLE_BYTES 128
#define READER_HEADING_LABEL_BYTES 32

/// 题头带同时供排版预留和绘制使用。/ Shared heading band for layout reservation and painting.
EpdRect reader_heading_band(int top);
/// 只绘图，不推屏、不修改设置；保留既有样式与缓冲上限。/ Paint only; no presentation or settings writes, retaining styles and buffer bounds.
void draw_reader_title_style(uint8_t *fb, EpdRect band, int style, int scale_px,
                              const char *label_sample, const char *title_sample);
