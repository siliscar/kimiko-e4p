/*
 * 中文：在显示边界逐字段转换页面区域与后端区域，不改变裁剪或扫描规则。
 * English: Convert page and backend rectangles field by field at the display boundary without changing clipping or scanning.
 */
#pragma once
#include "display_types.h"
#include "epdiy.h"

/// 复制页面区域，保留零尺寸和负坐标。/ Copy a page rectangle, preserving zero sizes and negative coordinates.
static inline EpdRect display_rect_to_epd(display_rect_t area) {
    return (EpdRect){area.x, area.y, area.width, area.height};
}

/// 复制绘制层区域供页面回调返回。/ Copy a drawing-layer rectangle for a page callback result.
static inline display_rect_t display_rect_from_epd(EpdRect area) {
    return (display_rect_t){area.x, area.y, area.width, area.height};
}
