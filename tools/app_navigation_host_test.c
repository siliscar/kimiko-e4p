// 中文：检查真实导航模块的回调顺序、返回来源、正文快照与接触锁存。
// English: Check the real navigation module's callback ordering, return origins, reader snapshots and contact latches.
#include "app_navigation.h"
#include "ui_kit.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const app_desc_t *current;
static app_ctx_t ctx;
static app_navigation_history_t history;
static bool menu_open, body_visible, snapshot_ok = true, snapshot_fullscreen = true;
static bool captured_fullscreen;
static int menu_leaf, enters, reads, restores;
static app_touch_t raw_touch;
static esp_err_t read_error;
static char sequence[128];
static unsigned sequence_size;
static app_redraw_t last_redraw;
static uint64_t trace = UINT64_C(1469598103934665603);

static void record(uint64_t value) { trace = (trace ^ value) * UINT64_C(1099511628211); }
int app_index_of(const app_desc_t *app);

static void event(char code) {
    if (sequence_size < sizeof(sequence) - 1) sequence[sequence_size++] = code;
    sequence[sequence_size] = 0;
    record(code); record(app_index_of(current)); record(ctx.leaf);
    record(app_index_of(history.return_app)); record(history.return_leaf);
    record(history.return_to_menu); record(history.return_menu_leaf);
    record(menu_open); record(menu_leaf);
}

static void enter(app_ctx_t *context) {
    assert(context == &ctx);
    event('E');
    ++enters;
    context->leaf = 0;
    context->request_menu = true;
}
static void exit_page(app_ctx_t *context) { assert(context == &ctx); event('X'); }
static bool root_visible(app_ctx_t *context) { assert(context == &ctx); return true; }
static bool book_visible(app_ctx_t *context) { assert(context == &ctx); return !body_visible; }
static const app_desc_t home = {.title="home", .on_enter=enter, .on_exit=exit_page, .main_page_visible=root_visible};
const app_desc_t app_book = {.title="book", .on_enter=enter, .on_exit=exit_page, .main_page_visible=book_visible};
static const app_desc_t settings = {.title="settings", .on_enter=enter, .on_exit=exit_page, .main_page_visible=root_visible};
static const app_desc_t image = {.title="image", .on_enter=enter, .on_exit=exit_page, .main_page_visible=root_visible, .enter_full=true};
const app_desc_t app_transfer = {.title="transfer", .on_enter=enter, .on_exit=exit_page};
static const app_desc_t empty_page = {.title="empty"};
static const app_desc_t *const pages[] = {&home, &app_book, &settings, &image, &app_transfer, &empty_page};

int app_index_of(const app_desc_t *app) {
    for (unsigned i = 0; i < sizeof(pages)/sizeof(*pages); ++i) if (app == pages[i]) return i;
    return -1;
}
void display_main_transition_cancel(void) { event('C'); }
void display_main_transition_arm(const void *app, bool shelf, bool changed) {
    assert(app == current && changed); event(shelf ? 'S' : 'A');
}
void display_main_transition_disarm(void) { event('D'); }
bool app_font_activate_system(void) { event('F'); return true; }
void app_time_service_cancel(void) { event('T'); }
void pico_ota_auto_cancel(void) {}
void app_present(app_ctx_t *context, const app_desc_t *app, app_redraw_t redraw) {
    assert(context == &ctx && app == current); event('P'); record(redraw);
    last_redraw = redraw;
}
static void present_menu(app_ctx_t *context, const app_desc_t *app, int leaf, void *opaque) {
    assert(context == &ctx && app == current && leaf == menu_leaf && opaque == &history);
    assert(menu_open); event('M');
}
int ui_menu_leaf_for_app(const app_desc_t *app) { return 10 + app_index_of(app); }
static esp_err_t read_touch(void *handle, app_touch_t *touch) {
    assert(handle == &ctx); ++reads; *touch = raw_touch; return read_error;
}
int ui_nav_hit(uint16_t x, uint16_t y) { return y == 1100 && x < 4 ? x : -1; }
void ui_nav_request(app_ctx_t *context, int tab) {
    assert(tab >= 0 && tab < 4); context->request_app = pages[tab];
}
bool app_book_reader_body_visible(void) { return body_visible; }
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen) {
    assert(capacity == 256); event('B');
    snprintf(path, capacity, "/sdcard/books/test.epub");
    *fullscreen = snapshot_fullscreen;
    captured_fullscreen = *fullscreen;
    return snapshot_ok;
}
bool app_book_request_return_to_body(const char *path, bool fullscreen) {
    assert(!strcmp(path, "/sdcard/books/test.epub") && fullscreen == captured_fullscreen);
    ++restores; event('R'); return true;
}

static void apply(const app_desc_t *next, bool go_back, bool from_menu) {
    sequence_size = 0; sequence[0] = 0;
    app_navigation_apply_request(&ctx, &current, &menu_open, &menu_leaf, &history,
        (app_navigation_request_t){next, go_back, from_menu}, present_menu, &history);
    record(app_index_of(current)); record(ctx.leaf); record(menu_open); record(menu_leaf);
    record(app_index_of(history.return_app)); record(ctx.request_menu);
}

static void transitions(void) {
    current = &home; ctx.leaf = 7;
    apply(&settings, false, false);
    assert(!strcmp(sequence, "CXFEAPD") && current == &settings && history.return_app == &home);
    assert(history.return_leaf == 7 && !menu_open && ctx.request_menu);
    apply(&app_transfer, true, false);
    assert(!strcmp(sequence, "CXFEAPD") && current == &home && ctx.leaf == 7 && !history.return_app);
    apply(NULL, true, false);
    assert(!strcmp(sequence, "CM") && current == &home && menu_open && menu_leaf == 10);
    int before = enters;
    apply(&home, false, true);
    assert(!*sequence && enters == before && !menu_open && !history.return_app);
    menu_open = true; menu_leaf = 3; ctx.leaf = 9;
    apply(&app_transfer, false, true);
    assert(!strcmp(sequence, "CXTFEPD") && history.return_to_menu && history.return_menu_leaf == 3);
    apply(NULL, true, false);
    assert(!strcmp(sequence, "CXFEM") && menu_open && menu_leaf == 3 && ctx.leaf == 9);
    apply(&app_book, false, false);
    apply(&image, false, false);
    assert(!strcmp(sequence, "CXFESPD"));
    apply(NULL, false, false);
    assert(!strcmp(sequence, "M") && menu_leaf == 13 && menu_open);
}

static void reader_returns(void) {
    current = &app_book; body_visible = true; ctx.leaf = 41;
    app_navigation_capture_reader_return(current);
    apply(&settings, false, false);
    assert(!strcmp(sequence, "CXFEPD"));
    assert(last_redraw == APP_REDRAW_FULL);
    int before = restores;
    apply(NULL, true, false);
    assert(!strcmp(sequence, "CXFREPD") && restores == before + 1 && ctx.leaf == 41);
    apply(&settings, false, false); apply(NULL, true, false);
    assert(!strcmp(sequence, "CXFEPD") && restores == before + 1);
    app_navigation_capture_reader_return(current);
    apply(&settings, false, false); apply(&image, false, false); apply(NULL, true, false);
    assert(restores == before + 1 && current == &settings);
    current = &app_book; snapshot_ok = false;
    app_navigation_capture_reader_return(current);
    apply(&settings, false, false); apply(NULL, true, false);
    assert(restores == before + 1);
    snapshot_ok = true; body_visible = false;
    app_navigation_capture_reader_return(current);
    // 四个主页面都清掉正文历史；正常主页切页仍保持普通刷新。
    // Clear reader history on all four roots while ordinary root transitions stay ordinary.
    for (unsigned i = 0; i < 4; ++i) {
        if (pages[i] == &app_book) continue;
        current = &app_book; body_visible = true;
        apply(pages[i], false, false);
        assert(last_redraw == APP_REDRAW_FULL);
        current = &app_book;
        history = (app_navigation_history_t){.return_app=pages[i]};
        apply(NULL, true, false);
        assert(last_redraw == APP_REDRAW_FULL);
    }
    body_visible = false; current = &home;
    apply(&settings, false, false);
    assert(last_redraw == APP_REDRAW_PAGE);
    current = &app_book; body_visible = true;
    apply(&app_transfer, false, false);
    assert(last_redraw == APP_REDRAW_PAGE);
}

static void contact_latches(void) {
    ctx = (app_ctx_t){.read_touch=read_touch, .touch_context=&ctx}; raw_touch = (app_touch_t){0};
    assert(!app_poll_main_navigation(NULL));
    app_ctx_t unbound = {.request_app=&home};
    int initial_reads = reads;
    assert(!app_poll_main_navigation(&unbound) && reads == initial_reads);
    app_navigation_reset_contact();
    assert(!app_poll_main_navigation(&ctx) && !app_navigation_contact_polled());
    raw_touch = (app_touch_t){.touched=true, .count=1, .x=1, .y=1100};
    assert(!app_poll_main_navigation(&ctx) && ctx.user_activity && app_navigation_contact_polled());
    raw_touch.x = 0;
    assert(!app_poll_main_navigation(&ctx));
    app_navigation_observe_contact(ESP_OK, true);
    assert(app_navigation_contact_polled());
    app_navigation_observe_contact(ESP_OK, false);
    assert(!app_navigation_contact_polled());
    assert(app_poll_main_navigation(&ctx) && ctx.request_app == &home);
    int before = reads;
    assert(app_poll_main_navigation(&ctx) && reads == before);
    ctx.request_app = NULL;
    app_navigation_observe_contact(ESP_ERR_INVALID_STATE, true);
    assert(!app_navigation_contact_polled());
    app_navigation_claim_contact();
    assert(!app_poll_main_navigation(&ctx));
    app_navigation_reset_contact();
    raw_touch.count = 2;
    assert(!app_poll_main_navigation(&ctx) && !app_navigation_contact_polled());
    raw_touch.count = 1; read_error = ESP_ERR_INVALID_STATE;
    assert(!app_poll_main_navigation(&ctx));
    read_error = ESP_OK; raw_touch.x = 20;
    assert(!app_poll_main_navigation(&ctx));
}

static uint32_t rng = UINT32_C(0x31a715e9);
static uint32_t random_value(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }
static void request_trace(void) {
    current = &home; history = (app_navigation_history_t){0};
    ctx = (app_ctx_t){0}; menu_open = false; menu_leaf = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned value = random_value();
        ctx.leaf = value % 13; ctx.request_menu = false;
        body_visible = (value & 16) != 0;
        snapshot_ok = value % 11 != 0;
        snapshot_fullscreen = (value & 32) != 0;
        if (value % 7 == 0) app_navigation_capture_reader_return(current);
        const app_desc_t *next = value % 9 < 6 ? pages[value % 9] : NULL;
        apply(next, value % 5 == 0, (value & 64) != 0);
    }
}

int main(void) {
    transitions(); reader_returns(); contact_latches(); request_trace();
    printf("NAV_TRACE %016" PRIx64 " enters=%d restores=%d reads=%d\n", trace, enters, restores, reads);
    return 0;
}
