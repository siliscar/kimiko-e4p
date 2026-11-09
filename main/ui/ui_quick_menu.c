/* SPDX-License-Identifier: Apache-2.0
 * 中文：全宽浅顶部层，下圆角与四个无文字圆钮；有前光的板把第四枚圆钮画成太阳
 * 开关。灯灭时层高只有圆钮区；开灯后圆钮下方展开两条横向胶囊滑杆，直接浮在遮
 * 罩上：无卡片、无文字、无旋钮，图标在胶囊内左端，白色填充从左缘生长。灯灭时
 * 开层后停顿再继续下拉可临时展开胶囊，只属本次开层；一次连续快滑只开层。
 * English: A shallow full-width top layer with rounded bottom corners and four
 * unlabeled circles; boards with a frontlight draw a sun switch in the fourth
 * circle. While the light is off the layer keeps only the circle band; switching
 * it on expands two horizontal capsule sliders below the circles, floating on
 * the shade: no card, text or knob, the icon inside the left end and a white
 * fill growing from the left edge. With the light off, pausing after the open
 * and then pulling further down expands the capsules for that opening only;
 * one continuous fast swipe only opens, never expands.
 * 用户修订：常驻大滑杆先改为随开关展开的卡片行，再按用户参考图去掉卡片容器，
 * 改成控制中心式横向胶囊；亮度用半填充圆图标，色温用温度计图标；填充不足一个
 * 帽宽时按行收进左端半圆。
 * User revision: the always-present sliders first became a card of rows, then per
 * the user's reference the card was dropped for control-centre style horizontal
 * capsules; brightness uses the half-filled circle and colour temperature the
 * thermometer; a fill narrower than one cap tucks into the left cap row by row.
 * 用户修订：面板用不透明白底，圆键仅黑白及固定灰；滑杆使用黑白填充与固定白底图标。
 * User revision: opaque white panels, black/white/fixed-gray keys, and binary sliders with fixed white icon disks.
 * 冻结：不访问硬件、设置或刷新，不申请帧缓存；只在展开/状态变化后由主循环调用绘图。
 * Frozen: No hardware, settings, presentation or frame allocations; the loop draws only on opening or state changes.
 */
#include "ui_quick_menu.h"
#include <stdlib.h>
#include "ui_kit.h"
#include "ui_nav.h"

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }
static EpdRect key_rect(int button) {
    return (EpdRect){center_x(button)-UI_QUICK_RADIUS,UI_QUICK_CIRCLE_Y-UI_QUICK_RADIUS,
                     2*UI_QUICK_RADIUS,2*UI_QUICK_RADIUS};
}
static const ui_icon_t s_icons[]={UI_ICON_WIFI,UI_ICON_BLUETOOTH,UI_ICON_REFRESH_CW};

static bool layer_pixel(int x, int y, int width, int height) {
    const int r = 56;
    if (y < height - r || (x >= r && x < width - r)) return true;
    int dx = x < r ? x - r : x - (width - r - 1);
    int dy = y - (height - r - 1);
    return dx * dx + dy * dy < r * r;
}

// 44px锁形沿用细线几何，圆角锁体与上半圆锁梁；不引入生成图标资产。
// A 44px lock follows the thin-line geometry, with a rounded body and arched shackle; no new generated icon asset is needed.
static void lock_icon(uint8_t *fb, int cx, int cy) {
    for (int stroke = 0; stroke < 3; ++stroke) {
        ui_draw_round_rect(fb, (EpdRect){cx - 14 + stroke, cy - 1 + stroke,
                                      28 - stroke * 2, 23 - stroke * 2}, 4 - stroke, UI_GRAY_BLACK);
        int radius = 9 - stroke;
        for (int x = -radius; x <= radius; ++x) {
            int y = 0;
            while ((y + 1) * (y + 1) + x * x <= radius * radius) ++y;
            epd_draw_pixel(cx + x, cy - 10 - y, UI_GRAY_BLACK, fb);
            if (abs(x) == radius) epd_draw_line(cx + x, cy - 10, cx + x, cy - 1, UI_GRAY_BLACK, fb);
        }
    }
    ui_fill_rect_fast((EpdRect){cx - 1, cy + 8, 3, 7}, UI_GRAY_BLACK, fb);
}

/* ---- 圆键 / Circle keys ---- */

static void draw_badge(uint8_t *fb,int button,bool active) {
    if(button>1||!active)return;
    int cx=center_x(button);
    epd_fill_circle(cx+30,UI_QUICK_CIRCLE_Y-30,5,UI_GRAY_WHITE,fb);
    epd_draw_circle(cx+30,UI_QUICK_CIRCLE_Y-30,5,UI_GRAY_BLACK,fb);
}

// 圆键只用固定灰活动态和白色普通态，按压增加黑色内圈。
// Keys use fixed gray when active and white otherwise; pressing adds a black inner ring.
static void draw_key(uint8_t *fb,int button,const ui_quick_menu_t *menu,bool active,bool pressed) {
    int cx=center_x(button);
    const bool frontlight_key=button==3&&menu!=NULL&&menu->frontlight;
    epd_fill_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS,active?UI_GRAY_LIGHT:UI_GRAY_WHITE,fb);
    epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS,UI_GRAY_BLACK,fb);
    epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-1,UI_GRAY_BLACK,fb);
    if(button<3) ui_draw_icon(fb,cx,UI_QUICK_CIRCLE_Y,44,s_icons[button],UI_GRAY_BLACK);
    else if(frontlight_key) ui_draw_icon(fb,cx,UI_QUICK_CIRCLE_Y,44,UI_ICON_SUN,UI_GRAY_BLACK);
    else lock_icon(fb,cx,UI_QUICK_CIRCLE_Y);
    if(pressed){
        epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-7,UI_GRAY_BLACK,fb);
        epd_draw_circle(cx,UI_QUICK_CIRCLE_Y,UI_QUICK_RADIUS-8,UI_GRAY_BLACK,fb);
    }
}

/* ---- 前光胶囊 / Frontlight capsules ---- */

// 胶囊在开灯或本次开层下拉临时展开时存在：层高随之伸缩，熄灯点按即收起。
// The capsules exist while the light is on or the layer was pulled open this
// session: the layer height follows, and the light-off tap collapses them.
static bool fl_rows_visible(const ui_quick_menu_t *menu) {
    return menu != NULL && menu->frontlight && (menu->fl_on || menu->fl_expanded);
}
static EpdRect fl_row_rect(ui_quick_control_t control) {
    // 行矩形只作为局刷范围：胶囊整片自绘，行内胶囊以外的像素保持遮罩不动。
    // The row rectangle is only the local refresh scope: the capsule paints its
    // whole body, so pixels outside it in the row keep the shade untouched.
    if (control == UI_QUICK_CTL_BRIGHTNESS)
        return (EpdRect){UI_QUICK_FL_ROW_X, UI_QUICK_FL_BRI_CY - UI_QUICK_FL_ROW_H / 2,
                         UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    if (control == UI_QUICK_CTL_CCT)
        return (EpdRect){UI_QUICK_FL_ROW_X, UI_QUICK_FL_CCT_CY - UI_QUICK_FL_ROW_H / 2,
                         UI_QUICK_FL_ROW_W, UI_QUICK_FL_ROW_H};
    return (EpdRect){0};
}
static int fl_thumb_x(uint8_t value) {
    return UI_QUICK_FL_TRACK_X + (UI_QUICK_FL_TRACK_W * value) / 100;
}

// 左端半圆帽在当前行的内缩量；整数解且偏保守，填充不出轨道的胶囊端。
// Per-row inset of the left semicircular cap; integer and conservative, so the fill never leaves the capsule end.
static int cap_inset(int offset) {
    const int r = UI_QUICK_FL_TRACK_H / 2;
    int chord = 0;
    while ((chord + 1) * (chord + 1) + offset * offset <= r * r) ++chord;
    return r - chord;
}

// 胶囊使用黑底白色填充与固定白底图标，移动时只有黑白像素改变。
// Capsules use black tracks, white fills and fixed white icon disks; motion changes black/white pixels only.
static void draw_capsule(uint8_t *fb, int cy, ui_icon_t icon, uint8_t value) {
    const int r = UI_QUICK_FL_TRACK_H / 2;
    const EpdRect track = {UI_QUICK_FL_TRACK_X, cy - r, UI_QUICK_FL_TRACK_W, UI_QUICK_FL_TRACK_H};
    ui_fill_round_rect_raw(fb, track, r, UI_GRAY_BLACK);
    const int width = fl_thumb_x(value) - track.x;
    if (width >= UI_QUICK_FL_TRACK_H) {
        ui_fill_round_rect_raw(fb, (EpdRect){track.x, cy - r, width, UI_QUICK_FL_TRACK_H}, r, UI_GRAY_WHITE);
    } else if (width > 0) {
        // 不足一个帽宽的填充按行收进左端半圆，与轨道的圆端保持同形。
        // A fill narrower than one cap tucks into the left cap row by row, matching the track's rounded end.
        for (int dy = 0; dy < UI_QUICK_FL_TRACK_H; ++dy) {
            const int inset = cap_inset(dy < r ? r - dy : dy - r);
            if (inset >= width) continue;
            ui_fill_rect_fast((EpdRect){track.x + inset, cy - r + dy, width - inset, 1},
                             UI_GRAY_WHITE, fb);
        }
    }
    epd_fill_circle(UI_QUICK_FL_ICON_CX, cy, UI_QUICK_FL_ICON_SIZE / 2 + 8, UI_GRAY_WHITE, fb);
    ui_draw_icon(fb, UI_QUICK_FL_ICON_CX, cy, UI_QUICK_FL_ICON_SIZE, icon, UI_GRAY_BLACK);
    ui_draw_round_rect_raw(fb, track, r, UI_GRAY_BLACK);
    ui_draw_round_rect_raw(fb, ui_inset_rect(track, 1), r - 1, UI_GRAY_BLACK);
}

static void draw_fl_row_at(uint8_t *fb, ui_quick_control_t control, uint8_t value) {
    // 亮度用半填充圆图标，与前光开关的太阳明显不同；色温用温度计。
    // Brightness uses the half-filled circle, clearly unlike the sun switch; colour temperature uses the thermometer.
    if (control == UI_QUICK_CTL_BRIGHTNESS)
        draw_capsule(fb, UI_QUICK_FL_BRI_CY, UI_ICON_CONTRAST, value);
    else if (control == UI_QUICK_CTL_CCT)
        draw_capsule(fb, UI_QUICK_FL_CCT_CY, UI_ICON_THERMOMETER, value);
}

static void draw_fl_row(uint8_t *fb, const ui_quick_menu_t *menu, ui_quick_control_t control) {
    draw_fl_row_at(fb, control,
                   control == UI_QUICK_CTL_BRIGHTNESS ? menu->fl_bri : menu->fl_cct);
}

void ui_quick_menu_end(void) {}

bool ui_quick_menu_draw_button(uint8_t *fb,const ui_quick_menu_t *menu,int button,bool active,bool pressed,EpdRect *dirty) {
    if(!fb||!dirty||button<0||button>=4||epd_rotated_display_width()<UI_LOCK_WIDTH||epd_rotated_display_height()<UI_QUICK_HEIGHT)return false;
    if(menu!=NULL&&menu->frontlight&&button==3)active=menu->fl_on;
    draw_key(fb,button,menu,active,pressed);
    draw_badge(fb,button,active);
    *dirty=key_rect(button);
    return true;
}

bool ui_quick_menu_draw_control(uint8_t *fb, const ui_quick_menu_t *menu,
                                ui_quick_control_t control, EpdRect *dirty) {
    if (!fb || !fl_rows_visible(menu) || !dirty) return false;
    const EpdRect row = fl_row_rect(control);
    if (row.width <= 0) return false;
    draw_fl_row(fb, menu, control);
    *dirty = row;
    return true;
}

bool ui_quick_menu_draw_control_white(uint8_t *fb, const ui_quick_menu_t *menu,
                                      ui_quick_control_t control, EpdRect *dirty) {
    if (!fb || !fl_rows_visible(menu) || !dirty) return false;
    const EpdRect row = fl_row_rect(control);
    if (row.width <= 0 || control < UI_QUICK_CTL_BRIGHTNESS) return false;
    // 定稿第一段：白填充长到本次弄脏的上沿，底色区域先归一到已知白。
    // Settle stage one: grow the white fill to the dirty extent so the body
    // region is normalized to known white first.
    draw_fl_row_at(fb, control,
                   control == UI_QUICK_CTL_CCT ? menu->fl_cct_dirty : menu->fl_bri_dirty);
    *dirty = row;
    return true;
}

void ui_quick_menu_draw_status(uint8_t *fb) {
    ui_fill_rect_fast((EpdRect){0,0,epd_rotated_display_width(),76},UI_GRAY_WHITE,fb);
    ui_nav_status(fb);
}

EpdRect ui_quick_menu_rect(const ui_quick_menu_t *menu) {
    return (EpdRect){0, 0, UI_LOCK_WIDTH,
                     fl_rows_visible(menu) ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT};
}

void ui_quick_menu_draw(uint8_t *fb,const ui_quick_menu_t *menu,bool wifi,bool bluetooth) {
    if(!fb)return;
    const int width = epd_rotated_display_width();
    const int height = epd_rotated_display_height();
    const int layer_height = fl_rows_visible(menu)
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT;
    // 不透明白底不混合底页，避免叠层产生额外灰阶。
    // Opaque white never blends the underlay into additional gray levels.
    for (int y = 0; y < layer_height && y < height; ++y) for (int x = 0; x < width; ++x)
        if (layer_pixel(x, y, width, layer_height)) epd_draw_pixel(x, y, UI_GRAY_WHITE, fb);
    // 状态栏留清晰白底，沿用系统时间/签名/电池；四图标与底栏共用Lucide细线44px。
    // Keep the system time/signature/battery legible on white; share navigation's 44px Lucide line icons.
    ui_quick_menu_draw_status(fb);
    for (int i = 0; i < 4; ++i) {
        bool active = (i == 0 && wifi) || (i == 1 && bluetooth) ||
            (i == 3 && menu != NULL && menu->frontlight && menu->fl_on);
        draw_key(fb, i, menu, active, false);
        draw_badge(fb, i, active);
    }
    // 胶囊滑杆只在开灯或本次开层下拉展开时出现，直接浮在遮罩上；熄灯点按或
    // 没有前光的板保持原来的 264 高度。
    // The capsule sliders appear while the light is on or the layer was pulled
    // open this session, floating on the shade; the light-off tap and boards
    // without a frontlight keep the original 264 height.
    if (fl_rows_visible(menu)) {
        draw_fl_row(fb, menu, UI_QUICK_CTL_BRIGHTNESS);
        draw_fl_row(fb, menu, UI_QUICK_CTL_CCT);
    }
    ui_fill_round_rect(fb, (EpdRect){width / 2 - 26, layer_height - 24, 52, 6}, 3, UI_GRAY_BLACK);
    // 只画遮罩内的双像素轮廓，顶部直角、底部圆角，不越过下层边界。
    // Draw a two-pixel outline within the mask: square top, rounded bottom, no underlay leakage.
    for (int y = 0; y < layer_height && y < height; ++y) for (int x = 0; x < width; ++x) {
        if (layer_pixel(x, y, width, layer_height) && (x < 2 || x >= width - 2 || y < 2 ||
            y >= layer_height - 2 || !layer_pixel(x - 2, y, width, layer_height) ||
            !layer_pixel(x + 2, y, width, layer_height) || !layer_pixel(x, y + 2, width, layer_height)))
            epd_draw_pixel(x, y, UI_GRAY_BLACK, fb);
    }
}
