/* 中文：真实自动 OTA 调度的时间、联网门控和取消回归。
 * English: Real automatic OTA scheduling, network admission and cancellation regressions. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../main/ota/ota_online.h"

enum { NVS_READONLY, NVS_READWRITE };
enum { WIFI_TRANSFER_STOPPED, WIFI_TRANSFER_READY, WIFI_TRANSFER_STARTING };
enum { WIFI_TRANSFER_MODE_AP, WIFI_TRANSFER_MODE_STA };
typedef int nvs_handle_t;
typedef struct { int state, mode; bool network_ready; } wifi_transfer_status_t;
typedef struct { int percent; bool charging; } pmu_snapshot_t;
static bool enabled, credentials, busy, start_ok;
static int checks, cancels, saves;
static uint64_t stored;
static int64_t mono;
static time_t wall;
static pmu_snapshot_t battery;
static wifi_transfer_status_t network;
static pico_update_status_t update;
static int64_t esp_timer_get_time(void) { return mono; }
static bool app_settings_auto_ota(void) { return enabled; }
static const pmu_snapshot_t *device_power_get(void) { return &battery; }
static int pmu_battery_percent(const pmu_snapshot_t *pmu) { return pmu->percent; }
static bool pmu_battery_charging(const pmu_snapshot_t *pmu) { return pmu->charging; }
static void wifi_transfer_get_status(wifi_transfer_status_t *out) { *out = network; }
static int wifi_transfer_get_saved_wifi(char *ssid, bool *saved) { ssid[0] = 0; *saved = credentials; return ESP_OK; }
static int nvs_open(const char *name, int mode, nvs_handle_t *h) { assert(!strcmp(name, "ota_auto")); (void)mode; *h = 1; return ESP_OK; }
static int nvs_get_u64(nvs_handle_t h, const char *key, uint64_t *out) { (void)h; assert(!strcmp(key, "last_try")); *out = stored; return ESP_OK; }
static int nvs_set_u64(nvs_handle_t h, const char *key, uint64_t value) { (void)h; assert(!strcmp(key, "last_try")); stored = value; ++saves; return ESP_OK; }
static int nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
static void nvs_close(nvs_handle_t h) { (void)h; }
bool pico_online_busy(void) { return busy; }
void pico_online_get_status(pico_update_status_t *out) { *out = update; }
esp_err_t pico_online_check(void) { ++checks; if (!start_ok) return -1; busy = true; update.state = PICO_UPDATE_CHECKING; return ESP_OK; }
void pico_online_cancel_join(void) { ++cancels; busy = false; update.state = PICO_UPDATE_CANCELLED; }
#define time(...) wall
#include "../main/ota/ota_auto.c"

static void reset(void) {
    s_loaded = s_checking = s_pending = s_attempted = false;
    s_last_attempt = 0; s_next_us = 15000000; s_attempt_us = 0;
    enabled = credentials = start_ok = true; busy = false;
    checks = cancels = saves = 0; stored = 0; mono = 15000000; wall = 1800000000;
    battery = (pmu_snapshot_t){80, false}; network = (wifi_transfer_status_t){0};
    update = (pico_update_status_t){0};
}
static void finish(pico_update_state_t state) { busy = false; update.state = state; pico_ota_auto_poll(false); }
int main(void) {
    reset(); mono = 14999999; pico_ota_auto_poll(true); assert(!checks);
    mono = 15000000; pico_ota_auto_poll(false); assert(!checks);
    pico_ota_auto_poll(true); assert(checks == 1 && saves == 1 && stored == (uint64_t)wall);
    finish(PICO_UPDATE_AVAILABLE); assert(pico_ota_auto_pending());
    pico_ota_auto_acknowledge(); assert(!pico_ota_auto_pending());
    mono += AUTO_INTERVAL_US - 1; wall += AUTO_INTERVAL_S - 1; pico_ota_auto_poll(true); assert(checks == 1);
    ++mono; ++wall; pico_ota_auto_poll(true); assert(checks == 2);
    finish(PICO_UPDATE_LATEST); assert(!pico_ota_auto_pending());

    reset(); enabled = false; pico_ota_auto_poll(true); assert(!checks && !saves);
    enabled = true; credentials = false; pico_ota_auto_poll(true); assert(!checks);
    credentials = true; network.state = WIFI_TRANSFER_STARTING; pico_ota_auto_poll(true); assert(!checks);
    network = (wifi_transfer_status_t){WIFI_TRANSFER_READY, WIFI_TRANSFER_MODE_AP, true}; pico_ota_auto_poll(true); assert(!checks);
    network = (wifi_transfer_status_t){WIFI_TRANSFER_READY, WIFI_TRANSFER_MODE_STA, true}; credentials = false;
    battery.percent = 9; pico_ota_auto_poll(true); assert(!checks);
    battery.charging = true; pico_ota_auto_poll(true); assert(checks == 1);
    enabled = false; pico_ota_auto_poll(false); assert(cancels == 1 && !busy && !pico_ota_auto_pending());
    enabled = true; pico_ota_auto_poll(true); assert(checks == 1);

    reset(); busy = true; pico_ota_auto_poll(true); pico_ota_auto_cancel(); assert(!checks && !cancels && busy);
    reset(); pico_ota_auto_poll(true); pico_ota_auto_cancel(); assert(cancels == 1 && !busy && !pico_ota_auto_pending());
    reset(); pico_ota_auto_poll(true); busy = false; update.state = PICO_UPDATE_AVAILABLE;
    pico_ota_auto_cancel(); assert(!cancels && pico_ota_auto_pending());

    reset(); start_ok = false; pico_ota_auto_poll(true); assert(checks == 1 && saves == 1);
    for (int i = 0; i < 100; ++i) pico_ota_auto_poll(true);
    assert(checks == 1); mono += AUTO_INTERVAL_US; wall += AUTO_INTERVAL_S;
    pico_ota_auto_poll(true); assert(checks == 2);
    reset(); pico_ota_auto_poll(true); finish(PICO_UPDATE_FAILED); assert(!pico_ota_auto_pending());
    mono += AUTO_INTERVAL_US; wall += AUTO_INTERVAL_S; pico_ota_auto_poll(true); assert(checks == 2);

    reset(); stored = wall - AUTO_INTERVAL_S + 1; pico_ota_auto_poll(true); assert(!checks);
    ++wall; pico_ota_auto_poll(true); assert(checks == 1);
    reset(); stored = wall + 3600; pico_ota_auto_poll(true); assert(!checks && stored == (uint64_t)wall);
    wall += AUTO_INTERVAL_S; pico_ota_auto_poll(true); assert(checks == 1);
    reset(); wall = 0; pico_ota_auto_poll(true); finish(PICO_UPDATE_LATEST); assert(checks == 1 && !saves);
    mono += 60000000; wall = 1800000060; pico_ota_auto_poll(true); assert(checks == 1 && stored == 1800000000);
    puts("ota_auto: PASS (12h boundary, reboot, rollback, invalid time, switch, radio/battery gates, cancellation, silent failure)");
}
