/* 中文：调度测试只替换目录与设置，真实刷新策略与分发参与链接；意外出口立即失败。
 * English: Scheduler tests replace only catalog/settings, linking real policy/dispatch; unexpected exits fail immediately.
 */
#include <assert.h>
#include "display.h"
#include "display_refresh_policy.h"
#include "display_normalize.h"

const EpdWaveform *display_waveform_get(display_waveform_id_t id) {
    switch (id) {
        case DISPLAY_WAVE_DEFAULT: return (const EpdWaveform *)&E0470_WAVEFORM;
        case DISPLAY_WAVE_FOLLOW: return (const EpdWaveform *)&E0470_FOLLOW_WAVEFORM;
        case DISPLAY_WAVE_CAPSULE_DRAG: return (const EpdWaveform *)&E0470_CAPSULE_DRAG_WAVEFORM;
        case DISPLAY_WAVE_NORMALIZE: return display_normalize_waveform(false);
        case DISPLAY_WAVE_NORMALIZE_GRAY: return display_normalize_waveform(true);
        default: assert(!"unexpected waveform"); return NULL;
    }
}
display_refresh_context_t display_refresh_context(void) {
    return (display_refresh_context_t){0};
}
__attribute__((weak)) enum EpdDrawError update_display_area_with(EpdiyHighlevelState *h, const void *w, int m, EpdRect a) {
    (void)h;(void)w;(void)m;(void)a;assert(!"unexpected area output");return EPD_DRAW_INVALID_CROP;
}
enum EpdDrawError update_display_with(EpdiyHighlevelState *h, const void *w, int m) {
    (void)h;(void)w;(void)m;assert(!"unexpected screen output");return EPD_DRAW_INVALID_CROP;
}
enum EpdDrawError update_display_mode_diff_with(EpdiyHighlevelState *h, const void *w, int m) {
    (void)h;(void)w;(void)m;assert(!"unexpected screen diff");return EPD_DRAW_INVALID_CROP;
}
