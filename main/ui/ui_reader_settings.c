/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从 app_book 提取的阅读设置绘制与共用几何；调用方持有设置和页面状态。
 * English: Reader-setting painting and shared geometry extracted from app_book; callers own settings and page state.
 */
#include "ui_reader_settings.h"
#include "ui_kit.h"
#include <stdio.h>

static int settings_extra(unsigned scale, int maximum) {
    if (scale < 100) scale = 100;
    if (scale > 200) scale = 200;
    return (int)(scale - 100) * maximum / 100;
}

int ui_reader_settings_top(unsigned system_font_size) {
    return 580 - settings_extra(system_font_size, 204);
}

EpdRect ui_reader_settings_rect(unsigned system_font_size, int index) {
    int extra=settings_extra(system_font_size,56), footer=settings_extra(system_font_size,36);
    if (index < 0 || index >= 8) return (EpdRect){0, 0, 0, 0};
    int row=index/2;
    // 八项排成四行双列：页眉页脚恰好补齐第四行右格，行距 104，四行底部仍留在「完成」之上。
    // Eight items fill four two-column rows: the chrome entry completes the fourth row's right
    // cell; a 104 pitch keeps every row clear of Done.
    return (EpdRect){36+(index%2)*314,702+row*104-(3-row)*extra-footer,298,98+extra};
}

void ui_reader_settings_switch(uint8_t *fb, EpdRect track, bool on) {
    ui_fill_round_rect(fb, track, 19, on ? 0x38 : 0xc4);
    int cx = track.x + (on ? 49 : 20);
    epd_fill_circle(cx, track.y + 19, 16, UI_GRAY_WHITE, fb);
    epd_draw_circle(cx, track.y + 19, 16, UI_GRAY_BLACK, fb);
}

static void draw_reading_card(uint8_t *fb, unsigned scale, int index, const char *title,
                              const char *value, bool cycle) {
    EpdRect card = ui_reader_settings_rect(scale,index);
    ui_fill_round_rect(fb, card, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, card, 20, 0x68);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+6,card.width-42,card.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
    char shown[48]; snprintf(shown,sizeof(shown),"%s",value);
    ui_text_box(fb,(EpdRect){card.x+21,card.y+card.height/2,222,card.height/2-8},26,shown,EPD_DRAW_ALIGN_LEFT,false);
    ui_draw_icon(fb,card.x+272,card.y+3*card.height/4,26,
                 cycle ? UI_ICON_ROTATE_CW : UI_ICON_CHEVRON_RIGHT, 0x38);
}

static void draw_reading_toggle(uint8_t *fb, unsigned scale, int index, const char *title,
                                const char *detail, bool on) {
    EpdRect row = ui_reader_settings_rect(scale,index);
    ui_fill_round_rect(fb, row, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, row, 20, 0x68);
    if (detail) {
        // 半宽格放两行：标题与说明各降一号字，开关仍留在右端。
        // A half-width cell carries two lines: title and detail step down a size, the switch stays right.
        ui_text_box(fb,(EpdRect){row.x+20,row.y+10,row.width-40,row.height/2-16},21,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){row.x+20,row.y+row.height/2,row.width-40,row.height/2-10},17,detail,EPD_DRAW_ALIGN_LEFT,false);
        ui_reader_settings_switch(fb,(EpdRect){row.x+row.width-90,row.y+3*row.height/4-19,69,39},on);
    } else {
        ui_text_box(fb,(EpdRect){row.x+21,row.y+6,row.width-42,row.height/2-12},23,title,EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){row.x+21,row.y+row.height/2,166,row.height/2-8},26,on?"开启":"关闭",EPD_DRAW_ALIGN_LEFT,false);
        ui_reader_settings_switch(fb,(EpdRect){row.x+209,row.y+3*row.height/4-19,69,39},on);
    }
}

// 这两个面板的居中文字沿用页面的原有留白与行高。/ Keep the page's original margins and line height for centered labels in these sheets.
static void draw_centered_label(uint8_t *fb, int x, int cy, int px, const char *text, bool inverted) {
    int width = 2 * (x < 342 ? x - 42 : 642 - x);
    ui_text_box(fb, (EpdRect){x - width / 2, cy - 22, width, 44}, px, text, EPD_DRAW_ALIGN_CENTER, inverted);
}

void ui_reader_settings_draw(uint8_t *fb, const ui_reader_settings_values_t *values) {
    char interval[24];
    unsigned pages = values->full_pages;
    if (pages) snprintf(interval, sizeof(interval), "每 %u 页", pages);
    else snprintf(interval, sizeof(interval), "%s", "不强制");
    draw_reading_card(fb, values->system_font_size, 0, "自动全刷", interval, true);
    draw_reading_card(fb, values->system_font_size, 1, "翻页效果", values->turn_effect ? "水波纹" : "普通", true);
    draw_reading_card(fb, values->system_font_size, 2, "翻页模式", values->vertical_turn ? "上下翻页" : "左右翻页", false);
    draw_reading_card(fb, values->system_font_size, 3, "按键控制", "自定义按键", false);
    draw_reading_toggle(fb, values->system_font_size, 4, "状态栏", NULL, values->statusbar);
    draw_reading_toggle(fb, values->system_font_size, 5, "关闭书内图片", "跳过正文插图，保留原书文件", values->hide_images);
    draw_reading_toggle(fb, values->system_font_size, 6, "竖排（从右到左）", NULL, values->vertical_text);
    draw_reading_card(fb, values->system_font_size, 7, "页眉页脚", "自定义内容", false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    draw_centered_label(fb, 342, 1170, 26, "完成", true);
}

void ui_reader_turn_settings_draw(uint8_t *fb, bool vertical, bool shake) {
    for (int i = 0; i < 2; ++i) {
        int y = 370 + i * 322;
        bool selected = vertical == (i != 0);
        EpdRect card = {36, y, 612, 292};
        ui_fill_round_rect(fb, card, 22, selected ? 0xe0 : 0xf0);
        ui_draw_control_frame(fb, card, 22, 0x50);
        ui_draw_control_frame(fb, (EpdRect){37, y + 1, 610, 290}, 21, 0x50);
        EpdRect diagram = {64, y + 37, 182, 220};
        ui_fill_round_rect(fb, diagram, 12, UI_GRAY_WHITE);
        ui_draw_control_frame(fb, diagram, 12, 0x58);
        if (i) {
            ui_hairline(fb, diagram.y + 73, 65, 180, 0x58);
            draw_centered_label(fb, 155, diagram.y + 36, 21, "上一页", false);
            draw_centered_label(fb, 155, diagram.y + 147, 21, "下一页", false);
        } else {
            ui_fill_rect_fast((EpdRect){155, diagram.y + 1, 1, 218}, UI_GRAY_BLACK, fb);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+94-15,78,30},21,"上一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){110-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+94-15,78,30},21,"下一",EPD_DRAW_ALIGN_CENTER,false);
            ui_text_box(fb,(EpdRect){201-39,diagram.y+126-15,78,30},21,"页",EPD_DRAW_ALIGN_CENTER,false);
        }
        ui_text_box(fb,(EpdRect){274,y+43,350,44},32,i ? "上下翻页" : "左右翻页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+112,350,44},23,i ? "上 1/3：上一页" : "左 1/2：上一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+158,350,44},23,i ? "下 2/3：下一页" : "右 1/2：下一页",EPD_DRAW_ALIGN_LEFT,false);
        ui_text_box(fb,(EpdRect){274,y+213,350,44},21,"左右滑动均可翻页",EPD_DRAW_ALIGN_LEFT,false);
        epd_draw_circle(610, y + 61, 12, UI_GRAY_BLACK, fb);
        if (selected) epd_fill_circle(610, y + 61, 7, UI_GRAY_BLACK, fb);
    }
    // 晃动翻页整行开关：标题、方向说明与右端开关；全屏提示移到行下方。
    // A full-width shake-turn toggle row: title, direction hint and a right-hand switch; the fullscreen hint moves below it.
    EpdRect row = {36, 1000, 612, 88};
    ui_fill_round_rect(fb, row, 20, UI_GRAY_WHITE);
    ui_draw_control_frame(fb, row, 20, 0x68);
    ui_text_box(fb,(EpdRect){row.x+20,row.y+10,row.width-40,row.height/2-14},21,"晃动翻页",EPD_DRAW_ALIGN_LEFT,false);
    ui_text_box(fb,(EpdRect){row.x+20,row.y+row.height/2+2,row.width-40,row.height/2-12},17,"左晃上一页，右晃下一页",EPD_DRAW_ALIGN_LEFT,false);
    ui_reader_settings_switch(fb,(EpdRect){row.x+row.width-90,row.y+row.height/2-19,69,39},shake);
    draw_centered_label(fb, 342, 1110, 21, "工具栏「全屏」按钮切换全屏", false);
    ui_fill_round_rect(fb, (EpdRect){36, 1141, 612, 58}, 20, UI_GRAY_BLACK);
    draw_centered_label(fb, 342, 1170, 26, "完成", true);
}
