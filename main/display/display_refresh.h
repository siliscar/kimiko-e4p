/*
 * 中文：页面只提交刷新意图、范围和画面属性；显示层统一选择波形与像素覆盖方式。
 * English: Pages submit intent, bounds and frame attributes; display owns waveform and pixel-coverage selection.
 */
#pragma once
#include "epdiy.h"

typedef struct EpdiyHighlevelState EpdiyHighlevelState;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_REFRESH_DETAIL,
    DISPLAY_REFRESH_LAYOUT,
    DISPLAY_REFRESH_SETTLE,
    DISPLAY_REFRESH_FEEDBACK,
    DISPLAY_REFRESH_MOTION,
    DISPLAY_REFRESH_DYNAMIC,
    DISPLAY_REFRESH_MAIN_CLOCK,
    DISPLAY_REFRESH_MAIN_DETAIL,
    DISPLAY_REFRESH_MAIN_SCROLL,
    DISPLAY_REFRESH_READER_TEXT,
    DISPLAY_REFRESH_READER_BODY,
    DISPLAY_REFRESH_READER_LAYOUT,
    DISPLAY_REFRESH_READER_IMAGE,
    DISPLAY_REFRESH_READER_CLEANUP,
    DISPLAY_REFRESH_READER_FOOTER,
    DISPLAY_REFRESH_OVERLAY,
    DISPLAY_REFRESH_RESTORE,
    DISPLAY_REFRESH_CAPSULE,
    DISPLAY_REFRESH_COUNT
} display_refresh_intent_t;

typedef enum {
    DISPLAY_REFRESH_DEFAULT = 0,
    DISPLAY_REFRESH_QUICK = 1u << 0, ///< 已有短刷新状态 / Existing short-refresh state
    DISPLAY_REFRESH_IMAGE = 1u << 1, ///< 当前帧含插图 / Current frame contains illustration
    DISPLAY_REFRESH_HOLD_GRAY = 1u << 2 ///< 归一时保护背景灰阶 / Protect background grays during normalization
} display_refresh_flags_t;

/// 同步呈现区域，保留原有错误码；不保存请求或另起补刷。/ Synchronously present bounds with original errors; never retain requests or schedule another pass.
enum EpdDrawError display_refresh_area(EpdiyHighlevelState *hl, display_refresh_intent_t intent,
                                       EpdRect area, unsigned flags);
/// 同步呈现整屏；整屏范围与全像素驱动分别由策略表达。/ Synchronously present the screen; screen bounds and full-pixel drive remain distinct.
enum EpdDrawError display_refresh_screen(EpdiyHighlevelState *hl, display_refresh_intent_t intent,
                                         unsigned flags);

#ifdef __cplusplus
}
#endif
