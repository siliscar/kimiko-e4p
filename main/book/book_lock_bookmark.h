/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：当前页面上的透明PNG书封；输入framebuffer即底图，不加载书籍。
 * English: Transparent PNG cover over the current framebuffer; never load a book for its background.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"
bool book_lock_bookmark_draw(uint8_t *fb,const char *png);
/// 设置页预览：可命中缓存但不回写 / Settings preview: may hit the cache but never rewrites it
bool book_lock_bookmark_preview(uint8_t *fb,const char *png);
/// 选图确认后预建图层缓存，第一次锁屏不再解码 / Prebuild the layer cache after a pick so the first lock never decodes
void book_lock_bookmark_prepare(const char *png);
/// 最近一次绘制是否命中TF卡图层缓存 / Whether the last draw hit the SD-card layer cache
bool book_lock_bookmark_cached(void);
void book_lock_bookmark_preview_paint(uint8_t *fb,const uint8_t *canvas,EpdRect area);
