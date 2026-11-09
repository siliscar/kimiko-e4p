/*
 * 中文：记录真实呈现路由的显示调用，检查区域、波形、错误传播和水波纹节拍恢复。
 * English: Record real presentation calls to check regions, waveforms, error propagation and restored ripple pacing.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reader_present.h"
#include "display_refresh_policy.h"
#include "display.h"
#include "display_reader_waveform.h"
#include "display_normalize.h"
#include "e0470_epaper_waveform.h"

const EpdWaveform E0470_WAVEFORM = {1}, E0470_FULL_WAVEFORM = {2},
    E0470_FOLLOW_WAVEFORM = {3};
static unsigned present_depth, completed_presentations;
bool display_present_begin(void) { ++present_depth; return true; }
void display_present_end(bool held) { assert(held && present_depth == 1); --present_depth; ++completed_presentations; }

static const EpdWaveform text = {4}, page = {5}, image = {6},
    normalize = {7}, normalize_gray = {8}, background = {9};
static bool use_background;
const EpdWaveform* display_reader_text_waveform(void) { return &text; }
const EpdWaveform* display_reader_page_waveform(void) { return &page; }
const EpdWaveform* display_reader_image_waveform(void) { return &image; }
const EpdWaveform* display_normalize_waveform(bool gray) {
    return gray ? &normalize_gray : &normalize;
}
const EpdWaveform *display_waveform_get(display_waveform_id_t id) {
    switch(id) {
        case DISPLAY_WAVE_DEFAULT: return &E0470_WAVEFORM;
        case DISPLAY_WAVE_FULL: return &E0470_FULL_WAVEFORM;
        case DISPLAY_WAVE_FOLLOW: return &E0470_FOLLOW_WAVEFORM;
        case DISPLAY_WAVE_READER_TEXT: return &text;
        case DISPLAY_WAVE_READER_INK: return &page;
        case DISPLAY_WAVE_READER_BACKGROUND: return &background;
        case DISPLAY_WAVE_READER_IMAGE: return &image;
        case DISPLAY_WAVE_NORMALIZE: return &normalize;
        case DISPLAY_WAVE_NORMALIZE_GRAY: return &normalize_gray;
        default: assert(!"unexpected waveform"); return NULL;
    }
}
display_refresh_context_t display_refresh_context(void) {
    return (display_refresh_context_t){.reader_background = use_background};
}
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState *s, const EpdWaveform *w,
                                                 enum EpdDrawMode m, EpdRect a, int x, int y) {
    (void)s;(void)w;(void)m;(void)a;(void)x;(void)y;assert(!"unexpected padded output");return EPD_DRAW_INVALID_CROP;
}
enum call_kind { AREA, DIFF, FULL_AREA, WHOLE, WHOLE_DIFF, FULL, FAST, WATER, TICK };
typedef struct {
    enum call_kind kind;
    const EpdWaveform *wave;
    int mode;
    EpdRect area;
    int value;
} call_t;
static call_t calls[16];
static size_t count;
static enum EpdDrawError errors[16];
static size_t outputs;
static int tick;
static uint8_t front[4] = {1, 2, 3, 4}, back[4] = {4, 3, 2, 1};
static EpdiyHighlevelState hl = {.front_fb = front, .back_fb = back};
static const EpdRect none = {0};
static enum EpdDrawError record(EpdiyHighlevelState *state, enum call_kind kind,
                                const EpdWaveform *wave, int mode, EpdRect area) {
    assert(state == &hl && state->front_fb == front && state->back_fb == back);
    assert(present_depth == 1);
    assert(count < 16 && outputs < 16);
    calls[count++] = (call_t){kind, wave, mode, area, tick};
    return errors[outputs++];
}
#define AREA_OUTPUT(name, kind) \
enum EpdDrawError name(EpdiyHighlevelState *state, const EpdWaveform *wave, \
                      enum EpdDrawMode mode, EpdRect area) { \
    return record(state, kind, wave, mode, area); \
}
AREA_OUTPUT(update_display_area_with, AREA)
AREA_OUTPUT(update_display_area_diff_with, DIFF)
AREA_OUTPUT(update_display_area_full_with, FULL_AREA)
enum EpdDrawError update_display_with(EpdiyHighlevelState *s, const EpdWaveform *w, enum EpdDrawMode m) {
    return record(s, WHOLE, w, m, none);
}
enum EpdDrawError update_display_mode_diff_with(EpdiyHighlevelState *s, const EpdWaveform *w, enum EpdDrawMode m) {
    return record(s, WHOLE_DIFF, w, m, none);
}
enum EpdDrawError update_display_full(EpdiyHighlevelState *s) { return record(s, FULL, NULL, 0, none); }
enum EpdDrawError update_display_fast_page(EpdiyHighlevelState *s) { return record(s, FAST, NULL, 0, none); }
enum EpdDrawError update_display_water_turn(EpdiyHighlevelState *s, EpdRect a, e0470_turn_dir_t d) {
    return record(s, WATER, NULL, d, a);
}
int e0470_page_turn_tick_us(void) { return tick; }
void e0470_page_turn_set_tick_us(int value) {
    assert(count < 16);
    calls[count++] = (call_t){.kind = TICK, .value = value};
    tick = value;
}
static reader_present_request_t reset(void) {
    count = outputs = 0;
    memset(errors, 0, sizeof(errors));
    tick = 13579;
    use_background = false;
    return (reader_present_request_t){
        .refresh = READER_PRESENT_AREA, .mode = MODE_GL16, .page_mode = MODE_DU,
        .status_area = {0, 0, 684, 160}, .area = {1, 2, 3, 4}, .body_area = {5, 6, 7, 8}, .footer_area = {9, 10, 11, 12},
        .progress_area = {13, 14, 15, 16}, .tools_area = {17, 18, 19, 20},
        .screen_area = {0, 0, 684, 1216}, .water_dir = E0470_TURN_BTT,
        .reading = true, .reader_body = true, .prior_reader = true, .turn_pending = true,
    };
}
static void expect(size_t i, enum call_kind kind, const EpdWaveform *wave, int mode, EpdRect area) {
    assert(i < count);
    assert(calls[i].kind == kind && calls[i].wave == wave && calls[i].mode == mode);
    assert(memcmp(&calls[i].area, &area, sizeof(area)) == 0);
}
int main(void) {
    // 正文必须先输出，再用旧短 DU 更新普通页脚。/ Present the body before the legacy short-DU footer.
    reader_present_request_t probe = reset(); probe.footer_pending = true;
    reader_present_result_t probe_out = reader_present_output(&hl, &probe);
    assert(probe_out.error == EPD_DRAW_SUCCESS && probe_out.footer_updated && count == 2);
    expect(0, DIFF, &text, MODE_GL16, probe.area);
    expect(1, AREA, &E0470_FOLLOW_WAVEFORM, MODE_DU, probe.footer_area);
    probe = reset(); probe.footer_pending = probe.footer_strong = probe.status_pending = true;
    probe_out = reader_present_output(&hl, &probe);
    assert(probe_out.error == EPD_DRAW_SUCCESS && probe_out.footer_updated && probe_out.status_updated && count == 3);
    expect(0, DIFF, &text, MODE_GL16, probe.area);
    expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, probe.footer_area);
    expect(2, DIFF, &E0470_WAVEFORM, MODE_GL16, probe.status_area);
    reader_present_request_t r = reset();
    reader_present_result_t out = reader_present_output(&hl, &r);
    assert(out.error == 0 && !out.tools_band && !out.footer_updated && count == 1);
    expect(0, DIFF, &text, MODE_GL16, r.area);
    r = reset(); r.fullscreen = true;
    reader_present_output(&hl, &r);
    assert(count == 2); expect(0, DIFF, &text, MODE_GL16, r.area);
    expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, r.progress_area);
    r = reset(); r.turn_pending = false; r.fullscreen = true;
    reader_present_output(&hl, &r); expect(0, DIFF, &page, MODE_GL16, r.area);
    r = reset(); r.turn_pending = false;
    reader_present_output(&hl, &r); expect(0, AREA, &page, MODE_GL16, r.area);
    r = reset(); r.mode = MODE_DU; r.fullscreen = true;
    reader_present_output(&hl, &r); expect(0, AREA, &page, MODE_DU, r.area);

    // 正文先输出，随后页脚或进度，顶部最后；背景、状态、全屏及两种效果共用顺序。
    // Body precedes footer or progress, with status last, across backgrounds, status changes, full screen and both effects.
    for (int bg = 0; bg < 2; ++bg) for (int full = 0; full < 2; ++full)
    for (int water = 0; water < 2; ++water) for (int status = 0; status < 2; ++status)
    for (int strong = 0; strong < 2; ++strong) {
        r = reset(); use_background = bg;
        r.fullscreen = full; r.water_turn_pending = water;
        r.status_pending = status; r.footer_pending = !full;
        r.footer_strong = strong;
        out = reader_present_output(&hl, &r);
        assert(out.error == 0 && out.status_updated == !!status && out.footer_updated == !full);
        size_t i = 0;
        if (water) {
            assert(calls[i++].kind == TICK);
            expect(i++, WATER, NULL, E0470_TURN_BTT, r.area);
            assert(calls[i++].kind == TICK && tick == 13579);
        } else expect(i++, DIFF, &text, MODE_GL16, r.area);
        if (full) expect(i++, DIFF, &E0470_WAVEFORM, MODE_GL16, r.progress_area);
        else if (strong) expect(i++, DIFF, &E0470_WAVEFORM, MODE_GL16, r.footer_area);
        else expect(i++, AREA, &E0470_FOLLOW_WAVEFORM, MODE_DU, r.footer_area);
        if (status) expect(i++, DIFF, &E0470_WAVEFORM, MODE_GL16, r.status_area);
        assert(i == count);
    }
    // 正文失败不刷顶底栏；所有失败都短路并保留错误，水波纹始终恢复节拍。
    // Body errors skip chrome; all failures stop later output and propagate, and ripple always restores pacing.
    for (int water = 0; water < 2; ++water) for (int failure = 0; failure < 4; ++failure) {
        r = reset(); r.status_pending = r.footer_pending = r.fullscreen = true;
        r.water_turn_pending = water;
        errors[failure] = EPD_DRAW_OTHER_ERROR;
        out = reader_present_output(&hl, &r);
        assert(out.error == EPD_DRAW_OTHER_ERROR && outputs == (size_t)failure + 1);
        assert(!out.status_updated && out.footer_updated == (failure > 1));
        assert(tick == 13579 && count == outputs + (water ? 2u : 0u));
    }
    // 插图正文也优先输出，保留原完整灰阶或插图差分出口。
    // Illustration bodies also present first while retaining full-gray or image-differential output.
    for (int bg = 0; bg < 2; ++bg) for (int water = 0; water < 2; ++water)
    for (int failure = -1; failure < 4; ++failure) {
        r = reset(); use_background = bg;
        r.image_gray_refresh = r.image_page = r.fullscreen = r.footer_pending = r.status_pending = true;
        r.water_effect = water;
        if (failure >= 0) errors[failure] = EPD_DRAW_OTHER_ERROR;
        out = reader_present_output(&hl, &r);
        assert(count == (failure < 0 ? 4u : (size_t)failure + 1));
        assert(out.error == (failure < 0 ? 0 : EPD_DRAW_OTHER_ERROR));
        assert(out.status_updated == (failure < 0));
        assert(out.footer_updated == (failure < 0 || failure > 1));
        expect(0, water ? DIFF : AREA, water ? &image : &E0470_FULL_WAVEFORM,
               water ? MODE_GL16 : MODE_GC16, r.body_area);
        if (count > 1) expect(1, DIFF, &E0470_WAVEFORM, MODE_GL16, r.footer_area);
        if (count > 2) expect(2, DIFF, &E0470_WAVEFORM, MODE_GL16, r.progress_area);
        if (count > 3) expect(3, DIFF, &E0470_WAVEFORM, MODE_GL16, r.status_area);
    }
    r = reset(); r.image_gray_refresh = r.image_page = r.water_effect = true; r.prior_reader = false;
    reader_present_output(&hl, &r); assert(count == 1); expect(0, WHOLE_DIFF, &image, MODE_GL16, none);
    r.cleanup = true; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, WHOLE, &E0470_FULL_WAVEFORM, MODE_GC16, none);
    r.cleanup = false; r.refresh = READER_PRESENT_FULL; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, WHOLE, &E0470_FULL_WAVEFORM, MODE_GC16, none);
    r = reset(); r.status_pending = r.footer_pending = r.cleanup = r.tools_toggle = true; r.refresh = READER_PRESENT_PAGE;
    reader_present_output(&hl, &r); assert(count == 1); expect(0, FULL_AREA, &E0470_FULL_WAVEFORM, MODE_GC16, r.screen_area);
    r.refresh = READER_PRESENT_FULL; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, FULL, NULL, 0, none);

    r = reset(); r.status_pending = r.footer_pending = r.tools_toggle = true; r.refresh = READER_PRESENT_PAGE;
    out = reader_present_output(&hl, &r);
    assert(count == 1 && out.tools_band); expect(0, DIFF, &page, MODE_GL16, r.tools_area);
    for (int gray = 0; gray < 2; ++gray) {
        r = reset(); r.refresh = READER_PRESENT_PAGE; r.hold_gray = gray;
        r.tools_toggle = r.closing_tools_to_text = r.closing_panel_to_text = true;
        out = reader_present_output(&hl, &r);
        assert(count == 1 && !out.tools_band);
        expect(0, FULL_AREA, gray ? &normalize_gray : &normalize, MODE_GL16, r.screen_area);
    }
    r = reset(); r.refresh = READER_PRESENT_PAGE; r.image_page = true;
    reader_present_output(&hl, &r); expect(0, WHOLE_DIFF, &image, MODE_DU, none);
    r = reset(); r.refresh = READER_PRESENT_PAGE;
    reader_present_output(&hl, &r); expect(0, WHOLE_DIFF, &page, MODE_DU, none);
    r.reading = false; count = outputs = 0;
    reader_present_output(&hl, &r); expect(0, FULL, NULL, 0, none);
    count = outputs = 0; errors[0] = EPD_DRAW_OTHER_ERROR;
    assert(reader_present_output(&hl, &r).error == EPD_DRAW_OTHER_ERROR);
    count = outputs = 0; errors[0] = EPD_DRAW_SUCCESS; r.prior_reader = false;
    reader_present_output(&hl, &r); expect(0, FAST, NULL, 0, none);
    for (int du = 0; du < 2; ++du) {
        r = reset(); r.reading = false; r.feedback = true; r.mode = du ? MODE_DU : MODE_GL16;
        r.status_pending = r.water_turn_pending = r.fullscreen = r.footer_pending = true;
        reader_present_output(&hl, &r);
        assert(count == 1 && tick == 13579);
        expect(0, DIFF, du ? &E0470_FOLLOW_WAVEFORM : &E0470_WAVEFORM, r.mode, r.area);
    }
    assert(front[0] == 1 && back[0] == 4);
    assert(!present_depth && completed_presentations);
    puts("reader present routing: PASS");
    return 0;
}
