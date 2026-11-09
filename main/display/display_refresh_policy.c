/*
 * 中文：集中原有场景的波形、模式与覆盖选择，保持原有局刷和定稿差异。
 * English: Centralize existing scene waveform, mode and coverage choices while retaining partial-update and settle distinctions.
 */
#include "display_refresh_policy.h"
#include "app_config.h"

bool display_refresh_resolve(display_refresh_intent_t intent, unsigned flags,
                             display_refresh_context_t context, display_refresh_plan_t *plan) {
    if (!plan || (unsigned)intent >= DISPLAY_REFRESH_COUNT ||
        flags & ~(DISPLAY_REFRESH_QUICK|DISPLAY_REFRESH_IMAGE|DISPLAY_REFRESH_HOLD_GRAY)) return false;
    unsigned allowed = DISPLAY_REFRESH_DEFAULT;
    if (intent == DISPLAY_REFRESH_READER_BODY) allowed = DISPLAY_REFRESH_QUICK | DISPLAY_REFRESH_IMAGE;
    if (intent == DISPLAY_REFRESH_READER_LAYOUT) allowed = DISPLAY_REFRESH_QUICK;
    if (intent == DISPLAY_REFRESH_RESTORE) allowed = DISPLAY_REFRESH_HOLD_GRAY;
    if (flags & ~allowed) return false;
    const bool quick = (flags & DISPLAY_REFRESH_QUICK) != 0;
    const display_waveform_id_t reader = flags & DISPLAY_REFRESH_IMAGE ? DISPLAY_WAVE_READER_IMAGE
        : context.reader_background ? DISPLAY_WAVE_READER_BACKGROUND : DISPLAY_WAVE_READER_INK;
    *plan = (display_refresh_plan_t){DISPLAY_WAVE_DEFAULT, MODE_GL16, DISPLAY_COVERAGE_DIFF, 0, 0};
    switch (intent) {
        case DISPLAY_REFRESH_DETAIL: break;
        case DISPLAY_REFRESH_LAYOUT: plan->coverage=DISPLAY_COVERAGE_AUTO; break;
        case DISPLAY_REFRESH_SETTLE:
        case DISPLAY_REFRESH_OVERLAY: plan->coverage=DISPLAY_COVERAGE_FULL; break;
        case DISPLAY_REFRESH_FEEDBACK: plan->waveform=DISPLAY_WAVE_FOLLOW; plan->mode=MODE_DU; break;
        case DISPLAY_REFRESH_MOTION:
        case DISPLAY_REFRESH_READER_FOOTER:
            plan->waveform=DISPLAY_WAVE_FOLLOW; plan->mode=MODE_DU; plan->coverage=DISPLAY_COVERAGE_AUTO; break;
        case DISPLAY_REFRESH_DYNAMIC:
            plan->mode=APP_DYNAMIC_REFRESH_MODE; plan->coverage=DISPLAY_COVERAGE_AUTO; break;
        case DISPLAY_REFRESH_MAIN_CLOCK:
            plan->waveform=context.main_fast ? DISPLAY_WAVE_MAIN_BW : DISPLAY_WAVE_MAIN_GRAY;
            plan->mode=context.main_fast ? MODE_DU : MODE_GL16; break;
        case DISPLAY_REFRESH_MAIN_DETAIL:
            plan->waveform=context.main_ordinary ? DISPLAY_WAVE_MAIN_NORMAL : DISPLAY_WAVE_MAIN_GRAY; break;
        case DISPLAY_REFRESH_MAIN_SCROLL:
            plan->waveform=context.main_ordinary ? DISPLAY_WAVE_MAIN_NORMAL : DISPLAY_WAVE_DEFAULT;
            plan->pad_lines=plan->pad_col_bytes=1; break;
        case DISPLAY_REFRESH_READER_TEXT: plan->waveform=DISPLAY_WAVE_READER_TEXT; break;
        case DISPLAY_REFRESH_READER_BODY:
            plan->waveform=reader; plan->mode=quick ? MODE_DU : MODE_GL16; break;
        case DISPLAY_REFRESH_READER_LAYOUT:
            plan->waveform=context.reader_background ? DISPLAY_WAVE_READER_BACKGROUND : DISPLAY_WAVE_READER_INK;
            plan->mode=quick ? MODE_DU : MODE_GL16; plan->coverage=DISPLAY_COVERAGE_AUTO; break;
        case DISPLAY_REFRESH_READER_IMAGE:
            plan->waveform=DISPLAY_WAVE_FULL; plan->mode=MODE_GC16; plan->coverage=DISPLAY_COVERAGE_AUTO; break;
        case DISPLAY_REFRESH_READER_CLEANUP:
            plan->waveform=DISPLAY_WAVE_FULL; plan->mode=MODE_GC16; plan->coverage=DISPLAY_COVERAGE_FULL; break;
        case DISPLAY_REFRESH_RESTORE:
            plan->waveform=flags & DISPLAY_REFRESH_HOLD_GRAY ? DISPLAY_WAVE_NORMALIZE_GRAY : DISPLAY_WAVE_NORMALIZE;
            plan->coverage=DISPLAY_COVERAGE_FULL; break;
        case DISPLAY_REFRESH_CAPSULE:
            // 黑白滑杆使用完整 DU，并擦除边界外扩区的细线残影。
            // Binary sliders use complete DU pulses and padded dirty edges to erase fine-line ghosts.
            plan->mode=MODE_DU;
            plan->pad_lines=plan->pad_col_bytes=1; break;
        case DISPLAY_REFRESH_COUNT: return false;
    }
    return true;
}
