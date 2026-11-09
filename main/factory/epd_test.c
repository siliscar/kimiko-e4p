/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * EPD bring-up 测试图案：灰阶、棋盘、细线、方向、边框与连续局刷。
 *
 * EPD bring-up test pattern: gray ramp, checkerboard, thin lines, orientation,
 * border and repeated partial updates. Enabled only by CONFIG_MOGO_EPD_TEST_PATTERN.
 */

#include "epd_test.h"

#include "e0470_epaper_waveform.h"
#include "epd_highlevel.h"
#include "epdiy.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "epd_test"

static void draw_gray_bars(uint8_t* fb, int x, int y, int w, int bar_h) {
    for (int level = 0; level < 16; level++) {
        EpdRect bar = {.x = x, .y = y + level * bar_h, .width = w, .height = bar_h};
        epd_fill_rect(bar, (uint8_t)level, fb);
    }
}

static void draw_checkerboard(uint8_t* fb, int x, int y, int size) {
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            epd_draw_pixel(x + i, y + j, ((i + j) & 1) ? 0 : 15, fb);
        }
    }
}

static void draw_thin_lines(uint8_t* fb, int x, int y, int size) {
    for (int i = 0; i < size; i += 2) {
        epd_draw_line(x + i, y, x + i, y + size - 1, 0, fb);
    }
    for (int i = 0; i < size; i += 4) {
        epd_draw_line(x, y + i, x + size - 1, y + i, 0, fb);
    }
}

static void draw_corner_cross(uint8_t* fb, int x, int y, int r) {
    epd_draw_line(x - r, y, x + r, y, 0, fb);
    epd_draw_line(x, y - r, x, y + r, 0, fb);
}

static void draw_pattern(uint8_t* fb, int w, int h) {
    const int bar_h = 30;
    draw_gray_bars(fb, 0, 0, w, bar_h);

    const int region_y = 16 * bar_h + 16;
    draw_checkerboard(fb, 24, region_y, 96);
    draw_thin_lines(fb, 148, region_y, 96);

    draw_corner_cross(fb, 24, region_y + 160, 24);
    draw_corner_cross(fb, w - 24, region_y + 160, 24);
    draw_corner_cross(fb, 24, h - 80, 24);
    draw_corner_cross(fb, w - 24, h - 80, 24);

    epd_draw_circle(w / 2, h / 2, 100, 0, fb);
    epd_draw_circle(w / 2, h / 2, 99, 0, fb);

    // 1 px 屏幕边框，检查 BORDER/XOE 时序。
    // 1 px panel border to check BORDER/XOE timing.
    EpdRect border = {.x = 0, .y = 0, .width = w, .height = h};
    epd_draw_rect(border, 0, fb);
}

void epd_test_run(EpdiyHighlevelState* hl, uint8_t* fb) {
    if (hl == NULL || fb == NULL) return;

    const int w = epd_rotated_display_width();
    const int h = epd_rotated_display_height();
    ESP_LOGW(TAG, "EPD test pattern start (%dx%d, rot=%d)", w, h, (int)epd_get_rotation());

    epd_poweron();
    epd_clear();
    epd_hl_set_all_white(hl);
    draw_pattern(fb, w, h);

    enum EpdDrawError err = epd_hl_update_screen_full(hl, MODE_GC16, 25);
    ESP_LOGW(TAG, "GC16 full update: 0x%x", (unsigned)err);

    // 连续 20 次 GL16 局刷，检查前导跳过与残影。
    // 20 GL16 area updates to check leading skip and ghosting.
    EpdRect area = {.x = w / 2 - 40, .y = h - 160, .width = 80, .height = 80};
    for (int i = 0; i < 20; i++) {
        epd_fill_rect(area, (i & 1) ? 15 : 0, fb);
        err = epd_hl_update_area_padded(hl, MODE_GL16, 25, area, 1, 1);
        if (err != EPD_DRAW_SUCCESS) {
            ESP_LOGE(TAG, "GL16 update %d failed: 0x%x", i, (unsigned)err);
            break;
        }
    }
    ESP_LOGW(TAG, "EPD test pattern done (last err=0x%x)", (unsigned)err);

    epd_poweroff();
    vTaskDelay(pdMS_TO_TICKS(10000));
}
