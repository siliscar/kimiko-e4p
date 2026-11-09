/* 中文：固定十二小时自动检查，复用联网 OTA 的清单校验和可取消任务。
 * English: Fixed twelve-hour checks reuse online OTA manifest validation and cancellable work.
 * 冻结：仅主循环调用；只查清单，失败静默，睡眠期间不主动唤醒。
 * Frozen: Main-loop calls only; manifest checks only, silent failures, no wake during sleep. */
#include "ota_auto.h"
#include "ota_online.h"
#include "settings.h"
#include "device_power.h"
#include "wifi_transfer.h"
#include "esp_timer.h"
#include "nvs.h"
#include <time.h>

#define AUTO_INTERVAL_S (12ULL * 60 * 60)
#define AUTO_INTERVAL_US ((int64_t)AUTO_INTERVAL_S * 1000000)
#define AUTO_CLOCK_MIN 1704067200ULL
#define AUTO_CLOCK_MAX 4102444799ULL
static bool s_loaded, s_checking, s_pending, s_attempted;
static uint64_t s_last_attempt;
static int64_t s_next_us = 15000000, s_attempt_us;

static bool clock_valid(time_t now) {
    return now >= (time_t)AUTO_CLOCK_MIN && now <= (time_t)AUTO_CLOCK_MAX;
}
static void save_attempt(uint64_t now) {
    s_last_attempt = now;
    nvs_handle_t h;
    if (nvs_open("ota_auto", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_u64(h, "last_try", now) == ESP_OK) (void)nvs_commit(h);
        nvs_close(h);
    }
}
static void collect_result(void) {
    if (!s_checking || pico_online_busy()) return;
    pico_update_status_t status;
    pico_online_get_status(&status);
    s_checking = false;
    s_pending = status.state == PICO_UPDATE_AVAILABLE && app_settings_auto_ota();
}
void pico_ota_auto_cancel(void) {
    collect_result();
    if (!s_checking) return;
    pico_online_cancel_join();
    s_checking = false;
}
bool pico_ota_auto_pending(void) {
    if (!s_pending || !app_settings_auto_ota()) return false;
    pico_update_status_t status;
    pico_online_get_status(&status);
    if (status.busy || status.state != PICO_UPDATE_AVAILABLE) s_pending = false;
    return s_pending;
}
void pico_ota_auto_acknowledge(void) { s_pending = false; }

void pico_ota_auto_poll(bool idle) {
    collect_result();
    if (!app_settings_auto_ota()) {
        pico_ota_auto_cancel();
        s_pending = false;
        return;
    }
    if (!idle || s_checking || s_pending || pico_online_busy()) return;
    if (!s_loaded) {
        nvs_handle_t h;
        if (nvs_open("ota_auto", NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_u64(h, "last_try", &s_last_attempt) != ESP_OK) s_last_attempt = 0;
            nvs_close(h);
        }
        if (s_last_attempt < AUTO_CLOCK_MIN || s_last_attempt > AUTO_CLOCK_MAX) s_last_attempt = 0;
        s_loaded = true;
    }
    int64_t monotonic = esp_timer_get_time();
    time_t now = time(NULL);
    if (clock_valid(now) && s_attempted && !s_last_attempt) {
        uint64_t elapsed = monotonic >= s_attempt_us ? (uint64_t)(monotonic - s_attempt_us) / 1000000 : 0;
        save_attempt(elapsed < (uint64_t)now - AUTO_CLOCK_MIN ? (uint64_t)now - elapsed : (uint64_t)now);
    }
    if (monotonic < s_next_us) return;
    if (clock_valid(now)) {
        // 时钟倒退视为新基准，避免追赶；运行中对时后补齐无效时钟的尝试记录。
        // Rebase rollback without catch-up; after time sync, anchor an attempt made with an invalid clock.
        if (s_last_attempt > (uint64_t)now) { save_attempt((uint64_t)now); return; }
        if (s_last_attempt && (uint64_t)now - s_last_attempt < AUTO_INTERVAL_S) return;
    }
    wifi_transfer_status_t network;
    wifi_transfer_get_status(&network);
    bool online = network.mode == WIFI_TRANSFER_MODE_STA && network.network_ready &&
        network.state == WIFI_TRANSFER_READY;
    if (!online && network.state != WIFI_TRANSFER_STOPPED) return;
    if (!online) {
        char ssid[33]; bool saved = false;
        if (wifi_transfer_get_saved_wifi(ssid, &saved) != ESP_OK || !saved) return;
    }
    const pmu_snapshot_t *pmu = device_power_get();
    int percent = pmu_battery_percent(pmu);
    if (percent >= 0 && percent < 10 && !pmu_battery_charging(pmu)) return;
    // 启动失败也消耗本轮，不能每个 poll 重试；UTC 记录跨重启，单调时钟兜底本次开机。
    // A failed start consumes this cycle too; persist UTC across boots and bound this boot monotonically.
    s_next_us = monotonic + AUTO_INTERVAL_US;
    s_attempted = true;
    s_attempt_us = monotonic;
    if (clock_valid(now)) save_attempt((uint64_t)now);
    s_checking = pico_online_check() == ESP_OK;
}
