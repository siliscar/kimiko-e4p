/*
 * 中文：把统一策略同步交给既有显示出口，保留 OTA 互斥、供电检查与失败恢复。
 * English: Submit central policy synchronously through existing display exits, retaining OTA exclusion, power checks and recovery.
 */
#include "display_refresh_policy.h"
#include <display.h>

enum EpdDrawError display_refresh_area(EpdiyHighlevelState *hl, display_refresh_intent_t intent,
                                       EpdRect area, unsigned flags) {
    display_refresh_plan_t plan;
    if (!display_refresh_resolve(intent, flags, display_refresh_context(), &plan)) return EPD_DRAW_INVALID_CROP;
    const EpdWaveform *wave = display_waveform_get(plan.waveform);
    if (!wave) return EPD_DRAW_NO_PHASES_AVAILABLE;
    if (plan.coverage == DISPLAY_COVERAGE_FULL)
        return update_display_area_full_with(hl,wave,plan.mode,area);
    if (plan.coverage == DISPLAY_COVERAGE_AUTO)
        return update_display_area_with(hl,wave,plan.mode,area);
    if (plan.pad_lines || plan.pad_col_bytes)
        return update_display_area_diff_padded(hl,wave,plan.mode,area,plan.pad_lines,plan.pad_col_bytes);
    return update_display_area_diff_with(hl,wave,plan.mode,area);
}

enum EpdDrawError display_refresh_screen(EpdiyHighlevelState *hl, display_refresh_intent_t intent,
                                         unsigned flags) {
    display_refresh_plan_t plan;
    if (!display_refresh_resolve(intent,flags,display_refresh_context(),&plan)) return EPD_DRAW_INVALID_CROP;
    if (plan.coverage==DISPLAY_COVERAGE_FULL || plan.pad_lines || plan.pad_col_bytes)
        return EPD_DRAW_INVALID_CROP;
    const EpdWaveform *wave=display_waveform_get(plan.waveform);
    if (!wave) return EPD_DRAW_NO_PHASES_AVAILABLE;
    return plan.coverage==DISPLAY_COVERAGE_DIFF
        ? update_display_mode_diff_with(hl,wave,plan.mode) : update_display_with(hl,wave,plan.mode);
}
