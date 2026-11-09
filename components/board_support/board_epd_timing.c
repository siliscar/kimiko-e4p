/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 按像素钟解出行/帧扫描参数。
 *
 * Solve line/frame scan parameters from the pixel clock.
 */

#include "board_epd_timing.h"

#include "e0470_epaper_waveform.h"
#include "epdiy.h"
#include "esp_log.h"

#define TAG "read_pico"

// 原厂 Metalio 时序：L_SL 9、L_BL 4、L_EL 3 时钟，20 MHz 下 16 µs 一行（320 时钟）。
// Vendor Metalio timing: L_SL 9, L_BL 4, L_EL 3 clocks, 16 µs per line (320 clocks) at 20 MHz.
#define LINE_START_MIN_NS 450
#define LINE_BACK_MIN_NS 200
#define LINE_END_MIN_CLK 3

// 原厂帧消隐：起始 1 行、无后肩、结束 3 行；688 行约 11.0 ms（≈90.8 Hz）。
// Vendor frame blanking: 1 start line, no back porch, 3 end lines; 688 lines ≈ 11.0 ms (≈90.8 Hz).
#define FRAME_START_LINES 1
#define FRAME_BACK_LINES 0
#define FRAME_END_LINES 3

// 原厂 CKV 高 6.0 µs；源极整行保持数据，门极只需充电窗口，不要求覆盖整个数据窗。
// Vendor CKV high is 6.0 µs; the source holds the row for the whole line, so the
// gate only needs a charging window rather than the full data window.
#define CKV_LOW_MIN_01US 12
#define CKV_HIGH_MAX_01US 60

static int s_frame_target_us = E0470_WAVEFORM_FRAME_US;
static int s_ckv_high_max_01us = CKV_HIGH_MAX_01US;
static int s_pclk_mhz;
static board_epd_scan_profile_t s_profile = BOARD_EPD_SCAN_FULL;

int board_epd_frame_target_us(void) { return s_frame_target_us; }

void board_epd_set_frame_target_us(int us) { s_frame_target_us = us > 0 ? us : 0; }

int board_epd_ckv_high_max_01us(void) { return s_ckv_high_max_01us; }

void board_epd_set_ckv_high_max_01us(int v) {
    if (v >= CKV_LOW_MIN_01US) s_ckv_high_max_01us = v;
}

static int clampi(int v, int lo, int hi) {
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

static int ceil_div(int n, int d) { return (n + d - 1) / d; }

// ceil(ns × f / 1000)：把一段最小时间换成 f MHz 下的整数钟。
// / ceil(ns × f / 1000): minimum time to integer clocks at f MHz.
static int clocks_for_ns(int ns, int pclk_mhz) { return ceil_div(ns * pclk_mhz, 1000); }

static LcdLineTiming_t scan_to_line(const board_epd_scan_t* s) {
    return (LcdLineTiming_t){
        .le_high_time = s->line_start,
        .line_front_porch = s->line_back_porch,
        .line_end = s->line_end,
        .ckv_high_time = s->ckv_high_01us,
    };
}

static void apply_lcd(const board_epd_scan_t* scan) {
    LcdLineTiming_t line = scan_to_line(scan);
    epd_lcd_set_line_timing(&line);
    epd_set_lcd_pixel_clock_MHz(scan->pclk_mhz);
}

void board_epd_scan(
    int pclk_mhz, board_epd_scan_profile_t profile, board_epd_scan_t* out
) {
    if (out == NULL) return;
    pclk_mhz = clampi(pclk_mhz, BOARD_EPD_PCLK_MIN_MHZ, BOARD_EPD_PCLK_MAX_MHZ);

    const EpdDisplay_t* display = epd_get_display();
    const int p = display->bus_width / 2;  // 每像素 2bit，一钟推满整条总线 / 2 bits/pixel, one clock fills the bus
    const int ldl = display->width / p;    // p 必须整除 H，否则一行落不到整数钟上 / p must divide H or the line is not an integer clock count
    const int vall = FRAME_START_LINES + FRAME_BACK_LINES + display->height + FRAME_END_LINES;
    const int lsl = clocks_for_ns(LINE_START_MIN_NS, pclk_mhz);
    const int lbl = clocks_for_ns(LINE_BACK_MIN_NS, pclk_mhz);

    // 余量给 L_EL，让 L_DL 落在 CKV 高电平里。后肩只取规格下界。
    // / Slack goes to L_EL so L_DL sits inside CKV high. Back porch is the spec floor only.
    int line_us = ceil_div(ldl + lsl + lbl + LINE_END_MIN_CLK, pclk_mhz);
    if (profile == BOARD_EPD_SCAN_FULL) {
        const int target_line_us = (s_frame_target_us + vall / 2) / vall;
        if (target_line_us > line_us) line_us = target_line_us;
    }

    int line_clocks = line_us * pclk_mhz;
    int lel = line_clocks - ldl - lsl - lbl;
    if (lel < LINE_END_MIN_CLK) {
        line_us++;
        line_clocks = line_us * pclk_mhz;
        lel = line_clocks - ldl - lsl - lbl;
    }

    out->pclk_mhz = pclk_mhz;
    out->px_per_clk = p;
    out->line_start = lsl;
    out->line_back_porch = lbl;
    out->line_data = ldl;
    out->line_clocks = line_clocks;
    out->line_end = lel;
    out->line_us = line_us;
    out->frame_lines = vall;
    out->frame_us = line_us * vall;
    out->frame_hz_x100 = 100000000 / out->frame_us;

    // 原厂门极高 6.0 µs；行长不足时收到行长减 1.2 µs，低电平至少 1.2 µs。
    // Vendor gate high is 6.0 µs; on a short line cap at line minus 1.2 µs and
    // keep at least 1.2 µs low.
    out->ckv_high_01us = clampi(s_ckv_high_max_01us, CKV_LOW_MIN_01US,
                                line_us * 10 - CKV_LOW_MIN_01US);
}

int board_epd_refresh_ms(int pclk_mhz, board_epd_scan_profile_t profile, int phases) {
    board_epd_scan_t scan;
    board_epd_scan(pclk_mhz, profile, &scan);
    return (phases * scan.frame_us + 500) / 1000;
}

static void apply_scan(int pclk_mhz, board_epd_scan_profile_t profile) {
    board_epd_scan_t scan;
    board_epd_scan(pclk_mhz, profile, &scan);
    apply_lcd(&scan);
    // 每相开扫前的预填行数决定帧间隙。FULL 档一行 16us，喂线程余量大，32 行实测无欠载，
    // 帧间隙从约 1ms 归零；FAST 档一行只有 10us，32 行会欠载（EPD_DRAW_EMPTY_LINE_QUEUE，
    // 后续帧直接不输出），48/64 实测都没欠载，取 64 留余量。
    // / Prefill lines before each phase set the frame gap. FULL is 16 µs/line,
    // so 32 lines has slack and drops the ~1 ms gap to zero. FAST is 10 µs/line;
    // 32 underruns (EPD_DRAW_EMPTY_LINE_QUEUE, later frames go silent). 48/64
    // both hold; use 64.
    epd_lcd_set_prefill_lines(profile == BOARD_EPD_SCAN_FULL ? 32 : 64);
    s_pclk_mhz = scan.pclk_mhz;
    s_profile = profile;
    ESP_LOGI(
        TAG, "scan %s %d MHz: line %dus (%d+%d+%d+%d clk), frame %d.%02d Hz, GC16 %d phases %d ms",
        profile == BOARD_EPD_SCAN_FULL ? "full" : "fast", scan.pclk_mhz, scan.line_us,
        scan.line_start, scan.line_back_porch, scan.line_data, scan.line_end,
        scan.frame_hz_x100 / 100, scan.frame_hz_x100 % 100, E0470_GC16_FRAMES,
        board_epd_refresh_ms(scan.pclk_mhz, profile, E0470_GC16_FRAMES)
    );
}

void board_epd_init_lcd(
    const lcd_bus_config_t* bus, int bus_width, int width, int height,
    const board_epd_scan_t* scan
) {
    if (bus == NULL || scan == NULL) return;
    LcdEpdConfig_t config = {
        .pixel_clock = (size_t)scan->pclk_mhz * 1000 * 1000,
        .line = scan_to_line(scan),
        .bus_width = bus_width,
        .bus = *bus,
    };
    epd_lcd_init(&config, width, height);
}

void board_epd_set_pclk(int pclk_mhz) { apply_scan(pclk_mhz, s_profile); }

int board_epd_pclk_mhz(void) { return s_pclk_mhz; }

void board_epd_use_scan(board_epd_scan_profile_t profile) {
    if (profile == s_profile) return;
    apply_scan(s_pclk_mhz, profile);
}
