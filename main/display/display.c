/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 刷屏出口：按模式推屏、HV 轨空闲下电、pclk 欠载回退。
 *
 * Present path: push by mode, drop HV rails on idle, fall back pclk on
 * underrun.
 * 用户继续优化快刷响应：无灰阶条带时按32位并行归一八像素，已为黑白的字不重复写PSRAM；不删相位、不增加缓存或改变清残影和底栏边界。
 * User continues fast responsiveness tuning: normalize eight pixels per 32-bit word when no gray bands exist and avoid rewriting binary PSRAM words; retain phases, cleanup and navigation bounds without extra caches.
 * 用户修订：三种主页模式均固定底栏，仅局部移动横条；普通切页复用柔和灰阶表，黑白端点单向驱动、等灰保持，中灰保留校准相位。取消每三次全像素整理，仍每十二次完整清残影。快刷棋盘格输出、首次进入、解锁及手动全刷不变，不得伪造back。
 * User revision: all main modes retain navigation with local markers; ordinary transitions reuse soft grays with directional BW endpoints, held equal grays and calibrated changed midgrays. Remove third-page full-pixel settling but retain twelfth-page cleanup. Fast checkerboards, initial entry, unlock and manual full output remain; never fabricate back.
 * 用户修订：所有推屏及欠载恢复与 OTA 擦写共用递归锁；无升级任务时保持原波形、范围和节奏。
 * User revision: serialize all display output and underrun recovery with OTA flash operations; keep existing waveforms, bounds and cadence outside updates.
 * 用户要求加快主页水波纹：复用阅读的一拍一带调度，以8带37相共44拍、11090us完成；保留主页原灰阶序列、底栏边界、OTA锁及失败恢复。
 * User requests faster main-page ripple: reuse reader-style compact launches for eight bands/37 phases over 44 ticks at 11090us; retain main gray sequences, navigation bounds, OTA locking and recovery.
 * 用户修订：书架顶部文字、框线与封面/亚克力一起使用水波纹，不再拆分顶部差分刷新；底栏仍固定。
 * User revision: shelf headers, frames, covers and acrylic share one ripple instead of a separate header differential; navigation remains fixed.

 * 用户修订：普通书架上下文字区和中间书架区同一轮逐相输出，外侧柔和、封面/材质保留校准灰阶，留白仅向白擦除；不串行呈现分区。仅横条可变，底栏图标固定，保留统一故障恢复。
 * User revision: ordinary shelf chrome and art share each scan phase, with soft outer text, calibrated covers/furniture and directional whitespace erasure; never reveal regions serially. Only the marker may change within frozen navigation; retain common fault recovery.
 * 用户2026-11-03修订：固定区缩到实际导航顶部，所有页面内容差分直接覆盖原先多保留的24行；仅全像素整理向内对齐，避免驱动固定导航。
 * User revision 2026-11-03: shrink the fixed region to the actual navigation top, including the formerly protected 24 rows in every page's content differences; only full-pixel cleanup aligns inward to avoid driving fixed navigation.
 * 用户修订：纯文字水波纹改回面板原厂GL16表，保留每种灰阶转换的前导擦除；文字表只用于普通纯文字翻页，8带/11090us、目标灰阶和故障恢复保持。
 * User revision: pure-text ripple returns to the vendor GL16 table with each transition's leading erase; the text table serves ordinary text turns only, retaining eight bands/11090us, target grays and recovery.
 * 用户修订：高压轨不再保留8秒，前后光场景统一在每次刷新后只保留100ms，空闲即断VCOM/VSH。
 * User revision: drop the eight-second rail hold; with the light on or off, keep VCOM/VSH only 100 ms after each output and release them on idle.
 * 用户修订：100ms空闲关断由底层独立任务执行，扫描结束计时，不依赖主循环或开书特判。
 * User revision: a low-level independent task owns the 100 ms idle shutdown from scan completion, without app-loop polling or book-open special cases.
 * 用户修订：完整显示组结束主动关电，100ms保留为空闲兜底；只在真实扫描前上电。
 * User revision: shut down at the end of complete display groups; retain 100 ms idle fallback and power on only for physical scans.
 * 用户2026-11-08修订：解锁与阅读返回残影较重，转场请求使用完整48相GC16整屏全像素输出；成功后恢复原刷新策略。
 * User revision 2026-11-08: use complete 48-phase full-pixel GC16 for requested unlock/reader-return transitions after heavy ghosting reports, then resume the existing policy on success.

 */

#include "display.h"
#include "board_hardware.h"
#include "ota_online.h"

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"
#include "e0470_epaper_waveform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "display_main_waveform.h"
#include "display_reader_waveform.h"
#include "ui/ui_nav_layout.h"

static const char* TAG = "read_pico";
static bool s_bulk_io;
static bool s_full_refresh_requested;
static bool s_complete_refresh_requested;
void display_request_full_refresh(void) {
    s_full_refresh_requested = s_complete_refresh_requested = true;
}
static int s_pclk_mhz = DISPLAY_PCLK_DEFAULT_MHZ;

void display_set_bulk_io(bool active) {
    s_bulk_io = active;
    ESP_LOGI(TAG, "bulk I/O scan margin %s", active ? "on" : "off");
}

// 完整 DU 与胶囊定稿保留 FULL 扫描档；FOLLOW 与胶囊拖动保留 FAST 扫描档。
// 扫描档是时序配置选择，实际相周期由板级总线、像素时钟和行消隐共同求解，不能按档名推断耗时。
// Full DU and capsule settling retain FULL scan; FOLLOW and capsule drag retain FAST scan.
// Scan profiles select timing parameters; bus width, pclk and blanking determine phase duration, not the profile name.
static void use_scan_for(const EpdWaveform* waveform, enum EpdDrawMode mode) {
    const bool fast = (mode & 0xF) == MODE_DU &&
        (waveform == &E0470_FOLLOW_WAVEFORM || waveform == &E0470_CAPSULE_DRAG_WAVEFORM);
    board_epd_use_scan(fast ? BOARD_EPD_SCAN_FAST : BOARD_EPD_SCAN_FULL);
    // 高层刷新保持整屏扫描；两条63行可用队列在第127行入队前启动。
    // High-level updates scan the full panel; two 63-slot queues start before line 127 is enqueued.
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : (fast ? 64 : 32));
}

// 自上次整屏 GC16 以来的整页差分次数。局部反馈不计数，否则菜单按压几次就会触发黑白全刷。
// Whole-page differentials since the last full GC16. Local feedback is excluded so a few taps cannot trigger flashing.
static int s_page_refreshes;

static const void* s_main_armed;
static bool s_main_fault;
static bool s_main_shelf_page;
static EpdRect s_main_bands[3];
static unsigned s_main_band_count;
static bool s_main_shelf_exit;
static bool s_main_changing_page;
_Static_assert((UI_NAV_CLEAN_END & 31) == 0, "full-pixel cleanup ends on an aligned framebuffer column");
_Static_assert(UI_NAV_REFRESH_END == UI_NAV_TOP, "content and fixed navigation share one boundary");
_Static_assert(UI_NAV_MARKER_SCAN_END <= UI_NAV_TOP + 27, "marker crop must stop before navigation icons");
_Static_assert((UI_NAV_MARKER_TOP & 1) && !(UI_NAV_MARKER_BOTTOM & 1), "marker nibble guards match current geometry");
_Static_assert((UI_NAV_TOP & 7) == 0, "content tail ends on an eight-pixel normalization word");

static bool main_nav_geometry(void) {
    // 当前板为倒置竖屏：逻辑 y 对应物理 x。其他屏型保持原刷新路径。
    // On this inverted-portrait board logical y maps to physical x; other layouts keep the existing presenter.
    return epd_get_rotation() == EPD_ROT_INVERTED_PORTRAIT &&
           epd_width() == 1216 && epd_height() == 684;
}

static EpdRect main_body_area(void) {
    // 内容直接到实际导航顶部；扩列中的固定像素来自真实back，仅驱动差异。
    // Content reaches the actual navigation top; expanded fixed pixels retain real back and only differences are driven.
    return (EpdRect){0, 0, epd_rotated_display_width(), UI_NAV_REFRESH_END};
}

static uint8_t main_bw_byte(uint8_t value) {
    return (value & 0x0f) >= 8 ? ((value & 0xf0) >= 0x80 ? 0xff : 0x0f)
                             : ((value & 0xf0) >= 0x80 ? 0xf0 : 0x00);
}

static bool main_gray_column(int x) {
    for (unsigned i = 0; i < s_main_band_count; ++i)
        if (x >= s_main_bands[i].y && x < s_main_bands[i].y + s_main_bands[i].height) return true;
    return false;
}

static void main_normalize_body(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* row = hl->front_fb + y * stride;
        if (!s_main_band_count) {
            for (int x = 0; x < UI_NAV_TOP / 2; x += 4) {
                uint32_t value;
                memcpy(&value, row + x, sizeof(value));
                const uint32_t binary = ((value & UINT32_C(0x88888888)) >> 3) * 15u;
                if (value != binary) memcpy(row + x, &binary, sizeof(binary));
            }
            continue;
        }
        for (int x = 0; x < UI_NAV_TOP / 2; ++x)
            if (!main_gray_column(x * 2)) row[x] = main_bw_byte(row[x]);
    }
}

static bool main_body_changed(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y)
        if (memcmp(hl->front_fb + y * stride, hl->back_fb + y * stride, UI_NAV_REFRESH_END / 2)) return true;
    return false;
}

static bool main_edge_changed(EpdiyHighlevelState* hl) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y)
        if (memcmp(hl->front_fb + y * stride + UI_NAV_CLEAN_END / 2,
                   hl->back_fb + y * stride + UI_NAV_CLEAN_END / 2,
                   (UI_NAV_TOP - UI_NAV_CLEAN_END) / 2)) return true;
    return false;
}

// 三条最终灰阶最多约90KiB；首帧跳过它们，实际输出成功才提交back。
// Three final gray bands cost at most about 90 KiB; skip them in the first pass and commit back only after successful output.
static size_t main_band_bytes(void) {
    size_t bytes = 0;
    for (unsigned i = 0; i < s_main_band_count; ++i) bytes += (size_t)s_main_bands[i].height * epd_height() / 2;
    return bytes;
}
static void main_band_copy(EpdiyHighlevelState *hl, uint8_t *cache, bool restore) {
    const size_t stride = (size_t)epd_width() / 2;
    for (unsigned i = 0; i < s_main_band_count; ++i) {
        const size_t bytes = (size_t)s_main_bands[i].height / 2;
        for (int y = 0; y < epd_height(); ++y) {
            uint8_t *row = hl->front_fb + y * stride + s_main_bands[i].y / 2;
            if (restore) memcpy(row, cache, bytes);
            else { memcpy(cache, row, bytes); memcpy(row, hl->back_fb + y * stride + s_main_bands[i].y / 2, bytes); }
            cache += bytes;
        }
    }
}

static void main_freeze_navigation(EpdiyHighlevelState* hl, bool update_marker) {
    const size_t stride = (size_t)epd_width() / 2;
    for (int y = 0; y < epd_height(); ++y) {
        uint8_t* front = hl->front_fb + y * stride;
        const uint8_t* back = hl->back_fb + y * stride;
        if (!update_marker) {
            memcpy(front + UI_NAV_TOP / 2, back + UI_NAV_TOP / 2,
                   stride - UI_NAV_TOP / 2);
            continue;
        }
        // 只保留目标横条的五行；起点为奇数像素，同行字节的分隔线半字节仍取真实 back。
        // Keep only the five target marker rows; at the odd first pixel retain the neighboring divider nibble from actual back.
        memcpy(front + UI_NAV_TOP / 2, back + UI_NAV_TOP / 2,
               (UI_NAV_MARKER_TOP - UI_NAV_TOP) / 2);
        const int first = UI_NAV_MARKER_TOP / 2;
        front[first] = (front[first] & 0xf0) | (back[first] & 0x0f);
        memcpy(front + UI_NAV_MARKER_BOTTOM / 2, back + UI_NAV_MARKER_BOTTOM / 2,
               stride - UI_NAV_MARKER_BOTTOM / 2);
    }
}

void display_main_transition_disarm(void) {
    s_main_armed = NULL;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = false;
    s_main_changing_page = false;
}

void display_main_transition_cancel(void) {
    display_main_transition_disarm();
}

void display_main_transition_arm(const void* owner, bool leaving_shelf, bool changing_page) {
    s_main_armed = owner;
    s_main_shelf_page = false;
    s_main_band_count = 0;
    s_main_shelf_exit = s_main_armed && leaving_shelf;
    s_main_changing_page = s_main_armed && changing_page;
}

void display_main_transition_gray_bands(const EpdRect *bands, unsigned count) {
    if (!s_main_armed) return;
    s_main_band_count = 0;
    if (!bands || count > 3) return;
    int last = 0;
    for (unsigned i = 0; i < count; ++i) {
        EpdRect b = bands[i];
        if (b.x || b.width != epd_rotated_display_width() || b.y < last || b.height <= 0 ||
            (b.y & 1) || (b.height & 1) || b.y > UI_NAV_REFRESH_END || b.height > UI_NAV_REFRESH_END - b.y) return;
        s_main_bands[i] = b; last = b.y + b.height;
    }
    s_main_band_count = count;
}

void display_main_transition_shelf_page(void) {
    s_main_shelf_page = s_main_armed != NULL;
}

// 供电失败后先撤掉请求，至少静置一秒；积压输入不能反复进入板级就绪等待。
// Drop power requests after failure and wait at least one second; queued input must not repeatedly enter the board's ready wait.
#define POWER_RETRY_INTERVAL_MS 1000
static int64_t s_power_retry_ms;
static bool s_power_fault;

static bool display_power_ready(void) {
    if (s_power_retry_ms && esp_timer_get_time() / 1000 < s_power_retry_ms) return false;
    if (epd_power_scan_allowed()) {
        s_power_retry_ms = 0;
        return true;
    }
    epd_poweroff();
    s_power_retry_ms = esp_timer_get_time() / 1000 + POWER_RETRY_INTERVAL_MS;
    s_power_fault = s_main_fault = s_full_refresh_requested = true;
    return false;
}

// 只在整屏目标确实落屏后解除供电故障；失败时继续保留重建要求。
// Clear a power fault only after the full target reaches the screen; failed recovery retains the rebuild request.
static void display_power_recovered(enum EpdDrawError result) {
    if (s_power_fault && result == EPD_DRAW_SUCCESS) {
        s_power_fault = s_main_fault = s_full_refresh_requested = false;
    }
}

static enum EpdDrawError main_recover(EpdiyHighlevelState* hl, enum EpdDrawError error) {
    // 扫描失败后光学状态未知，必须从白底重建，不能复用半途图的 back。
    // A failed scan leaves unknown optical state; rebuild from white instead of reusing an intermediate back.
    display_main_transition_cancel();
    if (error & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        board_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_clear();
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    s_main_fault = result != EPD_DRAW_SUCCESS;
    s_page_refreshes = 0;
    ESP_LOGW(TAG, "main transition recovery error=%d result=%d", error, result);
    return result;
}

static enum EpdDrawError main_update_edge(EpdiyHighlevelState* hl) {
    // 末24行与横条共用32列裁剪，保留灰阶差分；固定导航已从真实back恢复，不做全像素整理。
    // Share the marker's aligned crop for the final 24 content rows using grayscale differences; fixed navigation is restored from real back, without full-pixel cleanup.
    const EpdWaveform* wave = app_settings_main_refresh_mode() == APP_MAIN_REFRESH_NORMAL
        ? display_main_normal_waveform() : display_main_gray_waveform();
    use_scan_for(wave, MODE_GL16);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, wave);
    return epd_hl_update_area_padded(hl, MODE_GL16, 25,
        (EpdRect){0, UI_NAV_CLEAN_END, epd_rotated_display_width(),
                  UI_NAV_MARKER_SCAN_END - UI_NAV_CLEAN_END}, 1, 1);
}

static bool main_begin(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    const void* owner = s_main_armed;
    const bool shelf_exit = s_main_shelf_exit || s_main_shelf_page;
    s_main_shelf_page = false;
    const bool changing_page = s_main_changing_page;
    s_main_changing_page = false;
    const int refresh_mode = app_settings_main_refresh_mode();
    const bool water = refresh_mode == APP_MAIN_REFRESH_WATER;
    const bool normal = refresh_mode == APP_MAIN_REFRESH_NORMAL;
    const bool fast = refresh_mode == APP_MAIN_REFRESH_FAST;
    s_main_shelf_exit = false;
    s_main_armed = NULL;
    if (!owner || !hl || !hl->front_fb || !hl->back_fb) return false;
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    if (!main_nav_geometry()) return false;
    // 8帧整页方案被实机否定；用户接受主页面黑白、亚克力灰阶。黑白端点原用完整20相DU，
    // 2026-10-24 起水波纹变黑端点改走原厂擦除序列并补回18次推黑，修复灰起点文字欠饱和。
    // Hardware rejected whole-page 8-frame output; the user accepted black/white main
    // pages with gray acrylic. Black endpoints used the full 20-phase DU; since
    // 2026-10-24 the water black endpoint takes the vendor erase sequence with 18
    // restored pushes, fixing undersaturated gray-start text.
    main_freeze_navigation(hl, true);
    if (fast) main_normalize_body(hl);
    const bool changed = main_body_changed(hl);
    uint8_t *cache = fast && changed && s_main_band_count
        ? heap_caps_aligned_alloc(16, main_band_bytes(), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    const bool gray_fallback = water || (fast && changed && s_main_band_count && !cache);
    // 黑白表的等灰迁移均不驱动；仅把有变化的横条并入同次扫描，固定图标保持原样。
    // The BW/exit tables hold every equal pair. Include only the changing marker
    // in this scan; frozen divider/labels/icons receive no drive. This removes
    // an additional comparison and eight-frame serial marker update.
    bool marker_with_body=fast&&changed&&!cache&&!gray_fallback;
    EpdRect body_area=main_body_area();
    if(marker_with_body)body_area.height=UI_NAV_MARKER_SCAN_END;
    if (cache) main_band_copy(hl, cache, false);
    const EpdWaveform* body_wave = normal ? display_main_normal_waveform() : gray_fallback ? display_main_gray_waveform()
        : shelf_exit ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
    enum EpdDrawMode body_mode = normal || gray_fallback ? MODE_GL16 : MODE_DU;
    bool body_full = false;
    // 普通切页只驱动变化像素，完整中灰相位不缩短；每十二次仍用原表清理内容区。
    // Ordinary changes drive differing pixels without shortening midgray phases; the original table still cleans the body every twelfth change.
    if (normal && changed) {
        ++s_page_refreshes;
        if (APP_UI_FAST_GC16_EVERY > 0 && s_page_refreshes >= APP_UI_FAST_GC16_EVERY) {
            s_page_refreshes = 0;
            body_wave = &E0470_WAVEFORM;
            body_mode = MODE_GC16;
            body_full = true;
        }
    }
    const bool shelf_regions=normal&&changed&&s_main_band_count&&(changing_page||body_full);
    if(body_full)body_area.height=UI_NAV_CLEAN_END;
    if(shelf_regions){marker_with_body=true;body_area.height=UI_NAV_MARKER_SCAN_END;}
    use_scan_for(body_wave, body_mode);
    if (!display_power_ready()) {
        if (cache) {
            main_band_copy(hl, cache, true);
            heap_caps_free(cache);
        }
        display_main_transition_cancel();
        *result = DISPLAY_DRAW_POWER_NOT_READY;
        return true;
    }
    epd_hl_waveform(hl, body_wave);
    int64_t start = esp_timer_get_time();
    // 快刷/水波纹不攒强刷次数；普通的周期整理也不能进入底栏。
    // Fast/water accrue no forced-cleanup debt; ordinary periodic settling also excludes navigation.
    if(shelf_regions){
        // 同一帧逐列选择柔和/校准表，上下和中间一起开始，不显示串行的三段旧图。
        // Select soft/calibrated LUTs by columns in the same frame so chrome and art start together without three serial old-image regions.
        EpdRect last=s_main_bands[s_main_band_count-1];
        int top=s_main_bands[0].y&~31,bottom=(last.y+last.height+31)&~31;
        if(bottom>UI_NAV_CLEAN_END)bottom=UI_NAV_CLEAN_END;
        *result=e0470_update_regions(hl,body_area,(EpdRect){0,top,body_area.width,bottom-top},
            display_main_normal_waveform(),display_main_shelf_waveform(body_full?MODE_GC16:MODE_GL16),
            body_full?MODE_GC16:MODE_GL16);
        if(*result==EPD_DRAW_NO_PHASES_AVAILABLE){
            // 低内存仍一次输出整张目标；不退回分段等待或更改真实back。
            // Low memory still presents the whole target once, without serial waiting or a fabricated baseline.
            epd_hl_waveform(hl,display_main_normal_waveform());
            *result=epd_hl_update_area_padded(hl,MODE_GL16,25,body_area,1,1);
        }
    } else if (water && changing_page && changed) {
        // 水波纹直接输出最终灰阶图，不先刷黑白、不追加整页灰阶；横条并入同一次扫描，
        // 旧横条随正文一起用强白推动擦除。失败由统一恢复处理。
        // Water outputs the final gray target directly, without a BW prepass or a second
        // whole-page gray update; the marker joins the same scan so the old bar is erased
        // with the body's stronger whitening. Common recovery handles failure.
        board_epd_use_scan(BOARD_EPD_SCAN_FAST);
        epd_lcd_set_prefill_lines(127);
        EpdRect ripple = main_body_area();
        ripple.height = UI_NAV_MARKER_SCAN_END;
        marker_with_body = true;
        const int prior_tick=e0470_page_turn_tick_us();
        e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        *result = e0470_page_turn_with_waveform_compact(hl, ripple, E0470_TURN_RTL,
                                                       display_main_water_waveform(), E0470_TURN_FAST_BANDS);
        e0470_page_turn_set_tick_us(prior_tick);
        if (*result == EPD_DRAW_NO_PHASES_AVAILABLE) {
            use_scan_for(body_wave, MODE_GL16);
            epd_hl_waveform(hl, body_wave);
            *result = epd_hl_update_area_padded(hl, MODE_GL16, 25, ripple, 1, 1);
        }
        // 主页面动画不长期保留相位缓存，避免与联网和阅读资源争用。
        // Release main-animation LUTs instead of competing with networking and reader resources.
        e0470_page_turn_release();
    } else if (!changed) *result = EPD_DRAW_SUCCESS;
    else *result = body_full ? epd_hl_update_area_full(hl, body_mode, 25, body_area)
                            : body_mode == MODE_GL16
                            ? epd_hl_update_area_padded(hl, body_mode, 25, body_area, 1, 1)
                            : epd_hl_update_area(hl, body_mode, 25, body_area);
    if (cache) {
        main_band_copy(hl, cache, true);
        heap_caps_free(cache);
        if (*result == EPD_DRAW_SUCCESS) {
            const EpdRect last = s_main_bands[s_main_band_count - 1];
            EpdRect gray = {0, s_main_bands[0].y, epd_rotated_display_width(), last.y + last.height - s_main_bands[0].y};
            // 采用原校准迁移，不自行按灰度比例截帧；等灰无需反复推动。
            // Retain calibrated transitions without scaling pulses by gray distance; equal grays need no repeated drive.
            epd_hl_waveform(hl, display_main_gray_waveform());
            use_scan_for(display_main_gray_waveform(), MODE_GL16);
            *result = epd_hl_update_area_padded(hl, MODE_GL16, 25, gray, 1, 1);
        }
    }
    s_main_band_count = 0;
    if (*result == EPD_DRAW_SUCCESS && !marker_with_body) {
        if (main_edge_changed(hl)) *result = main_update_edge(hl);
        else {
            const EpdRect marker = {0, UI_NAV_MARKER_TOP, epd_rotated_display_width(),
                                   UI_NAV_MARKER_BOTTOM - UI_NAV_MARKER_TOP};
            // 同页重绘横条无变化时 highlevel 自动跳过；32列扩展范围中其余像素来自 back，保持不驱动。
            // Highlevel skips an unchanged marker; all other pixels in its expanded 32-column crop come from back and remain undriven.
            epd_hl_waveform(hl, &E0470_FOLLOW_WAVEFORM);
            use_scan_for(&E0470_FOLLOW_WAVEFORM, MODE_DU);
            *result = epd_hl_update_area(hl, MODE_DU, 25, marker);
        }
    }
    if (*result != EPD_DRAW_SUCCESS) *result = main_recover(hl, *result);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    ESP_LOGI(TAG, "main refresh mode=%d shelf_exit=%d draw=%lldms result=%d", app_settings_main_refresh_mode(), shelf_exit, (esp_timer_get_time() - start) / 1000, *result);
    (void)start;
    return true;
}

// 其他显示出口只取消未用入口；主页面失败时先重建完整目标，绝不回放旧缓存。
// Other outputs disarm unused entry permission; a main-page failure first rebuilds the full current target, never replaying old cache.
static bool main_before_update(EpdiyHighlevelState* hl, enum EpdDrawError* result) {
    display_main_transition_cancel();
    if (s_main_fault) { *result = main_recover(hl, EPD_DRAW_SUCCESS); return true; }
    return false;
}

// 所有按 fb 刷屏的出口都经这里。GL16 必须全像素（白底补 1 帧靠它打到）。
// 整页差分刷攒够 APP_UI_GC16_EVERY 次就把这一次升为全像素 GC16：区域、fb 都不变，只换模式，
// 屏上内容仍由 fb 决定，不会丢；全像素是为了让未变化像素也过一遍 LUT，否则压不掉灰底。
// 跟随 DU 波形只有 DU 一张表，不计数也不升级。
// Every fb present goes through here. GL16 must be full-pixel (the extra white
// frame depends on that). After APP_UI_GC16_EVERY whole-page updates, this one is
// promoted to full-pixel GC16: area and fb stay the same, only the mode
// changes, so content is not lost. Full-pixel is so unchanged pixels also run
// the LUT; otherwise the gray floor will not clear. FOLLOW DU has only a DU
// table and does not count or promote.
static enum EpdDrawError hl_update_padded(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode, bool full,
    const EpdRect* area, bool allow_gl16_diff, int pad_lines, int pad_col_bytes
) {
    // 局部差分到实际导航顶边；只有全像素更新向内对齐，再补余下内容差分。
    // Partial differences reach the actual navigation top; only full-pixel updates align inward and finish the remaining content with differences.
    if (area && s_main_shelf_page && app_settings_main_fast_refresh()) {
        enum EpdDrawError result;
        if (main_begin(hl, &result)) return result;
    }
    EpdRect clipped;
    const bool protected_area = area && s_main_armed && !s_main_fault && main_nav_geometry();
    bool update_edge = false, skip_body = false;
    if (protected_area) {
        main_freeze_navigation(hl, false);
        if (app_settings_main_fast_refresh()) main_normalize_body(hl);
        const bool full_pixels = full || (mode & 0x0f) == MODE_GC16 ||
            ((mode & 0x0f) == MODE_GL16 && !allow_gl16_diff);
        const int content_end = full_pixels ? UI_NAV_CLEAN_END : UI_NAV_REFRESH_END;
        update_edge = full_pixels && area->y < UI_NAV_TOP && (int64_t)area->y + area->height > UI_NAV_CLEAN_END &&
                      main_edge_changed(hl);
        clipped = *area;
        if ((int64_t)clipped.y + clipped.height > content_end)
            clipped.height = content_end - clipped.y;
        if (clipped.height <= 0) {
            if (!update_edge) { display_main_transition_disarm(); return EPD_DRAW_SUCCESS; }
            skip_body = true;
        }
        area = &clipped;
        // 显式短局刷不被主页模式改写，猫眼/秒针在普通与水波纹中采用同一动效。
        // Preserve explicit short partial updates so cat eyes and seconds share ordinary-mode motion in ripple mode.
        if (!skip_body && !full && waveform != &E0470_FOLLOW_WAVEFORM && (mode & 0x0f) != MODE_GC16 &&
            (app_settings_main_refresh_mode()!=APP_MAIN_REFRESH_NORMAL || (mode & 0x0f)==MODE_GL16)) {
            bool has_gray = !app_settings_main_fast_refresh();
            for (unsigned i = 0; i < s_main_band_count; ++i)
                has_gray |= clipped.y < s_main_bands[i].y + s_main_bands[i].height && clipped.y + clipped.height > s_main_bands[i].y;
            waveform = app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL ? display_main_normal_waveform() :
                has_gray ? display_main_gray_waveform() : s_main_shelf_page
                ? display_main_shelf_exit_waveform() : display_main_bw_waveform();
            mode = has_gray ? MODE_GL16 : MODE_DU;
            allow_gl16_diff = true;
            epd_hl_waveform(hl, waveform);
        }
    }
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (waveform != &E0470_FOLLOW_WAVEFORM && area == NULL) {
        if ((mode & 0xF) == MODE_GC16) {
            s_page_refreshes = 0;
        } else if (APP_UI_GC16_EVERY > 0 && ++s_page_refreshes >= APP_UI_GC16_EVERY) {
            s_page_refreshes = 0;
            mode = (enum EpdDrawMode)((mode & ~0xF) | MODE_GC16);
            full = true;
            ESP_LOGI(TAG, "promote to GC16 after %d page transitions", APP_UI_GC16_EVERY);
        }
    }
    full = full || ((mode & 0xF) == MODE_GL16 && !allow_gl16_diff);
    // 显式传入的 padding 一律生效；默认差分出口由 hl_update 只给 GL16 传 1,1，
    // 所以 DU 仍保持原节奏，只有主动要求膨胀的调用（滑杆拖动）才扩边。
    // Explicit padding always applies; hl_update passes 1,1 for GL16 only, so
    // ordinary DU keeps its cadence and only callers that ask for dilation expand.
    const int pad_l = !full ? pad_lines : 0;
    const int pad_c = !full ? pad_col_bytes : 0;
    // 比较完成后只切换一次扫描档，避免快刷前先切 FULL 再切 FAST。
    // Select scan timing once after comparison, without a FULL-to-FAST round trip before fast updates.
    use_scan_for(waveform, mode);
    enum EpdDrawError result;
    if (skip_body) result = EPD_DRAW_SUCCESS;
    else if (area != NULL) {
        result = full ? epd_hl_update_area_full(hl, mode, 25, *area)
                    : epd_hl_update_area_padded(hl, mode, 25, *area, pad_l, pad_c);
    } else result = full ? epd_hl_update_screen_full(hl, mode, 25)
                         : epd_hl_update_screen_padded(hl, mode, 25, pad_l, pad_c);
    if (result == EPD_DRAW_SUCCESS && update_edge) {
        result = main_update_edge(hl);
        epd_hl_waveform(hl, waveform);
        use_scan_for(waveform, mode);
    }

    return protected_area && result != EPD_DRAW_SUCCESS ? main_recover(hl, result) : result;
}

static enum EpdDrawError hl_update(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode, bool full,
    const EpdRect* area, bool allow_gl16_diff
) {
    // 默认出口只给 GL16 差分扩边，DU 仍按原节奏；需要擦细线残影的调用走 padded API。
    // The default exit dilates GL16 differentials only; callers that clean thin-line ghosts use the padded API.
    const bool gl16 = (mode & 0xF) == MODE_GL16;
    return hl_update_padded(hl, waveform, mode, full, area, allow_gl16_diff,
                            gl16 ? 1 : 0, gl16 ? 1 : 0);
}

static enum EpdDrawError update_display_mode_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, false);
    return result;
}

static enum EpdDrawError update_display_mode_diff_unlocked(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    enum EpdDrawError result = hl_update(hl, &E0470_WAVEFORM, mode, false, NULL, true);
    return result;
}

// 阅读页整页差分可带自己的白目标序列；输出后恢复默认表，后续局部刷新不受影响。
// Reader page differentials may carry their own white-target sequence; restore the
// default waveform afterward so later partial updates are unaffected.
static enum EpdDrawError update_display_mode_diff_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, NULL, true);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_white_restore_unlocked(EpdiyHighlevelState* hl, EpdRect area) {
    if (!hl || !hl->front_fb || !hl->back_fb || area.x < 0 || area.y < 0 ||
        area.width <= 0 || area.height <= 0 ||
        (int64_t)area.x + area.width > epd_rotated_display_width() ||
        (int64_t)area.y + area.height > epd_rotated_display_height()) return EPD_DRAW_INVALID_CROP;
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    const size_t bytes = (size_t)epd_width() / 2 * epd_height();
    uint8_t *target = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const EpdWaveform *wave = display_main_water_waveform();
    use_scan_for(wave, MODE_GL16);
    epd_hl_waveform(hl, wave);
    enum EpdDrawError result;
    if (target) {
        memcpy(target, hl->front_fb, bytes);
        // 白目标只向白推动；成功后 back 才成为真实白底，不能先伪造缓存。
        // White targets drive only white; only successful output commits the real white baseline.
        epd_fill_rect(area, 0xff, hl->front_fb);
        result = epd_hl_update_area_padded(hl, MODE_GL16, 25, area, 1, 1);
        memcpy(hl->front_fb, target, bytes);
        heap_caps_free(target);
        if (result == EPD_DRAW_SUCCESS)
            result = epd_hl_update_area_padded(hl, MODE_GL16, 25, area, 1, 1);
    } else {
        // 低内存仍呈现完整目标，退回单次同向擦白差分，不额外黑闪。
        // Low memory still presents the complete target in one directional-white differential without an extra black flash.
        result = epd_hl_update_area_padded(hl, MODE_GL16, 25, area, 1, 1);
    }
    if (result != EPD_DRAW_SUCCESS) result = main_recover(hl, result);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    if (area.x == 0 && area.y == 0 && area.width == epd_rotated_display_width() &&
        area.height == epd_rotated_display_height()) s_page_refreshes = 0;
    return result;
}

static enum EpdDrawError update_display_fast_page_unlocked(EpdiyHighlevelState* hl) {
    enum EpdDrawError navigation;
    if (main_begin(hl, &navigation)) return navigation;
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    if (APP_UI_FAST_GC16_EVERY > 0 && ++s_page_refreshes >= APP_UI_FAST_GC16_EVERY) {
        s_page_refreshes = 0;
        use_scan_for(&E0470_WAVEFORM, MODE_GC16);
        if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
        epd_hl_waveform(hl, &E0470_WAVEFORM);
        enum EpdDrawError result = epd_hl_update_screen_full(hl, MODE_GC16, 25);

        ESP_LOGI(TAG, "fast navigation cleanup after %d transitions", APP_UI_FAST_GC16_EVERY);
        return result;
    }
    const bool ordinary = app_settings_main_refresh_mode() == APP_MAIN_REFRESH_NORMAL;
    const EpdWaveform* waveform = ordinary ? display_main_normal_waveform() : &E0470_WAVEFORM;
    use_scan_for(waveform, MODE_GL16);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    // 普通子页面与四主页使用同一柔和灰阶，不另加黑白预刷或延迟补刷；其他模式保留原周期。
    // Ordinary subpages share the roots' soft grayscale in one pass, without BW prepasses or delayed output; other modes retain their cadence.
    const bool settle = !ordinary && APP_UI_FAST_GL16_SETTLE_EVERY > 0 &&
                        s_page_refreshes % APP_UI_FAST_GL16_SETTLE_EVERY == 0;
    enum EpdDrawError result = settle ? epd_hl_update_screen_full(hl, MODE_GL16, 25)
                                      : epd_hl_update_screen_padded(hl, MODE_GL16, 25, 1, 1);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_from_white_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    use_scan_for(waveform, mode);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, mode, 25);

    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_page_refreshes = 0;
    return result;
}

static enum EpdDrawError update_display_from_white_unlocked(EpdiyHighlevelState* hl) {
    return update_display_from_white_with(hl, &E0470_WAVEFORM, MODE_GC16);
}

static enum EpdDrawError update_display_white_unlocked(EpdiyHighlevelState* hl) {
    epd_hl_set_all_white(hl);
    return update_display_full(hl);
}

static bool s_white_exit;

void display_hold_white_exit(bool hold) {
    s_white_exit = hold;
}

bool display_take_white_exit(void) {
    const bool hold = s_white_exit;
    s_white_exit = false;
    return hold;
}

static enum EpdDrawError update_display_full_unlocked(EpdiyHighlevelState* hl) {
    display_main_transition_cancel();
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    const EpdWaveform* waveform = s_complete_refresh_requested ? &E0470_FULL_WAVEFORM : &E0470_WAVEFORM;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result;
    if (s_power_fault || s_main_fault) {
        // 供电或扫描失败后的物理画面未知，先清物理屏，再从真实白底提交当前目标。
        // Power or scan failure leaves the optical frame unknown: clear the panel, then commit the current target from a real white baseline.
        use_scan_for(waveform, MODE_GC16);
        epd_clear();
        result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
        s_page_refreshes = 0;
        if (result == EPD_DRAW_SUCCESS) s_main_fault = false;
        display_power_recovered(result);
    } else result = hl_update(hl, waveform, MODE_GC16, true, NULL, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    // 失败保留完整转场请求，后续重试不能降回裁剪表。/ Retain the complete transition on failure so retries cannot fall back to the trimmed table.
    if (result == EPD_DRAW_SUCCESS) s_full_refresh_requested = s_complete_refresh_requested = false;
    else if (s_complete_refresh_requested) s_main_fault = true;
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);
static enum EpdDrawError update_display_area_full_with_unlocked(EpdiyHighlevelState*, const EpdWaveform*, enum EpdDrawMode, EpdRect);

static enum EpdDrawError update_display_image_gray_unlocked(EpdiyHighlevelState* hl) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    // 图片保留十六级原始灰阶；白底和完整 48 相波形稳定呈现层次。
    // Images keep sixteen source grays; a white baseline and the full 48-phase table preserve tones.
    use_scan_for(&E0470_FULL_WAVEFORM, MODE_GC16);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_clear();
    epd_hl_waveform(hl, &E0470_FULL_WAVEFORM);
    enum EpdDrawError result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    display_power_recovered(result);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    s_page_refreshes = 0;
    return result;
}

// 指定波形整屏刷一次，刷完把默认波形装回去。用来 A/B 两条灰阶表。
// Present the whole screen with a given waveform, then restore the default. Used to A/B two gray tables.
static enum EpdDrawError update_display_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, NULL, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_area_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_area_diff_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    // 触摸笔迹的 8 帧跟随 DU 固定走 FAST 档（一行 10µs 对 16µs）；否则档位由上一轮
    // 推屏遗留，滑块等跟手区域时而 11ms/帧、时而 6.9ms/帧。其余波形不改档位。
    // The 8-frame FOLLOW DU for touch ink always uses the FAST profile (10 µs/line vs 16);
    // otherwise the profile is whatever the previous push left, so tracking areas alternate
    // between 11 ms and 6.9 ms per frame. Other waveforms leave the profile untouched.
    if ((mode & 0xF) == MODE_DU && waveform == &E0470_FOLLOW_WAVEFORM)
        use_scan_for(waveform, mode);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, false, &area, true);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_area_diff_padded_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area, int pad_lines, int pad_col_bytes
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update_padded(hl, waveform, mode, false, &area, true,
                                                pad_lines, pad_col_bytes);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_area_full_with_unlocked(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    epd_hl_waveform(hl, waveform);
    enum EpdDrawError result = hl_update(hl, waveform, mode, true, &area, false);
    epd_hl_waveform(hl, &E0470_WAVEFORM);
    return result;
}

static enum EpdDrawError update_display_water_turn_unlocked(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    enum EpdDrawError intercepted;
    if (main_before_update(hl, &intercepted)) return intercepted;
    // 阅读页显式选择时调用；主页面水波纹有独立裁剪路径，普通模式不受影响。
    // Explicit reader effect; main-tab water has its own bounded path, and ordinary mode is unaffected.
    board_epd_use_scan(BOARD_EPD_SCAN_FAST);
    epd_lcd_set_prefill_lines(s_bulk_io ? 127 : 64);
    if (!display_power_ready()) return DISPLAY_DRAW_POWER_NOT_READY;
    // 水波纹使用面板原厂GL16表：每种灰阶转换保留前导擦除，灰阶由原厂脉冲校准；
    // 纯白目标仍是原厂的 18 次擦白，只把裁表砍掉的 to=0 推黑补回原厂 18 次。
    // Ripple uses the vendor GL16 table: every gray transition keeps its leading erase
    // and factory-calibrated gray pulses, pure white keeps the vendor's 18 erases, and
    // only to=0 regains the 18 black pushes the trim had cut.
    enum EpdDrawError result = e0470_page_turn_with_waveform_compact(hl, area, dir,
        display_reader_water_waveform(), E0470_TURN_FAST_BANDS);
    use_scan_for(&E0470_WAVEFORM, MODE_GL16);
    if (result == EPD_DRAW_SUCCESS) return result;
    if (result == EPD_DRAW_NO_PHASES_AVAILABLE) {
        ESP_LOGW(TAG, "water turn phases unavailable; use regular GL16");
        return update_display_area_with(hl, &E0470_WAVEFORM, MODE_GL16, area);
    }
    // 中断可能把物理屏留在半途。清屏后按目标 front 重建整屏基准。
    // An interrupted scan may leave intermediate pixels. Rebuild the whole baseline from white.
    if (result & EPD_DRAW_EMPTY_LINE_QUEUE) {
        s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
        board_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    }
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    epd_clear();
    enum EpdDrawError recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);

    s_page_refreshes = 0;
    ESP_LOGW(TAG, "water turn failed (%d), recovery=%d", result, recovered);
    return recovered;
}

// 供数不足时的兜底：把频率退回安全值，整屏白一次，让后面的差分刷有干净参考帧。
// Underrun fallback: drop to the safe clock and wipe the panel white so later differentials have a clean reference.
int display_pclk_mhz(void) { return s_pclk_mhz; }

static void guard_draw_result_unlocked(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    if (!(result & EPD_DRAW_EMPTY_LINE_QUEUE)) return;
    display_main_transition_cancel();
    s_pclk_mhz = DISPLAY_PCLK_SAFE_MHZ;
    board_epd_set_pclk(DISPLAY_PCLK_SAFE_MHZ);
    // 清物理屏后仅重置旧帧基准，保留目标页；否则局部刷新会留下整页白屏。
    // Reset only the old-frame baseline after clearing; preserving the target prevents blank pages after partial updates.
    enum EpdDrawError recovered;
    if (s_complete_refresh_requested) {
        s_main_fault = true;
        recovered = update_display_full_unlocked(hl);
    } else {
        use_scan_for(&E0470_WAVEFORM, MODE_GC16);
        if (!display_power_ready()) return;
        epd_clear();
        recovered = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
    }

    s_main_fault = recovered != EPD_DRAW_SUCCESS;

    if (recovered != EPD_DRAW_SUCCESS) ESP_LOGW(TAG, "underrun recovery failed (%d)", recovered);
    s_page_refreshes = 0;
    ESP_LOGW(TAG, "line queue underrun, pclk back to %d MHz, recovery=%d", DISPLAY_PCLK_SAFE_MHZ, recovered);
}

// 固定锁顺序：显示/OTA锁→供电锁→I2C；组内保留独立空闲期限，最外层结束即关电。
// Lock order is display/OTA -> power -> I2C; groups retain independent idle deadlines and shut down at their outermost end.
bool display_present_begin(void) {
    bool held = pico_online_display_begin();
    epd_power_session_begin();
    return held;
}
void display_present_end(bool held) {
    epd_power_session_end();
    pico_online_display_end(held);
}

// 连续相位全部完成后，back已是真实黑白画面；不因尚未补灰阶而多做一次整屏GC16。
// Once continuous phases finish, back is the real binary frame; pending gray restoration alone does not require full GC16.
static bool display_needs_full(void) { return s_full_refresh_requested || s_power_fault; }

enum EpdDrawError update_display_white_restore(EpdiyHighlevelState* hl, EpdRect area) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_white_restore_unlocked(hl, area);
    display_present_end(held);
    return result;
}

// 统一硬件出口，递归调用也必须覆盖整个扫描过程。/ Guard complete scans, including recursive fallback paths.
enum EpdDrawError update_display_mode(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_unlocked(hl, mode);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_mode_diff(
    EpdiyHighlevelState* hl, enum EpdDrawMode mode
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_diff_unlocked(hl, mode);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_mode_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_mode_diff_with_unlocked(hl, waveform, mode);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_fast_page(EpdiyHighlevelState* hl) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_fast_page_unlocked(hl);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_from_white_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_with_unlocked(hl, waveform, mode);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_from_white(EpdiyHighlevelState* hl) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_from_white_unlocked(hl);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_white(EpdiyHighlevelState* hl) {
    bool held = display_present_begin();
    enum EpdDrawError result = update_display_white_unlocked(hl);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_full(EpdiyHighlevelState* hl) {
    bool held = display_present_begin();
    enum EpdDrawError result = update_display_full_unlocked(hl);
    display_present_end(held);
    return result;
}

// 屏幕修复的遍数与单帧时长：遍数是盲刷黑/白走几轮，越多越彻底、整段也越久。
// Panel repair passes and per-frame time: the count is how many black/white rounds the blind drive
// walks -- more is more thorough and takes longer.
#define DISPLAY_REPAIR_CYCLES 6
#define DISPLAY_REPAIR_TIME 15
// 掉电保持时长：够面板把停在半帧的像素放掉，又不至于让用户等。
// How long the rails stay down: enough for pixels stuck mid-frame to let go, short enough to wait.
#define DISPLAY_REPAIR_OFF_MS 400

// 面板修复：整屏盲驱动，不看 framebuffer。刷新中途断电会让面板停在半帧状态，而这里所有常规
// 刷新都是差分的（只重写与 RAM 上一帧不同的像素与相位），停在半路的那些行永远轮不到重写——
// 现象就是怎么刷新都留着一道条纹。这条路把每一行按同一电平重新驱动几轮，最后停在白色。
// Panel repair: a blind full-screen drive that ignores the framebuffer. A power cut in the middle of
// a refresh leaves the panel stuck mid-frame, and every ordinary path here is differential -- it
// rewrites only what differs from the RAM frame -- so those rows are never driven again and the
// stripe survives every refresh. This drives every row to the same level for several rounds and ends
// on white.
enum EpdDrawError display_panel_repair(EpdiyHighlevelState* hl) {
    if (!hl || !hl->front_fb || !hl->back_fb) return EPD_DRAW_INVALID_CROP;
    bool held = display_present_begin();
    s_full_refresh_requested = s_complete_refresh_requested = false;
    display_main_transition_cancel();
    // 先真掉一次电：VCOM/VGH/VGL 重新建立，停在半路的像素失去保持电压。
    // One real power cycle first: VCOM/VGH/VGL are rebuilt and pixels stuck mid-frame lose their hold.
    epd_poweroff();
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_REPAIR_OFF_MS));
    use_scan_for(&E0470_WAVEFORM, MODE_GC16);
    if (!display_power_ready()) {
        display_present_end(held);
        return DISPLAY_DRAW_POWER_NOT_READY;
    }
    // 盲刷黑/白：与 framebuffer 无关；每轮之间让出 CPU，整段要几秒。
    // Blind black/white drive, independent of the framebuffer; the CPU is released between rounds
    // because the whole pass takes seconds.
    const EpdRect area = epd_full_screen();
    for (int cycle = 0; cycle < DISPLAY_REPAIR_CYCLES; ++cycle) {
        for (int i = 0; i < 10; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 0);
        for (int i = 0; i < 13; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 1);
        for (int i = 0; i < 3; ++i) epd_push_pixels(area, DISPLAY_REPAIR_TIME, 2);
        vTaskDelay(1);
    }
    // 面板刚被盲刷成白：把「面板显示的是白」这件事也写进基准，然后整屏推一次当前画面，连底栏
    // 一起推回去。这里不能用 epd_hl_set_all_white()——它清的是绘制缓冲（页面画在 front_fb 上），
    // 会把用户正看的那一页擦掉；修复只该动基准，画面由这一记整屏刷新重新落到面板上。
    // The blind drive left the panel white: the baseline is told so, and one full update pushes the
    // current frame -- navigation bar included -- back onto it. epd_hl_set_all_white() must not be
    // used here: it clears the *drawing* buffer (pages draw into front_fb) and would wipe the page
    // the user is looking at. The repair touches the baseline only; this full update re-lands the
    // frame on the panel.
    // 直接走受锁保护的整屏白底出口，包含底栏；不能被主页裁剪拦截，也不再重复推页。
    // Use the locked full-frame white-baseline path including navigation, without a main-page crop or duplicate page push.
    enum EpdDrawError result = update_display_from_white_with_unlocked(hl, &E0470_WAVEFORM, MODE_GC16);
    if (result != EPD_DRAW_SUCCESS) result = main_recover(hl, result);
    s_main_fault = result != EPD_DRAW_SUCCESS;
    if (s_main_fault) s_full_refresh_requested = true;
    display_power_recovered(result);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_image_gray(EpdiyHighlevelState* hl) {
    bool held = display_present_begin();
    enum EpdDrawError result = s_complete_refresh_requested
        ? update_display_full_unlocked(hl) : update_display_image_gray_unlocked(hl);
    if (result == EPD_DRAW_SUCCESS) s_full_refresh_requested = false;
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_with_unlocked(hl, waveform, mode);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_area_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_with_unlocked(hl, waveform, mode, area);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_area_diff_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_diff_with_unlocked(hl, waveform, mode, area);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_area_diff_padded(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area, int pad_lines, int pad_col_bytes
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl)
        : update_display_area_diff_padded_unlocked(hl, waveform, mode, area, pad_lines, pad_col_bytes);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_click_feedback(EpdiyHighlevelState* hl,EpdRect area) {
    bool held=display_present_begin();
    // 控件只扫自身；显示成功后还原未消费的主页许可，不缓存或回放页面像素。
    // Scan only the control, then restore the unused main permission without caching or replaying page pixels.
    const void* owner=s_main_armed;
    bool shelf=s_main_shelf_page,exit=s_main_shelf_exit,changing=s_main_changing_page;
    EpdRect bands[3];memcpy(bands,s_main_bands,sizeof(bands));
    unsigned count=s_main_band_count;
    bool full=display_needs_full(),restore=owner&&!s_main_fault&&!full;
    display_main_transition_disarm();
    enum EpdDrawError result=full?update_display_full_unlocked(hl):
        update_display_area_diff_with_unlocked(hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
    if(restore&&result==EPD_DRAW_SUCCESS&&!s_main_fault) {
        s_main_armed=owner;s_main_shelf_page=shelf;s_main_shelf_exit=exit;
        s_main_changing_page=changing;s_main_band_count=count;memcpy(s_main_bands,bands,sizeof(bands));
    }
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_area_full_with(
    EpdiyHighlevelState* hl, const EpdWaveform* waveform, enum EpdDrawMode mode,
    EpdRect area
) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_area_full_with_unlocked(hl, waveform, mode, area);
    display_present_end(held);
    return result;
}

enum EpdDrawError update_display_water_turn(EpdiyHighlevelState* hl, EpdRect area,
                                             e0470_turn_dir_t dir) {
    bool held = display_present_begin();
    enum EpdDrawError result = display_needs_full()
        ? update_display_full_unlocked(hl) : update_display_water_turn_unlocked(hl, area, dir);
    display_present_end(held);
    return result;
}

void guard_draw_result(EpdiyHighlevelState* hl, enum EpdDrawError result) {
    bool held = display_present_begin();
    guard_draw_result_unlocked(hl, result);
    display_present_end(held);
}
