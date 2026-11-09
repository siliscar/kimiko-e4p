/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：触摸震动。短脉冲由 esp_timer 单次定时关断，不依赖主循环 tick；
 * 强度只改脉宽，不做软 PWM。板级接口见 board_motor()。
 * English: Touch vibration. One esp_timer shot turns the motor off, independent of the
 * main-loop tick; intensity only changes the pulse width, with no soft PWM. The board
 * primitive is board_motor().
 *
 * 冻结：只做短脉冲，不提供连续震动；睡前与断电前必须关断。
 * Frozen: short pulses only, never continuous vibration; always stop before sleep or power-off.
 */
#include "haptics.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "board_hardware.h"

#define TAG "haptics"

static esp_timer_handle_t s_timer;
static bool s_available;
static bool s_enabled = true;
static uint8_t s_intensity = APP_HAPTIC_WEAK;
static bool s_pulsing;

static uint32_t pulse_ms(uint8_t intensity) {
    switch (intensity) {
        case APP_HAPTIC_MEDIUM: return 50;
        case APP_HAPTIC_STRONG: return 70;
        default: return 35;
    }
}

// 定时器任务里关断：主循环可能在刷屏里阻塞几百毫秒，不能靠 tick 收尾。
// / Cut the motor in the timer task: the main loop can block for hundreds of ms in a refresh.
static void on_pulse_end(void *arg) {
    (void)arg;
    s_pulsing = false;
    (void)board_motor(false);
}

void haptics_init(void) {
    s_available = board_has_motor();
    if (!s_available) return;
    if (s_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = on_pulse_end,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "haptic",
            .skip_unhandled_events = true,
        };
        if (esp_timer_create(&args, &s_timer) != ESP_OK) {
            ESP_LOGE(TAG, "timer create failed");
            s_available = false;
            return;
        }
    }
    (void)board_motor(false);
    ESP_LOGI(TAG, "ready enabled=%d intensity=%u pulse=%ums",
             (int)s_enabled, (unsigned)s_intensity, (unsigned)pulse_ms(s_intensity));
}

bool haptics_available(void) { return s_available; }
bool haptics_enabled(void) { return s_enabled; }
uint8_t haptics_intensity(void) { return s_intensity; }

void haptics_set_enabled(bool enabled) {
    s_enabled = enabled;
    if (!enabled) haptics_stop();
}

void haptics_set_intensity(uint8_t intensity) {
    s_intensity = intensity <= APP_HAPTIC_STRONG ? intensity : APP_HAPTIC_STRONG;
}

void haptics_pulse(void) {
    if (!s_available || !s_enabled || s_pulsing || s_timer == NULL) return;
    // 先停旧定时器再上电，避免上一记的回调在本次上电后立刻关断。
    // / Stop the previous shot before powering up so its callback cannot cut this pulse.
    (void)esp_timer_stop(s_timer);
    if (board_motor(true) != ESP_OK) return;
    s_pulsing = true;
    if (esp_timer_start_once(s_timer, (uint64_t)pulse_ms(s_intensity) * 1000ULL) != ESP_OK) {
        s_pulsing = false;
        (void)board_motor(false);
    }
}

void haptics_stop(void) {
    if (s_timer != NULL) (void)esp_timer_stop(s_timer);
    s_pulsing = false;
    if (s_available) (void)board_motor(false);
}
