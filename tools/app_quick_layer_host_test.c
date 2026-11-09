/*
 * 中文：使用真实快捷输入与控制器，验证回放合并、服务动作及显示失败边界。
 * English: Exercise the real quick input and controller across replay coalescing, service actions and display failures.
 */
#include "app_quick_layer.h"
#include "ui_quick_menu.h"
#include "display_normalize.h"
#include "wifi_transfer.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

static uint64_t trace = UINT64_C(1469598103934665603);
static void record(unsigned tag, unsigned value) {
    trace = (trace ^ tag) * UINT64_C(1099511628211);
    trace = (trace ^ value) * UINT64_C(1099511628211);
}
static bool available = true, saved_on = true, hw_on, ble, online, usb, hold_gray;
static uint8_t saved_bri = 40, saved_cct = 50, hw_bri, hw_cct;
static unsigned settings_writes, applies, rows, pushes, renders, fallback, failures;
static unsigned wifi_starts, wifi_stops, captures, enters, exits, setup_wifi, setup_bt;
static bool configured = true, can_stop = true, expect_origin;
static int error_once;
static wifi_transfer_status_t network;
static app_ctx_t ctx;
static app_touch_t touch;
static uint8_t fb[1];
static EpdiyHighlevelState hl;
int E0470_WAVEFORM, E0470_FOLLOW_WAVEFORM, E0470_CAPSULE_DRAG_WAVEFORM;
static int normalize_wave;
static unsigned last_kind;
static const void *last_wave;
static int last_mode;
static EpdRect last_area;

static void render(app_ctx_t *c, uint8_t *buffer) {
    assert(c == &ctx && buffer == fb); ++renders; record(1, renders);
}
static void enter(app_ctx_t *c) {assert(c == &ctx); ++enters; record(2, enters);}
static void leave(app_ctx_t *c) {assert(c == &ctx); ++exits; record(3, exits);}
static bool normalize(app_ctx_t *c) {assert(c == &ctx); return hold_gray;}
static const app_desc_t page = {.render = render, .normalize_hold_gray = normalize};
const app_desc_t app_transfer = {.render = render, .on_enter = enter, .on_exit = leave};
const app_desc_t app_device_settings = {.render = render, .on_enter = enter, .on_exit = leave};
static const app_desc_t *current = &page;

bool app_settings_frontlight_on(void) {return saved_on;}
uint8_t app_settings_frontlight_brightness(void) {return saved_bri;}
uint8_t app_settings_frontlight_cct(void) {return saved_cct;}
void app_settings_set_frontlight_on(bool on) {saved_on = on; ++settings_writes; record(4, on);}
void app_settings_set_frontlight_brightness(uint8_t bri) {saved_bri = bri; record(5, bri);}
void app_settings_set_frontlight_cct(uint8_t cct) {saved_cct = cct; record(6, cct);}
bool frontlight_ready(void) {return available;}
esp_err_t frontlight_apply(bool on, uint8_t bri, uint8_t cct) {
    hw_on = on; hw_bri = bri; hw_cct = cct; ++applies;
    record(7, on); record(8, bri); record(9, cct); return ESP_OK;
}
bool app_settings_ble_turner(void) {return ble;}
void app_settings_set_ble_turner(bool on) {ble = on; record(10, on);}
void ble_pt_sync(bool on) {record(11, on);}
void ble_pt_reconnect_recent(void) {assert(ble); record(12, 1);}
void ble_pt_cancel_recent(void) {assert(!ble); record(13, 0);}
bool pico_online_busy(void) {return online;}
bool usb_storage_active(void) {return usb;}
void wifi_transfer_get_status(wifi_transfer_status_t *s) {*s = network;}
int wifi_transfer_get_saved_wifi(char *ssid, bool *ok) {ssid[0] = 0; *ok = configured; return ESP_OK;}
int wifi_transfer_start(const wifi_transfer_cfg_t *cfg) {
    assert(cfg->mode == WIFI_TRANSFER_MODE_STA && cfg->network_only);
    ++wifi_starts; network.mode = cfg->mode; network.state = 1; record(14, wifi_starts); return ESP_OK;
}
bool wifi_transfer_try_stop_if_idle(void) {
    ++wifi_stops; if (can_stop) network.state = WIFI_TRANSFER_STOPPED;
    record(15, can_stop); return can_stop;
}
void app_navigation_capture_reader_return(const app_desc_t *p) {assert(p == current); ++captures; record(16, captures);}
void app_transfer_request_wifi_setup(void) {++setup_wifi; record(17, setup_wifi);}
void app_device_settings_request_bluetooth(bool origin) {expect_origin = origin; ++setup_bt; record(18, origin);}
void app_present(app_ctx_t *c, const app_desc_t *p, app_redraw_t redraw) {
    assert(c == &ctx && p == current);
    if (redraw == APP_REDRAW_FULL) ++fallback;
    else assert(redraw == APP_REDRAW_PAGE);
    record(19, redraw);
}
void ui_click_feedback_begin(uint8_t *buffer) {assert(buffer == fb); record(20, 0);}
void display_main_transition_cancel(void) {record(21, 0);}
const EpdWaveform *display_normalize_waveform(bool hold) {
    assert(hold == hold_gray); record(22, hold); return (const EpdWaveform *)&normalize_wave;
}
void ui_quick_menu_draw(uint8_t *buffer, const ui_quick_menu_t *m, bool wifi, bool bluetooth) {
    assert(buffer == fb); record(23, m->fl_on); record(24, wifi); record(25, bluetooth);
}
void ui_quick_menu_end(void) {record(26, 0);}
void ui_quick_menu_draw_status(uint8_t *buffer) {assert(buffer == fb); record(27, 0);}
EpdRect ui_quick_menu_rect(const ui_quick_menu_t *m) {
    return (EpdRect){0, 0, 684, m->frontlight && (m->fl_on || m->fl_expanded)
        ? UI_QUICK_FRONTLIGHT_HEIGHT : UI_QUICK_HEIGHT};
}
bool ui_quick_menu_draw_button(uint8_t *buffer, const ui_quick_menu_t *m,
                               int button, bool active, bool pressed, EpdRect *dirty) {
    assert(buffer == fb); (void)m;
    record(28, button); record(29, active); record(30, pressed);
    *dirty = (EpdRect){(button * 2 + 1) * 684 / 8 - 54, 102, 108, 108}; return true;
}
bool ui_quick_menu_draw_control(uint8_t *buffer, const ui_quick_menu_t *m,
                                ui_quick_control_t control, EpdRect *dirty) {
    assert(buffer == fb);
    if (!m->fl_on || (control != UI_QUICK_CTL_BRIGHTNESS && control != UI_QUICK_CTL_CCT)) return false;
    ++rows; record(31, control); record(32, m->fl_bri); record(33, m->fl_cct);
    record(34, m->fl_bri_dirty); record(35, m->fl_cct_dirty);
    *dirty = (EpdRect){40, control == UI_QUICK_CTL_BRIGHTNESS ? 244 : 344, 604, 80}; return true;
}
static enum EpdDrawError push(unsigned kind, const void *wave, int mode, EpdRect area) {
    last_kind = kind; last_wave = wave; last_mode = mode; last_area = area;
    record(kind, mode); record(40, area.x); record(41, area.y); record(42, area.width); record(43, area.height);
    record(44, wave == &E0470_WAVEFORM ? 1 : wave == &E0470_FOLLOW_WAVEFORM ? 2 :
        wave == &E0470_CAPSULE_DRAG_WAVEFORM ? 3 : wave == &normalize_wave ? 4 : 5);
    int result = error_once; error_once = 0; ++pushes; return (enum EpdDrawError)result;
}
enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState *h, const void *wave, int mode, EpdRect area) {
    assert(h == &hl); return push(36, wave, mode, area);
}
enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState *h, const void *wave, int mode, EpdRect area) {
    assert(h == &hl); return push(37, wave, mode, area);
}
enum EpdDrawError update_display_area_diff_padded(EpdiyHighlevelState *h, const void *wave, int mode, EpdRect area, int x, int y) {
    assert(h == &hl && x == 1 && y == 1); return push(38, wave, mode, area);
}
enum EpdDrawError update_display_full(EpdiyHighlevelState *h) {
    assert(h == &hl); return push(39, NULL, MODE_GC16, (EpdRect){0, 0, 684, 1216});
}
enum EpdDrawError update_display_white_restore(EpdiyHighlevelState *h, EpdRect area) {
    assert(h == &hl); return push(50, NULL, MODE_GL16, area);
}
void guard_draw_result(EpdiyHighlevelState *h, enum EpdDrawError error) {
    assert(h == &hl); if (error != EPD_DRAW_SUCCESS) ++failures; record(45, error);
}
int ui_key_hit_test(uint16_t x, uint16_t y) {(void)x; return y >= 1152 ? 0 : -1;}

static bool frame(bool down, bool press, bool release, int x, int y, int ms, bool coalesce) {
    ctx.now_ms += ms; touch.touched = down; touch.count = down ? 1 : 0;
    touch.x = x; touch.y = y; ctx.pressed = press; ctx.released = release; ctx.consumed = false;
    bool lock = false;
    bool owned = app_quick_layer_feed_frame(&ctx, current, &lock, coalesce);
    record(46, owned); record(47, lock); record(48, ctx.consumed); record(49, app_quick_layer_is_open());
    return lock;
}
static void release(void) {(void)frame(false, false, true, 0, 0, 10, false);}
static void open_layer(void) {
    app_quick_layer_reset(); online = usb = false; current = &page;
    saved_on = true; saved_bri = 40; saved_cct = 50; ctx.request_app = NULL;
    (void)frame(true, true, false, 200, 10, 10, false);
    (void)frame(true, false, false, 200, 100, 10, false);
    assert(app_quick_layer_is_open()); release();
}
static void tap(int button) {
    int x = (button * 2 + 1) * 684 / 8;
    (void)frame(true, true, false, x, 156, 10, false);
    app_quick_layer_present_buttons(&ctx, current);
    (void)frame(false, false, true, x, 156, 10, false);
}
static void hold(int button, const app_desc_t *p) {
    open_layer(); current = p; int x = (button * 2 + 1) * 684 / 8;
    (void)frame(true, true, false, x, 156, 10, false);
    (void)frame(true, false, false, x, 156, 500, false);
    assert(!app_quick_layer_is_open()); release();
}
int main(void) {
    ctx = (app_ctx_t){.hl = &hl, .fb = fb, .touch = &touch};
    app_quick_layer_init(); open_layer();
    unsigned p = pushes, a = applies, w = settings_writes;
    (void)frame(true, true, false, 100, 284, 10, true);
    (void)frame(true, false, false, 600, 284, 10, true);
    (void)frame(true, false, false, 350, 284, 10, true);
    (void)frame(false, false, true, 350, 284, 10, true);
    assert(applies == a + 4 && settings_writes == w + 1 && pushes == p);
    assert(saved_on == hw_on && saved_bri == hw_bri && saved_cct == hw_cct);
    app_quick_layer_flush_frontlight(&ctx, current); assert(pushes == p + 1);
    assert(last_kind == 38 && last_wave == &E0470_WAVEFORM && last_mode == MODE_DU);
    assert(last_area.y == 244 && last_area.height == 80);
    app_quick_layer_flush_frontlight(&ctx, current); assert(pushes == p + 1);
    (void)frame(true, true, false, 600, 384, 10, true);
    app_quick_layer_discard_frontlight(); p = pushes;
    app_quick_layer_flush_frontlight(&ctx, current); assert(pushes == p);
    w = settings_writes; assert(app_quick_layer_cancel_input()); assert(settings_writes == w + 1); release();
    assert(saved_cct == hw_cct);

    open_layer(); p = renders; tap(2); assert(renders == p);
    hold_gray = true; tap(3); p = renders; usb = true;
    release(); assert(!app_quick_layer_is_open() && renders == p + 1);
    assert(last_kind == 36 && last_wave == &normalize_wave && last_mode == MODE_GL16 && last_area.height == 1216);
    hold_gray = false;
    open_layer(); online = true; release(); assert(!app_quick_layer_is_open()); online = false;
    assert(last_kind == 36 && last_wave == &normalize_wave && last_mode == MODE_GL16 && last_area.height == 1216);
    app_quick_layer_present(&ctx, &app_transfer, false);
    assert(last_kind == 36 && last_wave == &normalize_wave && last_area.height == 1216);

    for (int path = 0; path < 5; ++path) {
        open_layer(); unsigned f = fallback, n = failures; error_once = 1;
        if (path == 0) app_quick_layer_present(&ctx, current, false);
        if (path == 1) app_quick_layer_present(&ctx, current, true);
        if (path == 2) {
            (void)frame(true, true, false, 85, 156, 10, false);
            app_quick_layer_present_buttons(&ctx, current);
        }
        if (path == 3) (void)frame(true, true, false, 250, 284, 10, false);
        if (path == 4) {usb = true; release(); usb = false;}
        assert(!app_quick_layer_is_open() && fallback == f + 1 && failures == n + 1);
        p = pushes; app_quick_layer_flush_frontlight(&ctx, current); assert(pushes == p);
    }

    open_layer(); configured = false; p = wifi_starts; tap(0); assert(wifi_starts == p);
    configured = true; tap(0); assert(wifi_starts == p + 1);
    can_stop = false; p = wifi_stops; tap(0); assert(wifi_stops == p + 1 && network.state != 0);
    can_stop = true; tap(0); assert(network.state == 0);
    tap(1); assert(ble); tap(1); assert(!ble);
    p = captures; hold(0, &page); assert(ctx.request_app == &app_transfer && captures == p + 1);
    p = enters; hold(0, &app_transfer); assert(enters == p + 1 && exits == enters);
    p = captures; hold(1, &page); assert(ctx.request_app == &app_device_settings && captures == p + 1 && expect_origin);
    p = enters; hold(1, &app_device_settings); assert(enters == p + 1 && exits == enters && !expect_origin);

    // 固定种子混合真实接触、回放、权限变化与失败，便于与提取前控制器逐事件对照。
    // Mix real contact, replay, permission changes and failures with a fixed seed for event-by-event comparison with the original controller.
    uint32_t seed = 0x31a02;
    current = &page;
    for (unsigned i = 0; i < 100000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        unsigned action = seed >> 27;
        if (action == 0) {app_quick_layer_reset(); release();}
        else if (action == 1) app_quick_layer_flush_frontlight(&ctx, current);
        else if (action == 2) app_quick_layer_discard_frontlight();
        else if (action == 3) (void)app_quick_layer_cancel_input();
        else {
            usb = (seed & 4095) == 4; online = (seed & 8191) == 5;
            if ((seed & 1023) == 6) error_once = 1;
            static const int xs[] = {85, 256, 427, 598, 100, 350, 620, 200};
            static const int ys[] = {10, 100, 156, 156, 284, 384, 700, 10};
            unsigned index = (seed >> 12) & 7;
            bool down = action < 24;
            bool was_down = touch.touched;
            (void)frame(down, down && !was_down, !down && was_down,
                xs[index], ys[index], (seed & 1) ? 10 : 510, (seed & 2) != 0);
            app_quick_layer_present_buttons(&ctx, current);
            network.network_ready = (seed & 16) != 0;
            app_quick_layer_sync_status(&ctx, current);
        }
        ctx.request_app = NULL;
    }
    app_quick_layer_reset(); p = pushes;
    app_quick_layer_flush_frontlight(&ctx, current); assert(pushes == p);
    printf("QUICK_TRACE %016" PRIx64 " applies=%u rows=%u pushes=%u failures=%u\n", trace, applies, rows, pushes, failures);
    return 0;
}
