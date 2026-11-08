/*
 * 中文：显示方向与竖屏触点转换；仅 UI 任务改变方向，板级扫描尺寸不变。
 * English: Display orientation and portrait-touch conversion; only the UI task changes orientation, never scan dimensions.
 */
#pragma once
#include <stdint.h>

/// 正常竖屏为零，逻辑画面顺时针每次增加四分之一圈。/ Normal portrait is zero; each step turns the logical image clockwise by a quarter turn.
typedef enum {
    DISPLAY_ORIENTATION_PORTRAIT,
    DISPLAY_ORIENTATION_RIGHT,
    DISPLAY_ORIENTATION_INVERTED,
    DISPLAY_ORIENTATION_LEFT,
} display_orientation_t;

/// 读取/设置逻辑绘制方向，不推屏；调用方须先交还预绘并重建布局。
/// Get/set logical drawing orientation without presenting; callers first join prefetch and rebuild layout.
display_orientation_t display_orientation_get(void);
void display_orientation_set(display_orientation_t orientation);
/// 转换屏内竖屏触点；屏外实体触摸键的坐标原样保留。/ Convert on-panel portrait contacts, preserving off-panel cover-key coordinates.
void display_orientation_touch(uint16_t *x, uint16_t *y);
