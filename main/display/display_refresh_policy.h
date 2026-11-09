/*
 * 中文：显示层内部的纯策略结果；画面属性显式输入，测试无需设备或全局设置。
 * English: Display-internal pure policy results; explicit frame attributes need no device or global settings in tests.
 */
#pragma once
#include "display_refresh.h"
#include "display_waveform_catalog.h"

typedef enum { DISPLAY_COVERAGE_AUTO, DISPLAY_COVERAGE_DIFF, DISPLAY_COVERAGE_FULL } display_coverage_t;
typedef struct {
    bool main_ordinary; ///< 主页普通模式 / Ordinary main mode
    bool main_fast; ///< 主页快刷模式 / Fast main mode
    bool reader_background; ///< 当前阅读背景设置 / Current reader-background setting
} display_refresh_context_t;
typedef struct {
    display_waveform_id_t waveform; ///< 统一目录编号 / Catalog ID
    int mode; ///< 后端驱动模式 / Backend drive mode
    display_coverage_t coverage; ///< 像素覆盖 / Pixel coverage
    int pad_lines; ///< 显式脏行膨胀 / Explicit dirty-row padding
    int pad_col_bytes; ///< 显式脏列字节膨胀 / Explicit dirty-column-byte padding
} display_refresh_plan_t;

/// 纯解析；无效意图或未知标志拒绝输出。/ Pure resolution; reject invalid intents or unknown flags.
bool display_refresh_resolve(display_refresh_intent_t intent, unsigned flags,
                             display_refresh_context_t context, display_refresh_plan_t *plan);
/// 只读取现有设置，不构造波形或修改状态。/ Read existing settings without constructing waveforms or mutating state.
display_refresh_context_t display_refresh_context(void);
