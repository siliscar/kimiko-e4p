// 中文：验证同步输入边界、真实手势事件与取消时的导航保护。
// English: Check synchronous input boundaries, real gesture events and navigation protection during cancellation.
#include "app_input.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static ui_gesture_t gesture;
static app_touch_t latest;
static app_ctx_t ctx;
static bool was_touched;
static int64_t last_input;
static unsigned callbacks, presents, public_presents, beeps;
static ui_gesture_event_t event;
static app_redraw_t answer;
static bool redirect_cancel, reactivate_present;
static const app_desc_t destination = {0}, unwanted = {0};
static uint64_t trace = UINT64_C(1469598103934665603);

static void record(uint64_t value) {
    trace = (trace ^ value) * UINT64_C(1099511628211);
}

void touch_diag_beep(void) { ++beeps; }

static app_redraw_t on_gesture(app_ctx_t *c, const ui_gesture_event_t *e) {
    assert(c == &ctx);
    ++callbacks;
    event = *e;
    record(100 + e->type); record(e->x0); record(e->y0);
    record(e->x); record(e->y); record(e->hold_ms); record(c->consumed);
    if (e->type == UI_GESTURE_CANCEL && redirect_cancel) {
        assert(gesture.active);
        c->request_app = &unwanted;
        c->request_menu = !c->request_menu;
        c->request_return = !c->request_return;
    }
    return answer;
}

static const app_desc_t page = { .on_gesture = on_gesture };
static const app_desc_t no_gesture_page = {0};

void app_present(app_ctx_t *c, const app_desc_t *app, app_redraw_t redraw) {
    assert(c == &ctx && app == &page && !gesture.active);
    ++public_presents;
    record(200 + redraw); record(c->consumed);
    if (redirect_cancel) {
        assert(c->request_app == &destination);
        assert(c->request_menu && !c->request_return);
    }
    if (reactivate_present) gesture.active = true;
}

static void present(app_ctx_t *c, const app_desc_t *app,
                    ui_gesture_t *g, app_redraw_t redraw) {
    assert(c == &ctx && app == &page && g == &gesture);
    ++presents;
    record(300 + redraw); record(c->consumed); record(g->active);
}

static void reset(void) {
    memset(&ctx, 0, sizeof(ctx));
    memset(&latest, 0, sizeof(latest));
    ui_gesture_reset(&gesture);
    ctx.touch = &latest;
    was_touched = false; last_input = 17;
    answer = APP_REDRAW_NONE;
    redirect_cancel = reactivate_present = false;
}

static void ingest(app_touch_t touch, int64_t time, esp_err_t err) {
    bool pressed, released;
    bool previous = was_touched;
    ctx.consumed = true;
    app_input_ingest_touch_frame(&ctx, &latest, &touch, err, &was_touched,
        time, &last_input, &pressed, &released);
    assert(was_touched == previous);
    assert(pressed == ctx.pressed && released == ctx.released && !ctx.consumed);
    record(ctx.now_ms); record(last_input); record(ctx.pressed); record(ctx.released);
    record(latest.touched); record(latest.count); record(latest.x); record(latest.y);
    for (unsigned i = 0; i < APP_TOUCH_MAX_POINTS; ++i) {
        record(latest.points[i].active); record(latest.points[i].x); record(latest.points[i].y);
    }
    for (unsigned i = 0; i < sizeof(latest.raw); ++i) record(latest.raw[i]);
}

static void feed(bool down, unsigned x, unsigned y, unsigned count, int64_t time) {
    ingest((app_touch_t){.touched=down, .x=x, .y=y, .count=count}, time, ESP_OK);
    app_input_dispatch_gesture_frame(&ctx, &page, &gesture, ESP_OK, false, present);
    was_touched = latest.touched;
}

static void snapshots(void) {
    reset();
    touch_frame_t frame = {.t_ms=INT64_C(4294967310), .touched=1, .count=2, .x=37, .y=91};
    app_touch_t raw;
    memset(&raw, 0xff, sizeof(raw));
    app_input_frame_to_touch(&frame, &raw);
    assert(raw.touched && raw.count == 2 && raw.x == 37 && raw.y == 91);
    app_touch_t expected = {.touched=true, .count=2, .x=37, .y=91};
    assert(!memcmp(&raw, &expected, sizeof(raw)));
    raw.points[0].active = true; raw.points[0].x = 5; raw.points[0].y = 9;
    raw.points[1].x = 999; raw.points[1].y = 888;
    memset(raw.raw, 0x71, sizeof(raw.raw));
    ingest(raw, frame.t_ms, ESP_OK);
    assert(ctx.pressed && !ctx.released && last_input == frame.t_ms);
    assert(latest.points[0].x == 5 && latest.points[0].active);
    assert(!latest.points[1].active && !latest.points[1].x && !latest.points[1].y);
    was_touched = latest.touched;
    raw = (app_touch_t){.x=800, .y=900};
    memset(raw.raw, 0x42, sizeof(raw.raw));
    ingest(raw, frame.t_ms + 30, ESP_OK);
    assert(ctx.released && !latest.touched && !latest.count);
    assert(latest.x == 37 && latest.y == 91 && latest.points[0].x == 5);
    assert(!latest.points[0].active && latest.raw[14] == 0x42);
    was_touched = latest.touched;
    ingest((app_touch_t){.x=1, .y=2}, frame.t_ms + 40, ESP_OK);
    assert(!ctx.pressed && !ctx.released && latest.x == 37 && last_input == frame.t_ms + 30);
    ingest((app_touch_t){.touched=true, .count=1, .x=3}, 200, ESP_ERR_INVALID_STATE);
    assert(ctx.pressed && latest.x == 3 && ctx.now_ms == 200);
    assert(last_input == frame.t_ms + 30);
}

static void gestures(void) {
    reset();
    unsigned start = callbacks;
    feed(true, 50, 70, 1, 1000);
    assert(event.type == UI_GESTURE_PRESS && gesture.active);
    feed(false, 0, 0, 0, 1030);
    assert(callbacks == start + 2 && event.type == UI_GESTURE_TAP);
    assert(event.x == 50 && event.y == 70 && event.hold_ms == 30 && !gesture.active);
    assert(!ctx.consumed);
    answer = APP_REDRAW_AREA;
    feed(true, 200, 200, 1, 2000);
    assert(ctx.consumed);
    answer = APP_REDRAW_NONE;
    feed(true, 80, 200, 1, 2010);
    assert(event.type == UI_GESTURE_MOVE);
    feed(false, 0, 0, 0, 2020);
    assert(event.type == UI_GESTURE_SWIPE_L && !gesture.active);
    feed(true, 20, 30, 1, 3000);
    feed(true, 20, 30, 1, 4000);
    assert(event.type == UI_GESTURE_LONG_PRESS && gesture.long_fired);
    start = callbacks;
    feed(true, 20, 30, 1, 4100);
    assert(callbacks == start);
    feed(false, 0, 0, 0, 4200);
    assert(event.type == UI_GESTURE_CANCEL);
    feed(true, 50, 70, 1, 5000);
    feed(true, 51, 71, 2, 5010);
    assert(event.type == UI_GESTURE_CANCEL && !gesture.active);
    feed(false, 0, 0, 0, 5020);
    feed(true, 50, 70, 1, 6000);
    feed(true, 50, 70, 1, 5999);
    assert(event.type == UI_GESTURE_CANCEL && !gesture.active);
}

static void ownership_and_cancel(void) {
    reset();
    feed(true, 50, 70, 1, 1000);
    unsigned start = callbacks, before = presents;
    ui_gesture_t saved = gesture;
    app_input_dispatch_gesture_frame(&ctx, &page, &gesture, ESP_OK, true, present);
    app_input_dispatch_gesture_frame(&ctx, &page, &gesture, ESP_ERR_INVALID_STATE, false, present);
    app_input_dispatch_gesture_frame(&ctx, &no_gesture_page, &gesture, ESP_OK, false, present);
    assert(callbacks == start && presents == before && !memcmp(&saved, &gesture, sizeof(saved)));
    ctx.request_app = &destination; ctx.request_menu = true;
    redirect_cancel = reactivate_present = true;
    answer = APP_REDRAW_PAGE;
    before = public_presents;
    app_input_cancel_gesture(&ctx, &page, &gesture);
    assert(callbacks == start + 1 && event.type == UI_GESTURE_CANCEL);
    assert(public_presents == before + 1 && !gesture.active);
    assert(ctx.request_app == &destination && ctx.request_menu && !ctx.request_return);
    app_input_cancel_gesture(&ctx, &page, &gesture);
    assert(callbacks == start + 1 && public_presents == before + 1);
    gesture.active = true;
    app_input_cancel_gesture(&ctx, &no_gesture_page, &gesture);
    assert(!gesture.active && callbacks == start + 1);
}

static uint32_t rng = UINT32_C(0xa315ce71);
static uint32_t random_value(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void event_trace(void) {
    reset();
    int64_t time = INT64_C(4294967310);
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned choice = random_value();
        bool down = choice % 5 != 0;
        app_touch_t touch = {.touched=down, .count=down ? 1 + (choice % 23 == 0) : 0,
            .x=random_value()%540, .y=random_value()%960};
        time += choice % 1000;
        esp_err_t err = choice % 31 == 0 ? ESP_ERR_INVALID_STATE : ESP_OK;
        ingest(touch, time - (choice % 97 == 0 ? 2000 : 0), err);
        answer = (app_redraw_t)(choice % 5);
        app_input_dispatch_gesture_frame(&ctx, &page, &gesture, err, choice % 29 == 0, present);
        if (choice % 17 == 0) app_input_cancel_gesture(&ctx, &page, &gesture);
        was_touched = latest.touched;
        record(ctx.consumed); record(gesture.active); record(gesture.x0); record(gesture.y0);
        record(gesture.x); record(gesture.y); record(gesture.t0_ms); record(gesture.last_ms);
        record(gesture.moved); record(gesture.long_fired);
    }
}

int main(void) {
    snapshots(); gestures(); ownership_and_cancel(); event_trace();
#if CONFIG_TOUCH_DIAG
    app_input_diag_counts_t counts = app_input_diag_counts();
    app_input_diag_counts_t again = app_input_diag_counts();
    assert(counts.press > 0 && counts.tap > 0 && counts.swipe > 0);
    assert(counts.longp > 0 && counts.cancel == beeps && counts.multi > 0);
    assert(!memcmp(&counts, &again, sizeof(counts)));
    record(counts.press); record(counts.tap); record(counts.swipe);
    record(counts.longp); record(counts.cancel); record(counts.multi);
#else
    assert(!beeps);
#endif
    printf("INPUT_TRACE diag=%d %016" PRIx64 " callbacks=%u presents=%u public=%u\n",
        CONFIG_TOUCH_DIAG, trace, callbacks, presents, public_presents);
    return 0;
}
