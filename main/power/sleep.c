/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 锁屏进睡、浅睡等待按键或拿起、软睡/关机拉掉 EN。
 *
 * Enter lock and sleep, light-sleep wait for key or pickup, and drop EN
 * for soft sleep / power-off.
 * 用户修订：票根从任意页面进入均用系统字体；浅睡唤醒恢复此前阅读字体，深睡由启动检查点恢复。
 * User revision: tickets use the system face from every page; light wake restores the previous reader face, while deep wake uses the startup checkpoint.
 * 用户修订：其他样式/自动锁屏先准备完整前缓冲，再清屏并做原 GC16 输出，缩短白屏等待。
 * User revision: other styles and idle locks prepare the complete front buffer before the original clear/GC16 output, shortening white-screen waits.
 * 用户修订：拼贴的新整帧缓存移到完整显示之后写入，密码层覆盖前提交。
 * User revision: persist new collage frames after complete presentation and before PIN overlays.
 * 用户修订：主动拼贴锁屏立即清白并显示圆锁，随后只用一次原GC16输出完整拼贴；取消占位和四步显影。
 * User revision: manual collage locks promptly present white and a round lock, then present the complete collage once with original GC16; remove placeholders and four-pass reveal.
 * 用户修订：合入官方 a2a63a6 的外设停用、IOE 收口与 CPU 浅睡保持；保留十分钟浅睡、密码和阅读检查点。
 * User revision: port official a2a63a6 peripheral/IOE parking and CPU light-sleep retention while preserving ten-minute light sleep, PIN and reader checkpoints.
 * 用户修订：锁屏入口带仅浅睡开关——待机方式选"仅浅睡"时锁屏无期限浅睡、永不断电；其余入口维持十分钟深睡阶梯。
 * User revision: the lock entry takes a light-only switch--shutdown style "light-only" sleeps without deadline and never powers off; other entries keep the ten-minute deep ladder.
 * 用户修订：无 PMU EN 的板（Metalio）深睡交接不存在，十分钟到点自动升级为原厂式电源键脉冲断电；脉冲彻底失败才回退无期限锁屏浅睡。
 * User revision: on boards without PMU EN (Metalio) no deep handoff exists, so the ten-minute mark escalates to the factory-style power-key pulse power-off; only exhausted pulses fall back to a deadline-free locked light sleep.
 * 用户修订：脉冲断电后硬件只能长按开机，锁屏 10 分钟深睡改为主机真深睡——EXT1 盯 IOE 扩展器中断，短按电源键即重启回检查点；脉冲断电只保留给主动关机（APP_SLEEP_OFF）。
 * User revision: after pulse power-off the hardware only boots on a long hold, so the ten-minute lock deep stage is now a true host deep sleep--EXT1 watches the expander interrupt and a short power-key press reboots back to the checkpoint; pulse power-off remains for explicit shutdown (APP_SLEEP_OFF).
 * 用户修订：待机方式「先浅后深」更名为「彻底关机」——锁屏出图后立即按 f326902 的原厂电源键脉冲彻底断电，不再有十分钟浅睡阶梯；此后长按电源键约 1 秒开机。
 * User revision: the standby style "staged" is renamed "full off" and locks pulse off factory-style (f326902) right after the lock art with no ten-minute stage; booting afterwards takes a one-second power-key hold.
 * 用户 2026-11-02 新增：独立的 10/30/60 分钟浅睡后关机档，不改变仅浅睡与立即关机；提前唤醒取消本轮倒计时。
 * User addition of 2026-11-02: separate 10/30/60-minute light-sleep-then-off styles preserve light-only and immediate off; an early wake cancels this countdown.
 * 冻结：沿用实际后缓冲，不把圆锁当白色；不增加帧副本，自动休眠/其他样式/失败回退保留原流程。
 * Frozen: Retain true history including the badge, add no frame copy and preserve idle/other-style/failure recovery.
 */
#include "app_lifecycle.h"
#include "ui_builtin_image.h"

#include "sleep.h"
#include "boot_state.h"
#include "lock_pin.h"
#include "lock_screen.h"
#include "board_sd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "app_font_context.h"
#include "book_cover.h"
#include "book_lock_collage.h"
#include "book_lock_bookmark.h"
#include "book_ticket.h"
#include "cache_wake_restore.h"
#include "s31_sleep_compat.h"
#include "display.h"
#include "display_refresh.h"
#include "frontlight.h"
#include "ota_online.h"
#include "time_service.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#if defined(CONFIG_PM_ESP_SLEEP_POWER_DOWN_CPU)
#include "esp_private/sleep_cpu.h"
#endif
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#if CONFIG_APP_SLEEP_DIAGNOSTICS
#include "esp_app_desc.h"
#include "nvs.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pmu_selftest.h"
#include "board_hardware.h"
#include "board_buzzer.h"
#include "ble_page_turner.h"
#include "device_power.h"
#include "wifi_transfer.h"
#include "sc7a20h_lab.h"
#include "settings.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "ui_wallpaper.h"
#include "ui_lock_feedback.h"

static const char* TAG = "read_pico";
static bool s_lock_feedback_visible;

#define APP_LOCK_KEY_POLL_US (2LL * 1000 * 1000)
#define APP_LOCK_STUCK_IRQ_US (3LL * 1000 * 1000)
#define APP_LOCK_SLEEP_ERROR_LIMIT 5
// PMU 确认后仍未掉 EN 时恢复调用方，禁止无限等待。/ Restore the caller if EN stays high after acknowledgement instead of waiting forever.
#define HOST_EN_DROP_WAIT_MS 5000

void app_lock_feedback(EpdiyHighlevelState *hl) {
    s_lock_feedback_visible = false;
    if (!hl || app_settings_lock_style() != 2 || pico_online_busy()) return;
    wifi_transfer_status_t transfer = {0};
    wifi_transfer_get_status(&transfer);
    if (transfer.state == WIFI_TRANSFER_UPLOADING) return;
    uint8_t *framebuffer = epd_hl_get_framebuffer(hl);
    if (!framebuffer || epd_rotated_display_width() < 97 || epd_rotated_display_height() < 97) return;
    display_main_transition_cancel();
    // 原GC16清白适用于任意旧灰阶，不用只保证黑白端点的短DU擦整屏，无额外定时停留。
    // Calibrated GC16 whitens any prior gray; endpoint-only short DU must not erase a whole gray page.
    epd_hl_set_all_white(hl);
    if (update_display_full(hl) != EPD_DRAW_SUCCESS) return;
    EpdRect area = ui_lock_feedback_draw(framebuffer);
    s_lock_feedback_visible = display_refresh_area(hl, DISPLAY_REFRESH_FEEDBACK, area, DISPLAY_REFRESH_DEFAULT) == EPD_DRAW_SUCCESS;
}

static bool draw_wallpaper(uint8_t *framebuffer) {
    const char *path = app_settings_wallpaper_path();
    return app_settings_lock_style() == 1 &&
        ui_wallpaper_prepare_lock(framebuffer,path);
}

static cst836u_handle_t s_tp;
static sc7a20h_handle_t s_acc;
static bool s_acc_was_on;

typedef enum {
    SLEEP_TRACE_KEY, SLEEP_TRACE_PICKUP, SLEEP_TRACE_JOBS_BUSY,
    SLEEP_TRACE_IRQ_STUCK, SLEEP_TRACE_TIMER_ERROR, SLEEP_TRACE_SLEEP_ERROR,
    SLEEP_TRACE_TIMEOUT,
} sleep_trace_exit_t;

typedef struct {
    uint32_t version, exit, wake, ble_blocked;
    int64_t start_us, total_us, ok_call_us, rejected_call_us, min_call_us, max_call_us, unix_s;
    uint32_t attempts, successes, failures, timer_wakes, gpio_wakes, other_wakes;
    uint32_t short_calls, timer_errors, irq_low_polls, unresolved_gpio, key_latched, key_read_errors;
    int32_t last_error;
    char elf[17];
} sleep_trace_t;

#if CONFIG_APP_SLEEP_DIAGNOSTICS
// 只在浅睡会话外读写一份汇总；调用停留时间不是电流或精确睡眠时间。
// Read/write one summary outside the sleep session; call residence is neither current nor exact sleep time.
static void sleep_trace_log(const sleep_trace_t *trace, const char *origin, esp_err_t save_error) {
    ESP_LOGI(TAG, "SLEEP_SUMMARY origin=%s elf=%s unix_s=%lld start_us=%lld exit=%lu wake=%lu ble_blocked=%lu attempts=%lu ok=%lu failed=%lu timer=%lu gpio=%lu other=%lu lt80ms=%lu timer_errors=%lu irq_low=%lu unresolved_gpio=%lu key_latched=%lu key_read_errors=%lu last_error=%ld total_us=%lld ok_call_us=%lld rejected_call_us=%lld outside_call_us=%lld min_call_us=%lld max_call_us=%lld save_error=%d",
        origin, trace->elf, (long long)trace->unix_s, (long long)trace->start_us,
        (unsigned long)trace->exit, (unsigned long)trace->wake, (unsigned long)trace->ble_blocked,
        (unsigned long)trace->attempts, (unsigned long)trace->successes, (unsigned long)trace->failures,
        (unsigned long)trace->timer_wakes, (unsigned long)trace->gpio_wakes, (unsigned long)trace->other_wakes,
        (unsigned long)trace->short_calls, (unsigned long)trace->timer_errors, (unsigned long)trace->irq_low_polls,
        (unsigned long)trace->unresolved_gpio, (unsigned long)trace->key_latched, (unsigned long)trace->key_read_errors,
        (long)trace->last_error, (long long)trace->total_us, (long long)trace->ok_call_us,
        (long long)trace->rejected_call_us,
        (long long)(trace->total_us - trace->ok_call_us - trace->rejected_call_us),
        (long long)trace->min_call_us, (long long)trace->max_call_us, (int)save_error);
}
#endif

static void sleep_trace_replay(void) {
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    nvs_handle_t handle;
    if (nvs_open("sleep_diag", NVS_READONLY, &handle) != ESP_OK) return;
    sleep_trace_t trace = {0};
    size_t size = sizeof(trace);
    esp_err_t error = nvs_get_blob(handle, "last", &trace, &size);
    nvs_close(handle);
    if (error != ESP_OK) return;
    if (size != sizeof(trace) || trace.version != 1 || trace.elf[16] != '\0' ||
        trace.exit > SLEEP_TRACE_TIMEOUT || trace.wake > APP_WAKE_TIMEOUT ||
        (uint64_t)trace.successes + trace.failures != trace.attempts ||
        trace.total_us < 0 || trace.ok_call_us < 0 || trace.rejected_call_us < 0 ||
        trace.ok_call_us > trace.total_us || trace.rejected_call_us > trace.total_us - trace.ok_call_us) {
        ESP_LOGW(TAG, "SLEEP_SUMMARY saved record invalid");
        return;
    }
    sleep_trace_log(&trace, "saved", ESP_OK);
#endif
}

static void sleep_trace_record(sleep_trace_t *trace, esp_err_t error, uint32_t causes, int64_t elapsed_us) {
    ++trace->attempts;
    if (error != ESP_OK) {
        ++trace->failures;
        trace->last_error = error;
        trace->rejected_call_us += elapsed_us;
        return;
    }
    ++trace->successes;
    trace->ok_call_us += elapsed_us;
    if (trace->successes == 1 || elapsed_us < trace->min_call_us) trace->min_call_us = elapsed_us;
    if (elapsed_us > trace->max_call_us) trace->max_call_us = elapsed_us;
    if (elapsed_us < 80000) ++trace->short_calls;
    if (causes & (1UL << ESP_SLEEP_WAKEUP_TIMER)) ++trace->timer_wakes;
    if (causes & (1UL << ESP_SLEEP_WAKEUP_GPIO)) ++trace->gpio_wakes;
    if (!causes || (causes & ~((1UL << ESP_SLEEP_WAKEUP_TIMER) | (1UL << ESP_SLEEP_WAKEUP_GPIO))))
        ++trace->other_wakes;
}

static void sleep_trace_finish(sleep_trace_t *trace, sleep_trace_exit_t exit, app_wake_source_t wake) {
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    trace->version = 1;
    trace->exit = exit;
    trace->wake = wake;
    trace->total_us = esp_timer_get_time() - trace->start_us;
    trace->unix_s = time(NULL);
    (void)esp_app_get_elf_sha256(trace->elf, sizeof(trace->elf));
    nvs_handle_t handle;
    esp_err_t error = nvs_open("sleep_diag", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "last", trace, sizeof(*trace));
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    sleep_trace_log(trace, "live", error);
#else
    (void)trace;
    (void)exit;
    (void)wake;
#endif
}

// 只保全成功 GPIO 唤醒后实际读到的 P17 低；共享中断本身不证明电源键。
// Preserve only a P17 low observed after successful GPIO wake; shared IRQ alone never proves POWER.
static bool lock_capture_power_key(esp_err_t error, uint32_t causes, bool *read_failed) {
    *read_failed = false;
    if (error != ESP_OK || !(causes & (1UL << ESP_SLEEP_WAKEUP_GPIO))) return false;
    fca9555_handle_t ioe = board_fca9555();
    uint8_t input = 0xff;
    if (!ioe || fca9555_read_input(ioe, 1, &input) != ESP_OK) {
        *read_failed = true;
        return false;
    }
    return !(input & (FCA9555_P17 >> 8));
}

void app_sleep_bind(cst836u_handle_t tp, sc7a20h_handle_t acc) {
    s_tp = tp;
    s_acc = acc;
    app_s31_sleep_init();
    // 没开 PM 时 IDF 不分配 CPU 保存区，浅睡里 CPU 不会掉电；这里手动申请一次。
    // Without PM, IDF never allocates CPU retention, so light sleep keeps the CPU powered; request it once here.
#if defined(CONFIG_PM_ESP_SLEEP_POWER_DOWN_CPU)
    esp_err_t err = sleep_cpu_configure(true);
    if (err != ESP_OK) ESP_LOGW(TAG, "CPU light-sleep power-down off: %s", esp_err_to_name(err));
#endif
    sleep_trace_replay();
}

// 触摸、加速度计都在常开 3V3 上，ESP 浅睡或 EN 拉低都不会断它们的电，必须主动送进最低功耗态。
// Touch and accelerometer sit on the always-on 3V3; neither light sleep nor EN low cuts them, so park them explicitly.
static void periph_suspend(bool host_off) {
    // 前光最先灭：锁屏图已经上屏，接下来不应再照在面板上。
    // Frontlight first: the lock image is already on the panel and should not stay lit.
    frontlight_suspend();
    if (board_buzzer_score_busy()) (void)board_buzzer_score_stop();
    esp_err_t ioe = board_ioe_sleep(host_off);
    if (ioe != ESP_OK) ESP_LOGW(TAG, "ioe sleep %s", esp_err_to_name(ioe));
    s_acc_was_on = sc7a20h_powered(s_acc);
    if (s_acc_was_on) (void)sc7a20h_power_down(s_acc);
    if (s_tp != NULL && cst836u_get_mode(s_tp) != CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_set_mode(s_tp, CST836U_MODE_DEEPSLEEP);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch deepsleep %s", esp_err_to_name(err));
    }
}

// 深睡的触摸只认 TP_RST 复位；加速度计按睡前的配置重新上电。
// Deep-sleeping touch only wakes via TP_RST; the accelerometer comes back with its pre-sleep config.
static void periph_resume(void) {
    (void)board_ioe_wake();
    // 醒后恢复用户睡前的前光状态；切换失败也继续叫醒触摸与加速度计。
    // Restore the user's frontlight state; wake touch/accelerometer even if it fails.
    frontlight_resume();
    if (s_tp != NULL && cst836u_get_mode(s_tp) == CST836U_MODE_DEEPSLEEP) {
        esp_err_t err = cst836u_wake(s_tp);
        if (err != ESP_OK) ESP_LOGW(TAG, "touch wake %s", esp_err_to_name(err));
    }
    if (s_acc_was_on) (void)sc7a20h_power_up(s_acc);
    s_acc_was_on = false;
}

static void lock_arm_ioe_wakeup(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_IOE_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    board_clear_ioe_int();
    gpio_wakeup_enable((gpio_num_t)BOARD_IOE_INT_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void app_lock_wait_key_idle(int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        device_power_drain_events();
        board_clear_ioe_int();
        bool held = false;
        if (device_power_poll() == ESP_OK) {
            held = (device_power_get()->key_state & 0x01) != 0;
        }
        bool int_high = gpio_get_level((gpio_num_t)BOARD_IOE_INT_GPIO) != 0;
        if (!held && int_high) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= 150) return;
        } else {
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

app_wake_source_t app_last_wake_source(void) {
    return (app_wake_source_t)app_settings_last_wake();
}

static void pickup_ack(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_read_events(acc, &ev);
    sc7a20h_ack_int(acc);
}

static bool pickup_ia(sc7a20h_handle_t acc) {
    sc7a20h_events_t ev;
    sc7a20h_aoi_src_t aoi;
    if (sc7a20h_read_events(acc, &ev) != ESP_OK) return false;
    sc7a20h_aoi_decode(ev.aoi1_src, &aoi);
    return aoi.ia;
}

// 共线高电平稳定一段时间后才进浅睡。
// Wait for a stable high level on the shared line before light sleep.
static bool pickup_wait_quiet(sc7a20h_handle_t acc, int quiet_ms, int timeout_ms) {
    int64_t start = esp_timer_get_time() / 1000;
    int64_t high_from = 0;
    while (esp_timer_get_time() / 1000 - start < timeout_ms) {
        if (sc7a20h_int1_level(acc) > 0) {
            if (high_from == 0) high_from = esp_timer_get_time() / 1000;
            if (esp_timer_get_time() / 1000 - high_from >= quiet_ms) return true;
        } else {
            pickup_ack(acc);
            board_clear_ioe_int();
            high_from = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return false;
}

static app_wake_source_t light_sleep_wait_until(sc7a20h_handle_t acc, int64_t deadline_us) {
    sleep_trace_t trace = {.start_us = esp_timer_get_time()};
    const app_lifecycle_light_t handoff = app_lifecycle_prepare_light();
    if (!handoff.jobs_ready) {
        sleep_trace_finish(&trace, SLEEP_TRACE_JOBS_BUSY, APP_WAKE_KEY);
        return APP_WAKE_KEY;
    }
    const bool key_sampling = device_power_key_sampler_enable(false);
    // 蓝牙控制器不能带着活动状态进浅睡：醒来后的第一条 HCI 命令会踩到 BTDM 控制器内部的
    // 链表断言（ble_hw.c）。锁屏不是用户关蓝牙，停栈时保留重连目标，解锁后主循环重新起栈
    // 并自动连回；停不下来就不进浅睡，保持唤醒轮询，绝不把活动控制器送进浅睡。
    // The BT controller must not stay active across light sleep: its first HCI command after wake
    // trips an internal linked-list assert in the BTDM controller (ble_hw.c). Locking is not an
    // explicit Bluetooth-off, so the teardown keeps the reconnect target and the main loop
    // restarts the stack after unlock. If the teardown fails, never light-sleep; keep polling
    // awake rather than handing an active controller to the sleep path.
    const bool ble_stopped = handoff.ble_stopped;
    trace.ble_blocked = !ble_stopped;
    if (!ble_stopped) ESP_LOGW(TAG, "BLE stop before light sleep failed; staying awake");
    // 首次浅睡前抓 CACHE_L1 配置快照；之后每次醒来写回，喂行产能才回得来。
    // Snapshot CACHE_L1 before the first light sleep; restore on every wake so
    // the feed capacity comes back with it.
    cache_wake_restore_snapshot();
    bool pickup = acc != NULL && app_settings_pickup_wake();
    bool acc_armed = false;
    periph_suspend(false);
    lock_arm_ioe_wakeup();
    if (pickup) {
        const esp_err_t arm_error = sc7a20h_arm_pickup_wake(acc, NULL);
        acc_armed = arm_error == ESP_OK;
        if (!acc_armed) pickup = false;
        pickup_ack(acc);
        if (!pickup || !pickup_wait_quiet(acc, 400, 2000)) {
            pickup = false;
            ESP_LOGW(TAG, "pickup INT1 unavailable or stuck low, key only");
        } else {
            const esp_err_t wake_error = sc7a20h_config_light_sleep_wakeup(acc);
            if (wake_error != ESP_OK || !pickup_wait_quiet(acc, 400, 1500)) {
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                pickup = false;
                ESP_LOGW(TAG, "pickup INT1 re-asserted, key only");
            } else {
                // 安静窗口里若 INT1 闪过，重新设置低电平唤醒。
                // Re-arm active-low wake after an INT1 glitch in the quiet window.
                gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                if (sc7a20h_config_light_sleep_wakeup(acc) != ESP_OK ||
                    sc7a20h_int1_level(acc) <= 0) {
                    gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
                    pickup = false;
                    ESP_LOGW(TAG, "pickup INT1 low at sleep, key only");
                } else {
                    ESP_LOGI(
                        TAG, "wait key GPIO%d or pickup GPIO%d",
                        BOARD_IOE_INT_GPIO, (int)sc7a20h_int1_gpio(acc)
                    );
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "wait key on IOE_INT GPIO%d", BOARD_IOE_INT_GPIO);
    }

    if (!pickup) {
        if (acc_armed) (void)sc7a20h_power_down(acc);
        lock_arm_ioe_wakeup();
    }

    app_wake_source_t wake = APP_WAKE_NONE;
    sleep_trace_exit_t exit = SLEEP_TRACE_KEY;
    int64_t ioe_low_since_us = 0;
    unsigned sleep_errors = 0;
    esp_err_t last_sleep_error = ESP_OK;
    int64_t last_sleep_error_log_ms = 0;
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_LOCK);
    for (;;) {
        int64_t now_us = esp_timer_get_time();
        // The PMU event is authoritative even if the IOE edge was missed.
        // Timer wakeups below provide a bounded fallback poll for that case.
        // 即使漏掉 IOE 中断，也由定时唤醒轮询 PMU 按键事件。
        if (device_power_take_key_wakeup()) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && pickup_ia(acc)) {
            wake = APP_WAKE_PICKUP;
            exit = SLEEP_TRACE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (deadline_us && now_us >= deadline_us) {
            wake = APP_WAKE_TIMEOUT;
            exit = SLEEP_TRACE_TIMEOUT;
            break;
        }
        board_clear_ioe_int();
        if (gpio_get_level((gpio_num_t)BOARD_IOE_INT_GPIO) == 0) {
            ++trace.irq_low_polls;
            if (!ioe_low_since_us) ioe_low_since_us = now_us;
            if (now_us - ioe_low_since_us >= APP_LOCK_STUCK_IRQ_US) {
                // Return to the active page instead of leaving an inert lock
                // image when a non-key IOE interrupt cannot be cleared.
                // 非按键中断一直清不掉时，退回可操作页面，避免像死机一样停在锁屏。
                ESP_LOGW(TAG, "lock IOE_INT stuck low; recover active page");
                wake = APP_WAKE_KEY;
                exit = SLEEP_TRACE_IRQ_STUCK;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        ioe_low_since_us = 0;
        if (pickup && sc7a20h_int1_level(acc) == 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
            continue;
        }
        if (!ble_stopped) {
            // 停栈失败：不睡，按同一轮询节奏等按键，锁屏画面保持不动。
            // Teardown failed: stay awake and poll at the same cadence; the lock face stays put.
            vTaskDelay(pdMS_TO_TICKS(APP_LOCK_KEY_POLL_US / 1000));
            continue;
        }
        int64_t sleep_us = APP_LOCK_KEY_POLL_US;
        if (deadline_us) {
            int64_t remaining_us = deadline_us - esp_timer_get_time();
            if (remaining_us <= 0) continue;
            if (remaining_us < sleep_us) sleep_us = remaining_us;
        }
        esp_err_t timer_error = esp_sleep_enable_timer_wakeup((uint64_t)sleep_us);
        if (timer_error != ESP_OK) {
            ++trace.timer_errors;
            trace.last_error = timer_error;
            ESP_LOGW(TAG, "lock sleep timer rejected: %s", esp_err_to_name(timer_error));
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                wake = APP_WAKE_KEY;
                exit = SLEEP_TRACE_TIMER_ERROR;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        esp_err_t sleep_error = app_s31_light_sleep_start();
        int64_t elapsed_us = esp_timer_get_time() - t0;
        int64_t dt_ms = elapsed_us / 1000;
        // 唤醒路径不恢复 CACHE_L1 仲裁/门控配置，必须在任何刷新前写回。
        // The wake path never restores the CACHE_L1 arbitration/gating words;
        // write them back before any refresh can run.
        if (sleep_error == ESP_OK) {
            cache_wake_restore_apply();
        }
        const uint32_t causes = sleep_error == ESP_OK ? esp_sleep_get_wakeup_causes() : 0;
        bool key_read_failed = false;
        const bool key_latched = lock_capture_power_key(sleep_error, causes, &key_read_failed);
        trace.key_latched += key_latched;
        trace.key_read_errors += key_read_failed;
        sleep_trace_record(&trace, sleep_error, causes, elapsed_us);
#ifdef KIIKO_SLEEP_DIAG
        // 缓存恢复后才输出，避免诊断本身干扰唤醒。/ Log only after cache restore to avoid disturbing wake.
        ESP_LOGI(TAG, "sleep result=%d causes=0x%lx elapsed_ms=%lld", (int)sleep_error,
                 (unsigned long)esp_sleep_get_wakeup_causes(), (long long)dt_ms);
#endif
        const bool key_polled = device_power_take_key_wakeup();
        if (key_latched || key_polled) {
            wake = APP_WAKE_KEY;
            break;
        }
        if (pickup && pickup_ia(acc)) {
            if (dt_ms < 80) {
                ESP_LOGW(TAG, "pickup ignored, sleep %lld ms", (long long)dt_ms);
                pickup_ack(acc);
                pickup_wait_quiet(acc, 400, 1500);
                continue;
            }
            wake = APP_WAKE_PICKUP;
            exit = SLEEP_TRACE_PICKUP;
            ESP_LOGI(TAG, "pickup wake");
            break;
        }
        if (causes & (1UL << ESP_SLEEP_WAKEUP_GPIO)) ++trace.unresolved_gpio;
        board_clear_ioe_int();
        if (pickup && sc7a20h_int1_level(acc) == 0) {
            pickup_ack(acc);
            pickup_wait_quiet(acc, 400, 1500);
        }
        if (sleep_error != ESP_OK) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (sleep_error != last_sleep_error || now_ms - last_sleep_error_log_ms >= 30000) {
                ESP_LOGW(TAG, "lock light sleep rejected: %s, IOE_INT=%d",
                         esp_err_to_name(sleep_error),
                         gpio_get_level((gpio_num_t)BOARD_IOE_INT_GPIO));
                last_sleep_error_log_ms = now_ms;
            }
            last_sleep_error = sleep_error;
            if (++sleep_errors >= APP_LOCK_SLEEP_ERROR_LIMIT) {
                ESP_LOGE(TAG, "lock light sleep repeatedly rejected; recover active page");
                wake = APP_WAKE_KEY;
                exit = SLEEP_TRACE_SLEEP_ERROR;
                break;
            }
            // A rejected sleep must not spin at full CPU speed behind the static e-paper image.
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            last_sleep_error = ESP_OK;
            sleep_errors = 0;
        }
    }

    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_WAKE);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)BOARD_IOE_INT_GPIO);
    if (acc_armed) {
        gpio_wakeup_disable(sc7a20h_int1_gpio(acc));
        sc7a20h_events_t ev;
        sc7a20h_read_events(acc, &ev);
        sc7a20h_power_down(acc);
        sc7a20h_int1_begin(acc);
    }
    periph_resume();
    app_settings_set_last_wake((uint8_t)(wake == APP_WAKE_TIMEOUT ? APP_WAKE_NONE : wake));
    (void)device_power_key_sampler_enable(key_sampling);
    sleep_trace_finish(&trace, exit, wake);
    return wake;
}

app_wake_source_t app_light_sleep_wait(sc7a20h_handle_t acc) {
    return light_sleep_wait_until(acc, 0);
}

app_wake_source_t app_lock_sleep_wait(EpdiyHighlevelState *hl, sc7a20h_handle_t acc) {
    uint8_t minutes = app_settings_shutdown_minutes();
    int64_t deadline_us = minutes ? esp_timer_get_time() + (int64_t)minutes * 60 * 1000000 : 0;
    app_wake_source_t wake = light_sleep_wait_until(acc, deadline_us);
    if (wake != APP_WAKE_TIMEOUT) return wake;
    if (lock_screen_save(epd_hl_get_framebuffer(hl))) (void)pico_boot_mark_frame_cached();
    (void)app_enter_host_sleep(APP_SLEEP_OFF);
    // 下电失败后不反复打脉冲，也不把定时唤醒当解锁；沿用无期限锁屏等待及其故障恢复。
    // Do not repeat failed power pulses or treat timer wakes as unlocks; retain the untimed locked wait and its fault recovery.
    return app_light_sleep_wait(acc);
}

esp_err_t app_enter_host_sleep(app_sleep_mode_t mode) {
    if (mode != APP_SLEEP_OFF && mode != APP_SLEEP_DEEP) return ESP_ERR_INVALID_ARG;
    // 产品页用 on_before_lock 保存并收齐后台；保留阅读内存以便交接失败仍可回到原页。
    // Product pages checkpoint/join in on_before_lock; retain reader state so a failed handoff can resume the same page.
    // 后台对时与封面/头像任务先静默：暂停会快照会话，浅睡唤醒后把它复活，那不是对时想要的。
    // Quiesce background time sync and cover/avatar jobs first: pause snapshots sessions and the
    // light-sleep wake revives them, which a sync never wants.
    if (!app_lifecycle_prepare_poweroff()) return ESP_ERR_INVALID_STATE;
    const bool key_sampling = device_power_key_sampler_enable(false);
    (void)app_sync_time_to_pmu();
    device_power_batt_log_event(DEVICE_POWER_BATT_LOG_OFF);
    (void)device_power_batt_log_flush(250);
    // 必须先断面板轨，再拉低 MODE，避免常开逻辑仍耗电。/ Drop panel rails before parking MODE on the always-on supply.
    pmu_selftest_prepare_powerdown();
    periph_suspend(true);
    esp_err_t err = ESP_FAIL;
    const bool soft = mode != APP_SLEEP_OFF;
    const unsigned attempts = soft ? 3 : 1;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
        err = soft ? device_power_report_sleep() : device_power_power_off();
        ESP_LOGI(TAG, "%s attempt %u: %s", soft ? "SOFT_SLEEP" : "power off",
                 attempt + 1, esp_err_to_name(err));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
            err = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "EN still high after PMU ack");
            break;
        }
        // 明确不支持软睡的板不再空转重试，循环外直接升级断电。/ A board that declares no soft
        // sleep must not spin through retries; escalate to power-off below.
        if (err == ESP_ERR_NOT_SUPPORTED) break;
        if (attempt + 1 < attempts) vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (soft && err == ESP_ERR_NOT_SUPPORTED) {
        // 本板没有 PMU EN，软睡交接不存在：按原厂方向升级为电源键脉冲断电，断电成功不返回。
        // No PMU EN on this board, so no soft-sleep handoff exists: escalate to the
        // factory-style power-key pulse train; a successful power-off does not return.
        ESP_LOGW(TAG, "soft sleep unsupported; escalate to power-off pulses");
        err = device_power_power_off();
        ESP_LOGI(TAG, "power off attempt 1: %s", esp_err_to_name(err));
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(HOST_EN_DROP_WAIT_MS));
            err = ESP_ERR_TIMEOUT;
            ESP_LOGE(TAG, "EN still high after PMU ack");
        }
    }
    // 失败不复位重试；恢复外设，交还锁屏/密码层，阅读恢复检查点仍由原入口处理。
    // Do not reset/retry on failure; restore peripherals to the lock/PIN caller, which retains checkpoint ownership.
    periph_resume();
    wifi_transfer_resume_after_sleep();
    (void)device_power_key_sampler_enable(key_sampling);
    return err;
}

esp_err_t app_sync_time_to_pmu(void) {
    time_t now = time(NULL);
    if (now < 946684800 || now > 4102444799U) {
        ESP_LOGW(TAG, "skip PMU time sync: system clock invalid");
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t seconds = (uint32_t)now;
    uint8_t payload[4] = {
        (uint8_t)(seconds & 0xff),
        (uint8_t)((seconds >> 8) & 0xff),
        (uint8_t)((seconds >> 16) & 0xff),
        (uint8_t)((seconds >> 24) & 0xff),
    };
    esp_err_t err = device_power_cmd(PMU_CMD_TIME_SYNC, payload, sizeof(payload));
    ESP_LOGI(TAG, "PMU time sync before power action: %s", esp_err_to_name(err));
    return err;
}

void app_restart_host(void) {
    (void)app_sync_time_to_pmu();
    pmu_selftest_prepare_powerdown();
    frontlight_suspend();
    esp_err_t err = device_power_action(PMU_ACTION_HOST_NORMAL_RESTART, 250, 0x5052);
    ESP_LOGI(TAG, "normal restart: %s", esp_err_to_name(err));
    if (err != ESP_OK) esp_restart();
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

void enter_lock_and_sleep(
    EpdiyHighlevelState* hl, int64_t* ignore_until_ms, sc7a20h_handle_t acc, cst836u_handle_t touch,
    bool reader_background, bool light_only
) {
    bool feedback_visible = s_lock_feedback_visible;
    s_lock_feedback_visible = false;
    // 分段计时：准备/字体/画面/清屏/出图/按键空闲六段，面板两段除以帧数可反推扫描档。
    // Stage timing: prep/font/art/clear/present/key-idle; the two panel stages divided by their frame counts reveal the scan profile.
    const int64_t lock_t_start = esp_timer_get_time();
    // Refuse to interrupt an active upload. The caller repaints its page when
    // this function returns, and the user can lock again after the transfer.
    // 后台对时同样先取消：它没有上传任务，锁屏不该被一次后台对时拖住。
    // A background sync hands back too: it has no upload to protect, and a lock must never
    // wait on a background sync.
    bool radio_paused = app_lifecycle_pause_for_lock();
    if (!radio_paused) {
        pico_boot_clear_resume();
        ESP_LOGW(TAG, "lock postponed while transfer is busy");
        return;
    }
    const bool key_sampling = device_power_key_sampler_enable(false);
    const int64_t lock_t_prep = esp_timer_get_time();
    uint8_t* framebuffer = epd_hl_get_framebuffer(hl);
    // 主动拼贴在白底圆锁上准备最终画面；其他入口仍保留旧页面，只改RAM。
    // Prepare manual collages behind the white lock badge; other entries keep their prior page while drawing RAM.
    uint8_t lock_style = app_settings_lock_style();
    // 透明书封保留当前页面作为底图，不清白、不重排阅读页，也不读取旧阅读快照。
    // Keep the current page beneath the PNG; no white reset, reader reflow or old reader snapshot.
    if (lock_style != 3) epd_hl_set_all_white(hl);
    bool ticket = lock_style == 0;
    bool system_lock = ticket || lock_style == 2 || lock_pin_enabled();
    app_lock_font_t lock_font = {0};
    if (system_lock) app_font_begin_lock(&lock_font);
    const int64_t lock_t_font = esp_timer_get_time();
    // 阅读票根独占锁屏画布：始终先画最近书籍封面，再叠票根；自定义壁纸只属于壁纸模式。
    // Ticket mode owns the lock canvas: book cover first, ticket on top. Custom wallpaper is wallpaper-only.
    bool lock_drawn = ticket
        ? book_ticket_draw(framebuffer, reader_background)
        : lock_style == 2 ? book_lock_collage_prepare(framebuffer) :
          lock_style == 3 ? book_lock_bookmark_draw(framebuffer,app_settings_bookmark_path()) : draw_wallpaper(framebuffer);
    if (!lock_drawn) {
        epd_hl_set_all_white(hl);
        (void)ui_builtin_image_draw(framebuffer, UI_BUILTIN_IMAGE_LOCK);
    }
    const int64_t lock_t_art = esp_timer_get_time();
    int64_t lock_t_clear = lock_t_art, lock_t_panel = lock_t_art;
    // 透明书封的画布就是"当前页面+叠加图"，与面板真实状态一致，可以直接差分整屏GC16；
    // 其他样式把画布刷成了白底，必须先 epd_clear 让面板变白，from-white 的LUT才成立。
    // The transparent cover's canvas is the current page plus the overlay, matching the real panel, so a
    // differential full GC16 is valid; other styles reset the canvas to white and need the clear first.
    bool keep_page = lock_style == 3 && lock_drawn;
    enum EpdDrawError result = EPD_DRAW_SUCCESS;
    if (feedback_visible) result = update_display_full(hl);
    if (!feedback_visible || result != EPD_DRAW_SUCCESS) {
        if (keep_page) {
            lock_t_clear = esp_timer_get_time();
            result = update_display_full(hl);
            lock_t_panel = esp_timer_get_time();
        } else {
            // 反馈成功时不再重复清白；GC16从真实圆锁历史输出。自动/其他样式/失败保留原清屏。
            // A successful badge needs no second white clear; GC16 uses its true history. Idle/other/fault paths retain original clearing.
            epd_poweron();
            epd_clear();
            lock_t_clear = esp_timer_get_time();
            result = epd_hl_update_screen_from_white(hl, MODE_GC16, 25);
            lock_t_panel = esp_timer_get_time();
        }
    } else lock_t_clear = lock_t_panel = esp_timer_get_time();
    if (lock_style == 2 && lock_drawn && result == EPD_DRAW_SUCCESS) book_lock_collage_cache_commit(framebuffer);
    if (lock_style == 1 && lock_drawn && result == EPD_DRAW_SUCCESS) ui_wallpaper_cache_commit(framebuffer);

    app_lock_wait_key_idle(800);
    const int64_t lock_t_key = esp_timer_get_time();
    ESP_LOGI(TAG, "lock timing style=%u drawn=%d prep=%lld font=%lld art=%lld clear=%lld gc16=%lld panel=%lld keyidle=%lld total=%lld ms",
        (unsigned)lock_style, (int)lock_drawn,
        (long long)((lock_t_prep - lock_t_start) / 1000), (long long)((lock_t_font - lock_t_prep) / 1000),
        (long long)((lock_t_art - lock_t_font) / 1000), (long long)((lock_t_clear - lock_t_art) / 1000),
        (long long)((lock_t_panel - lock_t_clear) / 1000), (long long)((lock_t_panel - lock_t_art) / 1000),
        (long long)((lock_t_key - lock_t_panel) / 1000), (long long)((lock_t_key - lock_t_start) / 1000));
    // 彻底关机档锁屏出图后立即断电交接——有 PMU EN 的板走软睡，没有的板自动升级为原厂脉冲断电（长按电源键约 1 秒开机），
    // 墨水屏保留这张锁屏图，检查点恢复原页面，开机后的密码由启动验证把关；仅浅睡无期限，定时档到时关机。
    // Full off hands off to power-down right after the lock art--soft sleep with PMU EN, an
    // automatic factory pulse power-off without it (a one-second power-key hold boots). The
    // e-paper keeps this lock image, the checkpoint restores the page and cold boots are PIN-gated;
    // "light-only" has no deadline; timed styles power off after their selected light-sleep interval.
    ESP_LOGI(TAG, "lock LIGHT");
    epd_poweroff();
    if (!light_only) {
        if (lock_screen_save(framebuffer)) (void)pico_boot_mark_frame_cached();
        esp_err_t handoff = app_enter_host_sleep(APP_SLEEP_DEEP);
        if (handoff != ESP_OK) {
            // 断电彻底失败（脉冲打满/EN 未掉）：保持锁屏继续无期限浅睡，绝不自行解锁；有密码交回密码层。
            // Power-down exhausted (pulses spent / EN never dropped): stay locked in a deadline-free
            // light sleep and hand PIN locks back to the pad; never self-unlock.
            ESP_LOGW(TAG, "deep sleep unavailable (%s); stay locked", esp_err_to_name(handoff));
            if (lock_pin_enabled()) lock_screen_authenticate(hl, touch, acc, false);
            else (void)app_light_sleep_wait(acc);
        }
    } else if (lock_pin_enabled()) {
        lock_screen_authenticate(hl, touch, acc, false);
    } else {
        (void)app_lock_sleep_wait(hl, acc);
    }
    pico_boot_clear_resume();
    if (system_lock) app_font_end_lock(&lock_font);
    wifi_transfer_resume_after_sleep();

    // 锁屏出图成功时面板和back都停在锁屏画面上：让当前页做一次整屏差分GC16，把叠加图擦回原页面即可。
    // 不清白、不做78帧盲刷，面板不会先白一次再把锁屏图重画一遍；只有出图失败才回到原来的清白重建。
    // After a successful lock the panel and back both hold the lock image: the page's full differential
    // GC16 removes the overlay. No blind clear, so the panel is never whitened and the lock art is never
    // repainted. Only a failed lock present falls back to the original white rebuild.
    device_power_drain_events();
    const bool keep_panel = result == EPD_DRAW_SUCCESS;
    if (!keep_panel) {
        epd_poweron();
        epd_clear();
        epd_hl_set_all_white(hl);
    }
    device_power_drain_events();
    ESP_LOGI(TAG, "unlocked keep_panel=%d", (int)keep_panel);
    // 唤醒键的按下/释放属于锁屏，不能在下一次轮询里变成新的短按再锁一次。
    // The wake press/release belongs to the lock; it must not become a new short press on the next poll and lock again.
    device_power_consume_key_press();
    if (ignore_until_ms) {
        *ignore_until_ms = esp_timer_get_time() / 1000 + APP_LOCK_IGNORE_BOOT_MS;
    }
    (void)device_power_key_sampler_enable(key_sampling);
    ESP_LOGI(TAG, "unlocked");
}

void app_lock_boot_gate(EpdiyHighlevelState *hl, cst836u_handle_t touch, sc7a20h_handle_t acc) {
    if (!lock_pin_enabled()) return;
    // 卡挂载有界等待仅为了字体/锁屏图；没有卡仍必须验证。
    // Bound mount waiting for the face/art only; absent media still requires authentication.
    board_sd_info_t info;
    for (unsigned i = 0; i < 150 && board_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++i)
        vTaskDelay(pdMS_TO_TICKS(20));
    app_lock_font_t saved; app_font_begin_lock(&saved);
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    if (!lock_screen_restore(fb)) {
        // 冷启动丢失锁屏缓存时没有当前页面，用干净底图并继续密码校验。
        // A cold start with no retained art has no current page; use a clean base and keep authentication.
        if (app_settings_lock_style() == 3) memset(fb,255,(size_t)epd_width()*epd_height()/2);
        bool drawn = app_settings_lock_style() == 0 ? book_ticket_draw(fb, false) :
            app_settings_lock_style() == 2 ? book_lock_collage_draw(fb) :
            app_settings_lock_style() == 3 ? book_lock_bookmark_draw(fb,app_settings_bookmark_path()) : draw_wallpaper(fb);
        if (!drawn) { memset(fb, 255, (size_t)epd_width() * epd_height() / 2); (void)ui_builtin_image_draw(fb, UI_BUILTIN_IMAGE_LOCK); }
    }
    lock_screen_authenticate(hl, touch, acc, true);
    app_font_end_lock(&saved);
}
