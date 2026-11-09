/* SPDX-License-Identifier: Apache-2.0
 * 中文：顶部边缘识别器只产出快捷动作与前光事件，不访问页面、硬件或刷新。
 * English: The top-edge recognizer emits quick actions and frontlight events
 * without accessing pages, hardware or presentation.
 */
#include "ui_quick_menu.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_menu.h"

// 圆键之后的卡片命中编号；ui_quick_menu_pressed 只把 0..3 当作按住的圆键。
// Card hit ids follow the circles; ui_quick_menu_pressed only treats 0..3 as a held key.
enum { HIT_BRIGHTNESS = 4, HIT_CCT = 5 };

static int center_x(int button) { return (button * 2 + 1) * UI_LOCK_WIDTH / 8; }

// 层高随开灯或临时展开伸缩：熄灯且未下拉展开时胶囊收起，只剩圆钮区，与绘图侧
// ui_quick_menu_rect 一致。
// The layer height follows the light or the session expansion: without either the
// capsule collapses to the circle band, matching ui_quick_menu_rect on the drawing side.
static int layer_height(const ui_quick_menu_t *menu) {
    return menu->frontlight && (menu->fl_on || menu->fl_expanded)
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT;
}

static int circle_hit(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        const int dx = x - center_x(i), dy = y - UI_QUICK_CIRCLE_Y;
        if (dx * dx + dy * dy <= UI_QUICK_HIT_RADIUS * UI_QUICK_HIT_RADIUS) return i;
    }
    return -1;
}

static int hit_test(const ui_quick_menu_t *menu, int x, int y) {
    int key = circle_hit(x, y);
    if (key >= 0) return key;
    // 胶囊行只在开灯或本次开层下拉展开时存在；否则卡片区域不是命中目标，也不
    // 命中第四枚以外的键。
    // Capsule rows exist while the light is on or the layer was pulled open this
    // session; otherwise the card area matches nothing.
    if (!menu->frontlight || (!menu->fl_on && !menu->fl_expanded)) return -1;
    if (x >= 40 && x < UI_LOCK_WIDTH - 40 &&
        y >= UI_QUICK_FL_BRI_HIT_Y0 && y <= UI_QUICK_FL_BRI_HIT_Y1) return HIT_BRIGHTNESS;
    if (x >= 40 && x < UI_LOCK_WIDTH - 40 &&
        y >= UI_QUICK_FL_CCT_HIT_Y0 && y <= UI_QUICK_FL_CCT_HIT_Y1) return HIT_CCT;
    return -1;
}

static uint8_t slider_value(int hit, int x) {
    if (x < UI_QUICK_FL_TRACK_X) x = UI_QUICK_FL_TRACK_X;
    const int right = UI_QUICK_FL_TRACK_X + UI_QUICK_FL_TRACK_W;
    if (x > right) x = right;
    int value = (x - UI_QUICK_FL_TRACK_X) * 100 / UI_QUICK_FL_TRACK_W;
    if (hit == HIT_BRIGHTNESS && value < 1) value = 1;
    return (uint8_t)value;
}

void ui_quick_menu_reset(ui_quick_menu_t *menu) {
    const bool frontlight = menu->frontlight;
    const bool on = menu->fl_on;
    const uint8_t bri = menu->fl_bri ? menu->fl_bri : 40;
    const uint8_t cct = menu->fl_cct;
    const uint8_t bri_dirty = menu->fl_bri_dirty;
    const uint8_t cct_dirty = menu->fl_cct_dirty;
    memset(menu, 0, sizeof(*menu));
    menu->frontlight = frontlight;
    menu->fl_on = on;
    menu->fl_bri = bri;
    menu->fl_cct = cct;
    menu->fl_bri_dirty = bri_dirty;
    menu->fl_cct_dirty = cct_dirty;
    menu->hit = -1;
    menu->drag = UI_QUICK_CTL_NONE;
}

void ui_quick_menu_set_frontlight(ui_quick_menu_t *menu, bool available, bool on,
                                  uint8_t brightness, uint8_t cct) {
    menu->frontlight = available;
    menu->fl_on = on;
    menu->fl_bri = brightness < 1 ? 1 : brightness > 100 ? 100 : brightness;
    menu->fl_cct = cct > 100 ? 100 : cct;
    // 每次开层都回到设置里的开灯状态：上次开层的下拉临时展开不跨层记忆。
    // Every opening returns to the persisted on state: a previous session's
    // pull-down expansion is never remembered.
    menu->fl_expanded = false;
    // 展开层时屏上状态视为已定稿：脏范围从当前档位起算。
    // The on-screen state counts as settled when the layer opens: dirty extents start at the current levels.
    menu->fl_bri_dirty = menu->fl_bri;
    menu->fl_cct_dirty = menu->fl_cct;
}

void ui_quick_menu_cancel_input(ui_quick_menu_t *menu) {
    menu->tracking = false;
    menu->swallow = true;
    menu->fl_pull = false;
    menu->drag = UI_QUICK_CTL_NONE;
}

int ui_quick_menu_pressed(const ui_quick_menu_t *menu) {
    return menu->open && menu->tracking && !menu->moved && menu->hit >= 0 && menu->hit < 4
        ? menu->hit : -1;
}

static void dismiss(ui_quick_menu_t *menu, ui_quick_event_t *event) {
    menu->open = menu->tracking = false;
    menu->swallow = true;
    menu->fl_expanded = false;
    menu->fl_pull = false;
    menu->drag = UI_QUICK_CTL_NONE;
    event->action = UI_QUICK_CLOSE;
}

bool ui_quick_menu_feed(ui_quick_menu_t *menu, const app_ctx_t *ctx, bool allowed,
                        ui_quick_event_t *event) {
    *event = (ui_quick_event_t){0};
    if (!allowed) {
        bool owned = menu->tracking || menu->swallow || menu->open;
        if (menu->open) dismiss(menu, event);
        else ui_quick_menu_reset(menu);
        return owned;
    }
    if (!ctx->touch || (ctx->touch->touched && ctx->touch->count != 1)) {
        bool owned = menu->open || menu->tracking || menu->swallow;
        if (owned) ui_quick_menu_cancel_input(menu);
        return owned;
    }
    if (menu->swallow) {
        // 开层的同一次下拉还在继续：停顿一小会儿、再往下拉过一程才临时展开胶
        // 囊；一次连续快滑的总行程不算「继续下拉」，开层与展开不会连触发。
        // The pull that opened the layer is still going: a pause and then a
        // further pull expands the capsules; one continuous fast swipe's total
        // travel never counts, so opening and expanding cannot chain in one flick.
        if (menu->fl_pull && ctx->touch->touched) {
            const int pull_y = ctx->touch->y;
            if (!menu->fl_pull_settled) {
                if (abs(pull_y - (int)menu->fl_pull_y) > UI_TOUCH_SLOP_PX) {
                    menu->fl_pull_y = ctx->touch->y;
                    menu->fl_pull_ms = ctx->now_ms;
                } else if (ctx->now_ms - menu->fl_pull_ms >= UI_QUICK_PULL_SETTLE_MS) {
                    // 停在圆钮区内才算还抓着遮罩；停在层外是滑过头，不armed。
                    // Resting inside the circle band still holds the shade;
                    // resting below it overshot and disarms the continuation.
                    if (pull_y < layer_height(menu)) {
                        menu->fl_pull_settled = true;
                        menu->fl_pull_x = ctx->touch->x;
                        menu->fl_pull_y = ctx->touch->y;
                    } else {
                        menu->fl_pull = false;
                    }
                }
            } else {
                const int more_dy = pull_y - (int)menu->fl_pull_y;
                const int more_dx = ctx->touch->x - (int)menu->fl_pull_x;
                if (more_dy >= UI_QUICK_SWIPE_PX && more_dy >= abs(more_dx) * 2) {
                    menu->fl_expanded = true;
                    menu->fl_pull = false;
                    event->action = UI_QUICK_EXPAND;
                    return true;
                }
            }
        }
        if (!ctx->touch->touched) menu->swallow = menu->fl_pull = false;
        return true;
    }
    int x = ctx->touch->x, y = ctx->touch->y;
    if (ctx->pressed && ctx->touch->touched) {
        if (!menu->open && y > UI_QUICK_EDGE_PX) return false;
        if (menu->open && ui_key_hit_test(x, y) >= 0) {
            dismiss(menu, event);
            return true;
        }
        menu->tracking = true;
        menu->moved = false;
        menu->x0 = x; menu->y0 = y;
        menu->started_ms = ctx->now_ms;
        menu->hit = menu->open ? hit_test(menu, x, y) : -1;
        menu->drag = UI_QUICK_CTL_NONE;
    }
    if (!menu->tracking) return menu->open;
    int dx = x - (int)menu->x0, dy = y - (int)menu->y0;
    if (ctx->now_ms < menu->started_ms) {
        ui_quick_menu_cancel_input(menu);
        return true;
    }
    if (abs(dx) > UI_TOUCH_SLOP_PX || abs(dy) > UI_TOUCH_SLOP_PX) menu->moved = true;
    // WiFi / 蓝牙圆键按住 0.5 秒：关层并请求对应设置页，抬手不再触发点按。手指允许
    // 在圆键命中半径内轻微漂移；层状态先收起，由主循环在同轮末尾切页，swallow
    // 保证这条接触序列不落到新页面。
    // Holding the WiFi / Bluetooth circle for 0.5 s closes the layer and requests
    // its settings page; the release no longer taps. The finger may drift inside
    // the circle's hit radius. The layer closes first and the loop switches pages
    // at the end of the tick; swallow keeps this contact stream from reaching the
    // new page.
    const int hx = x - (int)menu->x0, hy = y - (int)menu->y0;
    if (menu->open && (menu->hit == 0 || menu->hit == 1) &&
        hx * hx + hy * hy <= UI_QUICK_HIT_RADIUS * UI_QUICK_HIT_RADIUS &&
        ctx->now_ms - menu->started_ms >= UI_QUICK_HOLD_SETTINGS_MS) {
        menu->open = menu->tracking = false;
        menu->swallow = true;
        menu->fl_pull = false;
        menu->drag = UI_QUICK_CTL_NONE;
        event->action = menu->hit == 0 ? UI_QUICK_WIFI_SETTINGS
                                       : UI_QUICK_BLUETOOTH_SETTINGS;
        return true;
    }
    // 滑杆按下即定位：不等拖动阈值，手指落在哪一档就应用到哪一档。
    // A slider positions itself on touch: apply the tapped step before the drag threshold.
    if (menu->open && (menu->hit == HIT_BRIGHTNESS || menu->hit == HIT_CCT)) {
        const uint8_t value = slider_value(menu->hit, x);
        // 脏范围只增不减：记录本次定稿以来达到的最大档位，松手据此洗白。
        // The dirty extent only grows: track the highest level since the last settle for the whitening stage.
        if (menu->hit == HIT_BRIGHTNESS) {
            menu->fl_bri = value;
            if (value > menu->fl_bri_dirty) menu->fl_bri_dirty = value;
        } else {
            menu->fl_cct = value;
            if (value > menu->fl_cct_dirty) menu->fl_cct_dirty = value;
        }
        // 滑杆只记录数值，不替开关开灯；灯灭时拖动只更新下次点亮要用的档位。
        // A slider only records the value and never acts as the switch; with the light off it queues the level for the next on.
        menu->drag = menu->hit == HIT_BRIGHTNESS
            ? UI_QUICK_CTL_BRIGHTNESS : UI_QUICK_CTL_CCT;
        event->control = menu->drag;
        event->value = value;
    }
    if (!menu->open && dy >= UI_QUICK_SWIPE_PX && dy >= abs(dx) * 2 &&
        ctx->now_ms - menu->started_ms <= 1500) {
        menu->open = true;
        menu->tracking = false;
        menu->swallow = true;
        // 灯灭时记下「这次下拉刚开层」：手指停顿后再继续下拉还能临时展开胶囊；
        // 停顿判定的锚点从开层瞬间的手指位置起算。
        // With the light off, remember this pull just opened the layer: a pause
        // and further pull can still expand the capsules; the stillness anchor
        // starts at the finger position when the layer opened.
        menu->fl_pull = menu->frontlight && !menu->fl_on;
        menu->fl_pull_settled = false;
        menu->fl_pull_y = y;
        menu->fl_pull_ms = ctx->now_ms;
        event->action = UI_QUICK_OPEN;
    } else if (menu->open && menu->frontlight && !menu->fl_on && !menu->fl_expanded &&
               menu->drag == UI_QUICK_CTL_NONE && (int)menu->y0 < layer_height(menu) &&
               dy >= UI_QUICK_SWIPE_PX && dy >= abs(dx) * 2 &&
               ctx->now_ms - menu->started_ms <= 1500) {
        // 灯灭时在已开的层上再次下拉：同样临时展开胶囊，只属本次开层。
        // Pulling down again on the open layer with the light off: expand the
        // capsules for this opening only.
        menu->fl_expanded = true;
        menu->tracking = false;
        menu->swallow = true;
        event->action = UI_QUICK_EXPAND;
    } else if (menu->open && menu->drag == UI_QUICK_CTL_NONE &&
               dy <= -UI_QUICK_SWIPE_PX && -dy >= abs(dx) * 2) {
        dismiss(menu, event);
    } else if (ctx->released) {
        menu->tracking = false;
        if (menu->open && !menu->moved) {
            if (menu->hit == 3 && menu->frontlight) {
                // 第四枚圆钮就是前光开关：点按翻转，并作为一次立即提交；翻转也
                // 清掉下拉临时展开，熄灯即收起滑杆。
                // The fourth circle is the frontlight switch: a tap flips it and
                // commits immediately; the flip also clears the pull-down
                // expansion, so switching off collapses the sliders.
                menu->fl_on = !menu->fl_on;
                menu->fl_expanded = false;
                event->control = UI_QUICK_CTL_ON;
                event->value = menu->fl_on ? 1 : 0;
                event->commit = true;
            } else if (menu->hit >= 0 && menu->hit < 4 && circle_hit(x, y) == menu->hit) {
                static const ui_quick_action_t actions[] = {
                    UI_QUICK_WIFI, UI_QUICK_BLUETOOTH, UI_QUICK_FULL, UI_QUICK_LOCK,
                };
                event->action = actions[menu->hit];
            } else if (menu->drag != UI_QUICK_CTL_NONE) {
                event->control = menu->drag;
                event->value = menu->drag == UI_QUICK_CTL_BRIGHTNESS
                    ? menu->fl_bri : menu->fl_cct;
                event->commit = true;
            } else {
                // 层高随开灯伸缩：熄灯时点在圆钮区以下同样收起整层。
                // The layer height follows the light: with it off, taps below the circle band still dismiss.
                const int height = layer_height(menu);
                if (menu->y0 >= height && y >= height) {
                    dismiss(menu, event);
                    menu->swallow = false;
                }
            }
        } else if (menu->open && menu->drag != UI_QUICK_CTL_NONE) {
            // 拖动后松手：提交最后一档，避免只改硬件不落盘。
            // Release after a drag: commit the last step so hardware and NVS never diverge.
            event->control = menu->drag;
            event->value = menu->drag == UI_QUICK_CTL_BRIGHTNESS
                ? menu->fl_bri : menu->fl_cct;
            event->commit = true;
        }
        menu->drag = UI_QUICK_CTL_NONE;
    }
    return true;
}
