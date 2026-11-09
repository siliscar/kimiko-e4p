/* SPDX-License-Identifier: Apache-2.0 */
/* 触摸震动驱动：脉宽档位、去重、关断与无马达板空操作。
   Touch-vibration driver: pulse widths, in-flight dedupe, stop and no-motor no-op. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "haptics.h"
#include "esp_timer.h"
#include "board_hardware.h"

static esp_timer_create_args_t created;
static int creates, starts, motor_writes;
static uint64_t last_us;
static bool motor_on, has_motor = true, start_fails;

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle) {
    created = *args;
    ++creates;
    *handle = (esp_timer_handle_t)1;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us) {
    (void)handle;
    ++starts;
    last_us = timeout_us;
    return start_fails ? ESP_ERR_INVALID_STATE : ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t handle) { (void)handle; return ESP_OK; }
bool board_has_motor(void) { return has_motor; }
esp_err_t board_motor(bool on) { motor_on = on; ++motor_writes; return ESP_OK; }

int main(int argc, char **argv) {
    const bool no_motor = argc > 1 && !strcmp(argv[1], "nomotor");
    if (no_motor) has_motor = false;

    haptics_init();
    assert(haptics_available() == !no_motor);
    if (no_motor) {
        // 无马达板不建定时器、不写 IO，调用保持静默。
        // A board without a motor creates no timer and writes no IO.
        const int writes = motor_writes;
        haptics_pulse();
        haptics_stop();
        assert(creates == 0 && starts == 0 && motor_writes == writes);
        puts("PASS: no-motor boards stay silent and allocate no timer");
        return 0;
    }

    assert(creates == 1 && !motor_on);
    assert(haptics_enabled() && haptics_intensity() == APP_HAPTIC_WEAK);

    haptics_pulse();
    assert(motor_on && starts == 1 && last_us == 35000);
    // 正在震时的新点击不重启，避免连点变成长震。
    // A tap during a pulse does not restart it, so rapid taps never become one long buzz.
    haptics_pulse();
    assert(starts == 1 && motor_on);
    created.callback(created.arg);
    assert(!motor_on);

    haptics_set_intensity(APP_HAPTIC_MEDIUM);
    haptics_pulse();
    assert(last_us == 50000);
    created.callback(created.arg);
    haptics_set_intensity(APP_HAPTIC_STRONG);
    haptics_pulse();
    assert(last_us == 70000);
    created.callback(created.arg);
    haptics_set_intensity(9);
    assert(haptics_intensity() == APP_HAPTIC_STRONG);

    haptics_set_enabled(false);
    assert(!haptics_enabled() && !motor_on);
    const int writes = motor_writes;
    haptics_pulse();
    assert(motor_writes == writes && !motor_on);

    haptics_set_enabled(true);
    haptics_pulse();
    assert(motor_on);
    haptics_stop();
    assert(!motor_on);

    // 定时器启动失败必须立刻断电，不能留下常转的马达。
    // A failed timer start must cut power immediately, never leaving the motor running.
    start_fails = true;
    haptics_pulse();
    assert(!motor_on);
    start_fails = false;

    puts("PASS: 35/50/70 ms steps, in-flight dedupe, disable/stop and failed-start cutoff");
    return 0;
}
