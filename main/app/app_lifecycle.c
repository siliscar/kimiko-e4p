/*
 * 中文：集中锁屏恢复点和睡眠前的服务交还顺序；各服务保留资源，调用方保留绘制、日志与硬件恢复。
 * English: Centralize lock checkpoints and pre-sleep service handoff order; services retain resources and callers retain painting, logs and hardware recovery.
 */
#include "app_lifecycle.h"
#include "app_content_open.h"
#include "app_registry.h"
#include "boot_state.h"
#include "time_service.h"
#include "weather_service.h"
#include "storage_jobs.h"
#include "ota_online.h"
#include "ota_auto.h"
#include "wifi_transfer.h"
#include "ble_page_turner.h"

void app_lifecycle_checkpoint(app_ctx_t *ctx, const app_desc_t *current) {
    if (current->on_before_lock) current->on_before_lock(ctx);
    int tab = app_index_of(current);
    pico_resume_t resume = {.tab = tab >= 0 && tab < 4 ? (uint8_t)tab : 0};
    bool fullscreen = false;
    if (tab == 1 && app_book_resume_context(resume.path, sizeof(resume.path), &fullscreen)) {
        resume.reader = 1;
        resume.fullscreen = fullscreen;
    }
    (void)pico_boot_save_resume(&resume);
}

bool app_lifecycle_pause_for_lock(void) {
    app_weather_cancel_join();
    pico_ota_auto_cancel();
    app_time_service_cancel();
    return wifi_transfer_pause_for_sleep();
}

bool app_lifecycle_prepare_poweroff(void) {
    app_weather_cancel_join();
    pico_ota_auto_cancel();
    app_time_service_cancel();
    if (pico_online_busy() || !storage_jobs_quiesce(2000) || !wifi_transfer_pause_for_sleep()) return false;
    (void)ble_pt_stop(2000);
    return true;
}

app_lifecycle_light_t app_lifecycle_prepare_light(void) {
    if (!storage_jobs_quiesce(2000)) return (app_lifecycle_light_t){0};
    return (app_lifecycle_light_t){.jobs_ready = true, .ble_stopped = ble_pt_stop_for_sleep(2000) == ESP_OK};
}
