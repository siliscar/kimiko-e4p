/*
 * 中文：页面借用的不透明显示状态与逻辑区域，不引入板级或屏幕驱动头。
 * English: Opaque borrowed display state and logical rectangles for pages, without board or screen driver headers.
 */
#pragma once

/// 显示服务持有状态；页面只借用，不能释放或检查帧缓冲内部字段。
/// The display service owns this state; pages borrow it without freeing it or inspecting framebuffer internals.
typedef struct EpdiyHighlevelState display_state_t;

/// 逻辑屏幕坐标，保持调用方给定的符号与范围，不隐式裁剪。
/// Logical screen coordinates retain the caller's signs and ranges without implicit clipping.
typedef struct {
    int x; ///< 横向起点 / Horizontal origin
    int y; ///< 纵向起点 / Vertical origin
    int width; ///< 宽度 / Width
    int height; ///< 高度 / Height
} display_rect_t;
