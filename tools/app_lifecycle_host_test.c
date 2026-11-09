/*
 * 中文：验证实际锁屏检查点与服务交还顺序；硬件操作仍由各调用方的回归覆盖。
 * English: Verify actual lock checkpoints and service handoff order; caller regressions retain hardware-operation coverage.
 */
#include "app_lifecycle.h"
#include "boot_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ESP_FAIL (-1)

static char events[64];
static size_t used;
static int tab, save_error, ble_error;
static bool reader, fullscreen, ota, cover_ok, avatar_ok, transfer_ok;
static bool page_flushed;
static pico_resume_t saved;
static app_ctx_t *expected_ctx;

static void event(char value) {
    assert(used + 1 < sizeof(events));
    events[used++] = value; events[used] = 0;
}
static void expect(const char *value) { assert(!strcmp(events, value)); }
static void reset(void) {
    used = 0; events[0] = 0; save_error = ble_error = 0;
    ota = page_flushed = reader = fullscreen = false;
    cover_ok = avatar_ok = transfer_ok = true;
    memset(&saved, 0xa5, sizeof(saved));
}
static void page_before_lock(app_ctx_t *ctx) {
    assert(ctx == expected_ctx); event('P'); page_flushed = true;
}
int app_index_of(const app_desc_t *current) {
    assert(current); event('I');
    if (current->on_before_lock) assert(page_flushed);
    return tab;
}
bool app_book_resume_context(char *path, size_t capacity, bool *is_fullscreen) {
    event('R'); assert(tab == 1 && capacity == sizeof(saved.path));
    if (!reader) return false;
    strcpy(path, "/sdcard/books/阅读.epub"); *is_fullscreen = fullscreen;
    return true;
}
esp_err_t pico_boot_save_resume(const pico_resume_t *resume) {
    event('S'); saved = *resume; return save_error;
}
void app_time_service_cancel(void) { event('T'); }
void pico_ota_auto_cancel(void) { event('U'); }
bool pico_online_busy(void) { event('O'); return ota; }
bool book_cover_jobs_quiesce(unsigned timeout_ms) { assert(timeout_ms == 2000); event('C'); return cover_ok; }
bool ui_avatar_quiesce(unsigned timeout_ms) { assert(timeout_ms == 2000); event('A'); return avatar_ok; }
bool wifi_transfer_pause_for_sleep(void) { event('W'); return transfer_ok; }
esp_err_t ble_pt_stop(uint32_t timeout_ms) { assert(timeout_ms == 2000); event('B'); return ble_error; }
esp_err_t ble_pt_stop_for_sleep(uint32_t timeout_ms) { assert(timeout_ms == 2000); event('L'); return ble_error; }

int main(void) {
    app_ctx_t ctx = {0}; expected_ctx = &ctx;
    app_desc_t page = {0};
    // 所有页面索引、阅读/全屏、保存失败和可选回调保持原选择及先保存页面的顺序。
    // Preserve selection and page-before-checkpoint order across indices, reader/fullscreen, save failure and optional callbacks.
    for (int index = -2; index <= 5; ++index)
    for (unsigned bits = 0; bits < 16; ++bits) {
        reset(); tab = index; reader = bits & 1; fullscreen = bits & 2;
        page.on_before_lock = bits & 4 ? page_before_lock : NULL;
        save_error = bits & 8 ? ESP_FAIL : ESP_OK;
        app_lifecycle_checkpoint(&ctx, &page);
        char expected[8] = "";
        if (page.on_before_lock) strcat(expected, "P");
        strcat(expected, "I"); if (tab == 1) strcat(expected, "R"); strcat(expected, "S");
        expect(expected);
        assert(saved.tab == (index >= 0 && index < 4 ? index : 0));
        assert(saved.reader == (tab == 1 && reader));
        assert(saved.fullscreen == (tab == 1 && reader && fullscreen));
        if (saved.reader) assert(!strcmp(saved.path, "/sdcard/books/阅读.epub"));
        else assert(!saved.path[0]);
    }
    // 穷举每个失败点；后续服务不能抢先运行。关机蓝牙失败仍按原尽力停止语义通过。
    // Enumerate every failure point; later services cannot run early. Power-off retains best-effort Bluetooth-stop admission.
    for (unsigned bits = 0; bits < 32; ++bits) {
        reset(); ota = bits & 1; cover_ok = bits & 2; avatar_ok = bits & 4;
        transfer_ok = bits & 8; ble_error = bits & 16 ? ESP_FAIL : ESP_OK;
        bool admitted = app_lifecycle_prepare_poweroff();
        if (ota) { expect("UTO"); assert(!admitted); }
        else if (!cover_ok) { expect("UTOC"); assert(!admitted); }
        else if (!avatar_ok) { expect("UTOCA"); assert(!admitted); }
        else if (!transfer_ok) { expect("UTOCAW"); assert(!admitted); }
        else { expect("UTOCAWB"); assert(admitted); }
    }
    for (unsigned bits = 0; bits < 8; ++bits) {
        reset(); cover_ok = bits & 1; avatar_ok = bits & 2; ble_error = bits & 4 ? ESP_FAIL : ESP_OK;
        app_lifecycle_light_t handoff = app_lifecycle_prepare_light();
        assert(handoff.jobs_ready == (cover_ok && avatar_ok));
        assert(handoff.ble_stopped == (cover_ok && avatar_ok && ble_error == ESP_OK));
        if (!cover_ok) expect("C"); else if (!avatar_ok) expect("CA"); else expect("CAL");
    }
    for (unsigned i = 0; i < 100; ++i) {
        reset(); transfer_ok = i & 1;
        assert(app_lifecycle_pause_for_lock() == transfer_ok); expect("UTW");
    }
    puts("app_lifecycle: PASS (checkpoint, short-circuit, budgets, awake fallback, repeat handoff)");
}
