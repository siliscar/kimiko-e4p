/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从主循环提取同步输入管线，保持去抖、诊断、回调及取消边界。
 * English: Synchronous input pipeline extracted from the loop, retaining debounce, diagnostics, callbacks and cancellation boundaries.
 */
#include "app_input.h"
#include "touch_diag.h"
#include <string.h>

#if CONFIG_TOUCH_DIAG
static unsigned td_press, td_tap, td_swipe, td_longp, td_cancel, td_multi;
static bool td_multi_active;
static const char* td_gest_name(ui_gesture_type_t t) {
    switch (t) {
        case UI_GESTURE_PRESS: return "PRESS";
        case UI_GESTURE_MOVE: return "MOVE";
        case UI_GESTURE_LONG_PRESS: return "LONG";
        case UI_GESTURE_TAP: return "TAP";
        case UI_GESTURE_SWIPE_L: return "SWIPE_L";
        case UI_GESTURE_SWIPE_R: return "SWIPE_R";
        case UI_GESTURE_SWIPE_U: return "SWIPE_U";
        case UI_GESTURE_SWIPE_D: return "SWIPE_D";
        case UI_GESTURE_CANCEL: return "CANCEL";
        default: return "?";
    }
}
app_input_diag_counts_t app_input_diag_counts(void) {
    return (app_input_diag_counts_t){ .press = td_press, .tap = td_tap,
        .swipe = td_swipe, .longp = td_longp, .cancel = td_cancel, .multi = td_multi };
}
#endif

// 空槽常带着抬起事件或残留坐标，不能进页面看到的快照；抬手后保留最后一次位置。
// Empty slots often carry lift events or stale coords; keep last position on lift.
static void debounce_touch(
    app_touch_t* latest, const app_touch_t* raw, bool released
) {
    if (raw->touched) {
        *latest = *raw;
        for (int i = 0; i < APP_TOUCH_MAX_POINTS; i++) {
            if (!latest->points[i].active) {
                memset(&latest->points[i], 0, sizeof(latest->points[i]));
            }
        }
    } else if (released) {
        latest->touched = false;
        latest->count = 0;
        for (int i = 0; i < APP_TOUCH_MAX_POINTS; i++) {
            latest->points[i].active = false;
        }
    }
    memcpy(latest->raw, raw->raw, sizeof(raw->raw));
}

void app_input_frame_to_touch(const touch_frame_t* f, app_touch_t* t) {
    memset(t, 0, sizeof(*t));
    t->touched = f->touched != 0;
    t->count = f->count;
    t->x = f->x;
    t->y = f->y;
}

void app_input_ingest_touch_frame(app_ctx_t* ctx, app_touch_t* latest,
                               const app_touch_t* touch, esp_err_t err,
                               bool* was_touched, int64_t frame_ms,
                               int64_t* last_input_ms,
                               bool* out_pressed, bool* out_released) {
    const bool pressed = touch->touched && !*was_touched;
    const bool released = !touch->touched && *was_touched;
#if CONFIG_TOUCH_DIAG
    if (pressed) {
        td_press++;
        TD_LOGI("PRESS x=%u y=%u n=%u", touch->x, touch->y, touch->count);
    }
    if (err == ESP_OK && touch->touched && touch->count > 1) {
        if (!td_multi_active) {
            td_multi_active = true;
            td_multi++;
            TD_LOGI("MULTI n=%u x=%u y=%u", touch->count, touch->x, touch->y);
        }
    } else if (td_multi_active) td_multi_active = false;
#endif
    debounce_touch(latest, touch, released);
    ctx->now_ms = frame_ms;
    if (err == ESP_OK && (touch->touched || released)) *last_input_ms = frame_ms;
    ctx->pressed = pressed;
    ctx->released = released;
    ctx->consumed = false;
    *out_pressed = pressed;
    *out_released = released;
}

void app_input_dispatch_gesture_frame(app_ctx_t* ctx, const app_desc_t* current,
                                   ui_gesture_t* gesture, esp_err_t err,
                                   bool owned_by_other, app_input_present_t present) {
    if (owned_by_other || !current->on_gesture || err != ESP_OK) return;
    ui_gesture_event_t event;
    if (ui_gesture_feed(gesture, ctx, &event)) {
#if CONFIG_TOUCH_DIAG
        // MOVE 逐帧太多不记；按下与终态全记。MOVE frames are too noisy; log press + terminal.
        if (event.type != UI_GESTURE_MOVE) {
            switch (event.type) {
                case UI_GESTURE_TAP: td_tap++; break;
                case UI_GESTURE_LONG_PRESS: td_longp++; break;
                case UI_GESTURE_CANCEL:
                    td_cancel++;
                    touch_diag_beep();
                    break;
                case UI_GESTURE_SWIPE_L: case UI_GESTURE_SWIPE_R:
                case UI_GESTURE_SWIPE_U: case UI_GESTURE_SWIPE_D:
                    td_swipe++;
                    break;
                default: break;
            }
            TD_LOGI("GEND t=%s x0=%u y0=%u x=%u y=%u ms=%d",
                    td_gest_name(event.type), event.x0, event.y0, event.x, event.y,
                    (int)event.hold_ms);
        }
#endif
        app_redraw_t redraw = current->on_gesture(ctx, &event);
        present(ctx, current, gesture, redraw);
        if (redraw != APP_REDRAW_NONE) ctx->consumed = true;
    }
}

void app_input_cancel_gesture(app_ctx_t* ctx, const app_desc_t* app, ui_gesture_t* gesture) {
    if (gesture->active && app->on_gesture) {
        ui_gesture_event_t event = { .type = UI_GESTURE_CANCEL,
            .x0 = gesture->x0, .y0 = gesture->y0, .x = gesture->x, .y = gesture->y };
        const app_desc_t* requested = ctx->request_app;
        bool menu_requested = ctx->request_menu;
        bool return_requested = ctx->request_return;
        app_redraw_t redraw = app->on_gesture(ctx, &event);
        ctx->request_app = requested;
        ctx->request_menu = menu_requested;
        ctx->request_return = return_requested;
        ui_gesture_reset(gesture);
        app_present(ctx, app, redraw);
    }
    ui_gesture_reset(gesture);
}
