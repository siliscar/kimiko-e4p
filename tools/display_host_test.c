/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 真实 display.c 欠载恢复回归；仅模拟硬件与高层 framebuffer 边界。
 * Regression for real display.c underrun recovery, mocking hardware and high-level framebuffer boundaries only.
 * 冻结：目标画面不得丢失；白色基准只作用于后缓冲。
 * Frozen: Preserve the requested picture; the white baseline affects only the back buffer.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "display.h"
#include "app_config.h"
#include "settings.h"
#include "e0470_epaper_waveform.h"
app_main_refresh_mode_t app_settings_main_refresh_mode(void) {return APP_MAIN_REFRESH_NORMAL;}
// 所有真实推屏和欠载恢复都必须持有 OTA 互斥锁，包括递归兜底。
// Every physical scan and underrun recovery must hold OTA exclusion, including recursive fallback.
static unsigned ota_display_depth;
bool pico_online_display_begin(void) { ++ota_display_depth; return true; }
void pico_online_display_end(bool held) { assert(held && ota_display_depth); --ota_display_depth; }
void e0470_page_turn_release(void) {}
static int turn_tick_us = 21000;
void e0470_page_turn_set_tick_us(int us) { turn_tick_us = us; }
int e0470_page_turn_tick_us(void) { return turn_tick_us; }
bool app_settings_main_fast_refresh(void) {return false;}
bool frontlight_on(void) {return false;}
int epd_width(void) {return 1216;}
int epd_height(void) {return 684;}
enum EpdRotation epd_get_rotation(void) {return EPD_ROT_INVERTED_PORTRAIT;}
void* heap_caps_aligned_alloc(size_t a,size_t b,unsigned c) {(void)a;(void)b;(void)c;assert(false);return NULL;}
void heap_caps_free(void* p) {(void)p;assert(false);}
const EpdWaveform* display_main_gray_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_shelf_exit_waveform(void) {return &E0470_WAVEFORM;}
// 本组不注册灰阶内容带；若误入分区呈现，立即失败。/ This suite registers no gray bands; fail on an unexpected regional presentation.
const EpdWaveform* display_main_shelf_waveform(enum EpdDrawMode mode) {
    (void)mode; assert(false); return NULL;
}
enum EpdDrawError e0470_update_regions(EpdiyHighlevelState* hl, EpdRect area,
    EpdRect strong_area, const EpdWaveform* soft_waveform,
    const EpdWaveform* strong_waveform, enum EpdDrawMode strong_mode) {
    (void)hl; (void)area; (void)strong_area; (void)soft_waveform;
    (void)strong_waveform; (void)strong_mode;
    assert(false); return EPD_DRAW_OTHER_ERROR;
}
static const EpdWaveform soft_gray = {4};
static const EpdWaveform reader_soft = {5};
static const EpdWaveform reader_water = {6};
const EpdWaveform* display_reader_text_waveform(void) {return &reader_soft;}
const EpdWaveform* display_reader_water_waveform(void) {return &reader_water;}
const EpdWaveform* display_main_water_waveform(void) {return &soft_gray;}
const EpdWaveform* display_main_normal_waveform(void) { return display_main_water_waveform(); }
const EpdWaveform* display_main_bw_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_acrylic_waveform(void) {return &E0470_WAVEFORM;}
const EpdWaveform* display_main_acrylic_clean_waveform(void) {return &E0470_WAVEFORM;}
int epd_rotated_display_width(void) { return 684; }
int epd_rotated_display_height(void) { return 1216; }

#define FB_BYTES 128
const EpdWaveform E0470_WAVEFORM = {0}, E0470_FOLLOW_WAVEFORM = {1}, E0470_GRAY8_WAVEFORM = {2}, E0470_FULL_WAVEFORM = {3},
    E0470_CAPSULE_DRAG_WAVEFORM = {4}, E0470_CAPSULE_WHITE_WAVEFORM = {5},
    E0470_CAPSULE_GRAY_WAVEFORM = {6}, E0470_APPLY_WAVEFORM = {7};
static uint8_t target[FB_BYTES], presented[FB_BYTES];
static int clocks, powerons, clears, draws, full_draws, safe_clock, prefill, last_mode, last_scan;
static bool white_baseline, correct_target_at_draw;
static int scan_at_draw, prefill_at_draw;
static enum EpdDrawError next_draw_error;
static unsigned draw_errors_remaining;
static unsigned blind_pushes, repair_delays, poweroffs;
static bool checking_repair;
static bool drive_active, no_change;
static bool power_failure;
static unsigned drive_releases, release_failures;
static int64_t now_us = 1000000;
static const EpdWaveform *waveform_at_draw;
static enum EpdDrawError water_result;
static int water_calls;
static EpdRect last_area;

enum EpdDrawError e0470_page_turn(EpdiyHighlevelState* hl,EpdRect area,e0470_turn_dir_t dir) {
    (void)hl;(void)area;(void)dir;
    assert(false);return EPD_DRAW_OTHER_ERROR;
}
enum EpdDrawError e0470_page_turn_with_waveform_compact(EpdiyHighlevelState* hl,EpdRect area,
    e0470_turn_dir_t dir,const EpdWaveform* waveform,unsigned bands) {
    assert(ota_display_depth);
    assert(hl&&area.width>0&&dir==E0470_TURN_RTL);
    assert(waveform==&reader_water&&bands==E0470_TURN_FAST_BANDS);
    ++water_calls;assert(last_scan==BOARD_EPD_SCAN_FAST);
    return water_result;
}

void board_epd_set_pclk(int mhz) { ++clocks; safe_clock = mhz; }
void board_epd_use_scan(board_epd_scan_t scan) { last_scan = scan; }
void epd_lcd_set_prefill_lines(int lines) { prefill = lines; }
void epd_poweron(void) { ++powerons; drive_active = false; }
bool board_rails_on(void) { return !power_failure; }
bool epd_power_scan_allowed(void) { return board_rails_on(); }
static unsigned power_session_depth;
static bool actual_power;
void epd_power_session_begin(void) { ++power_session_depth; }
void epd_power_session_end(void) { assert(power_session_depth); if (!--power_session_depth) actual_power = false; }
static void physical_scan_power(void) { assert(power_session_depth); if (!actual_power) { epd_poweron(); actual_power = true; } }

bool board_epd_release_drive(void) {
    assert(ota_display_depth);
    ++drive_releases;
    if (release_failures) { --release_failures; return false; }
    drive_active = false;
    return true;
}
void epd_poweroff(void) { if (checking_repair) assert(ota_display_depth); ++poweroffs; actual_power = false; drive_active = false; }
EpdRect epd_full_screen(void) { return (EpdRect){0,0,1216,684}; }
void vTaskDelay(uint32_t ticks) {
    assert(checking_repair && ota_display_depth);
    assert((repair_delays == 0 && ticks == 400) || (repair_delays > 0 && ticks == 1));
    ++repair_delays;
}
void epd_push_pixels(EpdRect area, short time, int color) {
    physical_scan_power();
    assert(checking_repair && ota_display_depth && poweroffs && powerons);
    assert(area.x == 0 && area.y == 0 && area.width == 1216 && area.height == 684 && time == 15);
    const unsigned phase = blind_pushes % 26;
    assert(color == (phase < 10 ? 0 : phase < 23 ? 1 : 2));
    ++blind_pushes;
}
void epd_clear(void) { physical_scan_power(); assert(ota_display_depth); assert(powerons > 0); ++clears; }
int64_t esp_timer_get_time(void) { return now_us; }

// 与 highlevel.c:307 相同：该 API 清前缓冲，而不是参考后缓冲。
// Match highlevel.c:307: this API clears the front buffer, not the reference back buffer.
void epd_hl_set_all_white(EpdiyHighlevelState* hl) { memset(hl->front_fb, 255, FB_BYTES); }
void epd_hl_waveform(EpdiyHighlevelState* hl, const EpdWaveform* waveform) { hl->waveform = waveform; }
static enum EpdDrawError draw(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, bool full) {
    assert(ota_display_depth);
    assert((mode == MODE_DU || mode == MODE_GL16 || mode == MODE_GC16) && temperature == 25);
    if (no_change && !full) {
        assert(!memcmp(hl->front_fb, hl->back_fb, FB_BYTES));
        return EPD_DRAW_SUCCESS;
    }
    physical_scan_power();
    ++draws;
    full_draws += full;
    last_mode = mode;
    waveform_at_draw = hl->waveform;
    scan_at_draw = last_scan; prefill_at_draw = prefill;
    white_baseline = true;
    for (size_t i = 0; i < FB_BYTES; ++i) if (hl->back_fb[i] != 255) white_baseline = false;
    correct_target_at_draw = memcmp(hl->front_fb, target, FB_BYTES) == 0;
    memcpy(presented, hl->front_fb, FB_BYTES);
    enum EpdDrawError err = next_draw_error;
    if (draw_errors_remaining > 1) --draw_errors_remaining;
    else { draw_errors_remaining = 0; next_draw_error = EPD_DRAW_SUCCESS; }
    if (err == EPD_DRAW_SUCCESS) memcpy(hl->back_fb, hl->front_fb, FB_BYTES);
    return err;
}
enum EpdDrawError epd_hl_update_screen(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    return draw(hl, mode, temperature, false);
}
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    return draw(hl, mode, temperature, true);
}
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area) {
    (void)area;
    return draw(hl, mode, temperature, false);
}
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area) {
    last_area = area;
    return draw(hl, mode, temperature, true);
}
// 测试桩不模拟脏区膨胀，按普通差分处理。
// The stub does not model dirty-mask dilation and treats both as ordinary differentials.
enum EpdDrawError epd_hl_update_area_padded(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, EpdRect area, int pad_lines, int pad_col_bytes) {
    (void)pad_lines; (void)pad_col_bytes;
    return epd_hl_update_area(hl, mode, temperature, area);
}
enum EpdDrawError epd_hl_update_screen_padded(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature, int pad_lines, int pad_col_bytes) {
    (void)pad_lines; (void)pad_col_bytes;
    return draw(hl, mode, temperature, false);
}
// 与 highlevel.c:142 相同：白后缓冲后，强制整屏推目标前缓冲。
// Match highlevel.c:142: whiten the back buffer, then force a full update from the target front buffer.
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState* hl, enum EpdDrawMode mode, int temperature) {
    memset(hl->back_fb, 255, FB_BYTES);
    return epd_hl_update_screen_full(hl, mode, temperature);
}

static enum EpdDrawError power_fault_present(EpdiyHighlevelState *hl, unsigned path) {
    EpdRect area = {0, 160, 16, 16};
    switch (path) {
        case 0: return update_display_mode(hl, MODE_GL16);
        case 1: return update_display_mode_diff(hl, MODE_GL16);
        case 2: return update_display_mode_diff_with(hl, &E0470_WAVEFORM, MODE_GL16);
        case 3: return update_display_fast_page(hl);
        case 4: return update_display_from_white_with(hl, &E0470_WAVEFORM, MODE_GC16);
        case 5: return update_display_from_white(hl);
        case 6: return update_display_white(hl);
        case 7: return update_display_full(hl);
        case 8: return update_display_image_gray(hl);
        case 9: return update_display_with(hl, &E0470_WAVEFORM, MODE_GL16);
        case 10: return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 11: return update_display_area_diff_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 12: return update_display_area_diff_padded(hl, &E0470_WAVEFORM, MODE_DU, area, 1, 1);
        case 13: return update_display_area_full_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
        case 14: return update_display_click_feedback(hl, area);
        case 15: return update_display_water_turn(hl, area, E0470_TURN_RTL);
        default: return display_panel_repair(hl);
    }
}

int main(void) {
    uint8_t front[FB_BYTES], back[FB_BYTES];
    for (size_t i = 0; i < FB_BYTES; ++i) target[i] = (uint8_t)(i * 37U + 3U);
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    EpdiyHighlevelState hl = {.front_fb = front, .back_fb = back, .waveform = &E0470_WAVEFORM};
    // 局部控件刷新不推进整页清残影计数；第 40 次整页差分才升级为 GC16。
    // Local control updates do not advance cleanup; only the 40th whole-page differential promotes to GC16.
    clears = draws = full_draws = last_mode = 0;
    for (int i = 0; i < 80; ++i)
        assert(update_display_area_with(&hl, &E0470_WAVEFORM, MODE_GL16, (EpdRect){0, 0, 16, 16}) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GL16);
    // 反复局部差分不强驱动未变像素，也不累积整页 GC16 计数。
    // Repeated local differentials neither force unchanged pixels nor advance whole-page GC16 cleanup.
    int locals_full = full_draws;
    for (int i = 0; i < 200; ++i)
        assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){0, 160, 684, 936}) == EPD_DRAW_SUCCESS);
    assert(full_draws == locals_full && last_mode == MODE_GL16);
    // 词栏停顿整理必须驱动未变化的白底；局部 GL16 不清整屏、不升级 GC16。
    // Idle word-strip settling drives unchanged white pixels without screen clears or GC16 promotion.
    for (int i = 0; i < 80; ++i) {
        int before_full = full_draws, before_draws = draws;
        assert(update_display_area_full_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){24, 612, 636, 114}) == EPD_DRAW_SUCCESS);
        assert(full_draws == before_full + 1 && draws == before_draws + 1 && !clears);
        assert(last_area.x == 24 && last_area.y == 612 && last_area.width == 636 && last_area.height == 114);
        assert(last_mode == MODE_GL16 && waveform_at_draw == &E0470_WAVEFORM && correct_target_at_draw);
    }
    for (int i = 0; i < APP_UI_GC16_EVERY - 1; ++i) {
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(last_mode == MODE_GL16);
    }
    assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GC16);

    // 普通子页面一次输出柔和灰阶，第三次不驱动整页，达到原阈值仍用原表GC16。
    // Ordinary subpages use one soft-gray difference without third-page full output, retaining original-table GC16 at the threshold.
    for (int i = 0; i < APP_UI_FAST_GC16_EVERY - 1; ++i) {
        int prior_full = full_draws;
        assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
        assert(last_mode == MODE_GL16 && last_scan == BOARD_EPD_SCAN_FULL);
        assert(waveform_at_draw == &soft_gray && hl.waveform == &E0470_WAVEFORM);
        assert(full_draws == prior_full);
    }
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_WAVEFORM && last_scan == BOARD_EPD_SCAN_FULL && full_draws > 0);

    // Image pages clear the optical state and use the full grayscale waveform.
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    clears = draws = 0;
    assert(update_display_image_gray(&hl) == EPD_DRAW_SUCCESS);
    assert(clears == 1 && draws == 1 && white_baseline && correct_target_at_draw);
    assert(last_mode == MODE_GC16 && waveform_at_draw == &E0470_FULL_WAVEFORM &&
           hl.waveform == &E0470_WAVEFORM);

    // 撤回跟手滚动后，解锁的下一帧仍须全刷一次，随后恢复局部差分。
    // Removing follow scrolling must retain exactly one full update on the next wake frame, then resume local differences.
    int before_wake_full = full_draws;
    display_request_full_refresh();
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,160,684,928}) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 1 && last_mode == MODE_GC16);
    assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                        (EpdRect){0,160,684,928}) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 1 && last_mode == MODE_GL16);
    display_request_full_refresh();
    assert(update_display_fast_page(&hl) == EPD_DRAW_SUCCESS);
    assert(full_draws == before_wake_full + 2 && last_mode == MODE_GC16);

    clocks = powerons = clears = draws = full_draws = last_mode = 0;
    guard_draw_result(&hl, EPD_DRAW_SUCCESS);
    guard_draw_result(&hl, EPD_DRAW_OTHER_ERROR);
    assert(!clocks && !clears && !draws && !memcmp(front, target, FB_BYTES));
    for (int bulk = 0; bulk < 2; ++bulk) {
        memcpy(front, target, FB_BYTES);
        memset(back, 0x55, FB_BYTES);
        clocks = powerons = clears = draws = full_draws = last_mode = 0;
        display_set_bulk_io(bulk != 0);
        guard_draw_result(&hl, EPD_DRAW_EMPTY_LINE_QUEUE | EPD_DRAW_OTHER_ERROR);
        if (memcmp(front, target, FB_BYTES)) {
            fputs("FAIL: underrun recovery erased target front_fb (white screen regression)\n", stderr);
            return 1;
        }
        assert(correct_target_at_draw && !memcmp(presented, target, FB_BYTES));
        assert(white_baseline && full_draws == 1 && draws == 1);
        assert(!memcmp(back, target, FB_BYTES));
        assert(clocks == 1 && safe_clock == DISPLAY_PCLK_SAFE_MHZ && display_pclk_mhz() == DISPLAY_PCLK_SAFE_MHZ);
        assert(powerons == 1 && clears == 1 && prefill == (bulk ? 127 : 32));
    }
    // 缺波形直接沿用 GL16；扫描中断则从白底重建整屏目标，不把半途像素当作已完成。
    // Missing phases use GL16; an interrupted scan rebuilds the target from white.
    display_set_bulk_io(false);
    memcpy(front, target, FB_BYTES);
    memset(back, 0x55, FB_BYTES);
    water_calls = draws = clears = 0;
    water_result = EPD_DRAW_SUCCESS;
    assert(update_display_water_turn(&hl,(EpdRect){0,0,16,16},E0470_TURN_RTL)==EPD_DRAW_SUCCESS);
    assert(water_calls==1&&!draws&&!clears&&last_scan==BOARD_EPD_SCAN_FULL);
    assert(turn_tick_us==21000&&!ota_display_depth);
    water_calls=0;
    water_result = EPD_DRAW_NO_PHASES_AVAILABLE;
    assert(update_display_water_turn(&hl, (EpdRect){0, 0, 16, 16}, E0470_TURN_RTL) == EPD_DRAW_SUCCESS);
    assert(water_calls == 1 && draws == 1 && !clears && last_mode == MODE_GL16);
    memset(back, 0x55, FB_BYTES);
    water_result = EPD_DRAW_OTHER_ERROR;
    assert(update_display_water_turn(&hl, (EpdRect){0, 0, 16, 16}, E0470_TURN_RTL) == EPD_DRAW_SUCCESS);
    assert(water_calls == 2 && clears == 1 && white_baseline && correct_target_at_draw);
    assert(last_mode == MODE_GC16 && !memcmp(back, target, FB_BYTES));
    memcpy(front,target,FB_BYTES);memset(back,0x55,FB_BYTES);draws=clears=full_draws=0;
    // 阅读全刷与 rc90 一致：一轮原表 GC16，不先 epd_clear，也不追加白底文字输出。
    // Reader full refresh matches rc90: one original-table GC16 output, without epd_clear or a white-baseline text pass.
    assert(update_display_full(&hl)==EPD_DRAW_SUCCESS);
    assert(clears==0&&draws==1&&full_draws==1&&last_mode==MODE_GC16&&!white_baseline&&correct_target_at_draw);
    assert(waveform_at_draw==&E0470_WAVEFORM);
    assert(!memcmp(front,target,FB_BYTES)&&!memcmp(back,target,FB_BYTES));
    // 手动修复保留目标页和底栏，只做一次最终整屏输出；成功后局部更新不再被迫全刷。
    // Manual repair preserves the target and navigation with one final full output, then allows local updates again.
    checking_repair = true;
    for (int failure = 0; failure < 3; ++failure) {
        memcpy(front, target, FB_BYTES); memset(back, 0x55, FB_BYTES);
        blind_pushes = repair_delays = poweroffs = 0;
        draws = full_draws = clears = 0;
        next_draw_error = failure ? EPD_DRAW_EMPTY_LINE_QUEUE : EPD_DRAW_SUCCESS;
        draw_errors_remaining = failure;
        display_main_transition_arm(&hl, true, true);
        display_request_full_refresh();
        enum EpdDrawError repaired = display_panel_repair(&hl);
        assert(blind_pushes == 6 * 26 && repair_delays == 7 && poweroffs == 1);
        assert(!memcmp(front, target, FB_BYTES) && hl.waveform == &E0470_WAVEFORM);
        assert(draws == (failure ? 2 : 1) && full_draws == draws);
        assert(correct_target_at_draw && white_baseline && !ota_display_depth);
        assert((repaired == EPD_DRAW_SUCCESS) == (failure != 2));
        int prior = full_draws;
        assert(update_display_area_diff_with(&hl, &E0470_WAVEFORM, MODE_GL16,
                                            (EpdRect){0,160,16,16}) == EPD_DRAW_SUCCESS);
        assert(full_draws == prior + (failure == 2));
    }
    unsigned prior_pushes = blind_pushes;
    assert(display_panel_repair(NULL) == EPD_DRAW_INVALID_CROP && blind_pushes == prior_pushes);
    checking_repair = false;
    assert(!ota_display_depth);
    puts("display recovery: front retained, locked blind panel repair, single final frame, retry/failure recovery and bulk prefill passed");
    // 每个输出出口都必须拒绝未就绪的供电；连续输入不重复等待，恢复失败继续要求整屏重建。
    // Every output must reject unready power; queued input cannot repeat the wait, and failed recovery retains the full-screen rebuild request.
    no_change = false;
    for (unsigned path = 0; path <= 16; ++path) {
        now_us += 2000000;
        for (unsigned i = 0; i < FB_BYTES; ++i) target[i] = (uint8_t)(i + path * 13);
        memcpy(front, target, FB_BYTES); memset(back, 0x5a, FB_BYTES);
        power_failure = true;
        checking_repair = path == 16;
        if (checking_repair) repair_delays = 0;
        int before_draws = draws, before_clears = clears, before_water = water_calls;
        unsigned before_blind = blind_pushes;
        assert(power_fault_present(&hl, path) == DISPLAY_DRAW_POWER_NOT_READY);
        checking_repair = false;
        assert(draws == before_draws && clears == before_clears && water_calls == before_water);
        assert(blind_pushes == before_blind && !drive_active && !ota_display_depth);
        for (unsigned i = 0; i < FB_BYTES; ++i) assert(back[i] == 0x5a);
        // white 出口的目标本来就是白；其他出口必须保留调用方的完整目标。
        // White output intentionally requests white; every other output preserves the caller's complete target.
        if (path == 6) memset(target, 0xff, FB_BYTES);
        assert(!memcmp(front, target, FB_BYTES));
        int attempts = powerons;
        for (unsigned n = 0; n < 20; ++n)
            assert(update_display_mode_diff(&hl, MODE_GL16) == DISPLAY_DRAW_POWER_NOT_READY);
        guard_draw_result(&hl, DISPLAY_DRAW_POWER_NOT_READY);
        guard_draw_result(&hl, EPD_DRAW_EMPTY_LINE_QUEUE);
        assert(powerons == attempts && draws == before_draws && clears == before_clears);
        now_us += 1001000; power_failure = false;
        next_draw_error = EPD_DRAW_OTHER_ERROR;
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_OTHER_ERROR);
        assert(clears == before_clears + 1 && draws == before_draws + 1 && white_baseline);
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(clears == before_clears + 2 && draws == before_draws + 2 && correct_target_at_draw);
        assert(!memcmp(front, target, FB_BYTES) && !memcmp(back, target, FB_BYTES) && !ota_display_depth);
        assert(update_display_mode_diff(&hl, MODE_GL16) == EPD_DRAW_SUCCESS);
        assert(clears == before_clears + 2);
    }
    puts("power recovery: all 17 outputs reject missing rails without committing back; queued retries are bounded and recovery preserves the target until full output succeeds");
}

// Continuous state is exercised separately with native-size buffers.

void epd_fill_rect(EpdRect area, uint8_t color, uint8_t *fb) {
    (void)area; (void)color; (void)fb; assert(false);
}
