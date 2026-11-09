/* 中文：阅读设置绘制回归的显示类型与绘图替身接口。
 * English: Display types and drawing-double interfaces for reader-settings regressions.
 */
#pragma once
#include <stdint.h>
typedef struct { int x, y, width, height; } EpdRect;
enum EpdFontFlags { EPD_DRAW_ALIGN_LEFT, EPD_DRAW_ALIGN_CENTER, EPD_DRAW_ALIGN_RIGHT };
void epd_draw_circle(int x, int y, int r, uint8_t color, uint8_t *framebuffer);
void epd_fill_circle(int x, int y, int r, uint8_t color, uint8_t *framebuffer);
