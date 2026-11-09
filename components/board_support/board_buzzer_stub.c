/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 没有蜂鸣器：保留 board_buzzer_* API 的静默实现。
 * 禁用 RMT/LEDC 提示音，避免与 EPD CKV 的 RMT 通道冲突。
 *
 * Metalio E-INK4 Plus has no buzzer: keep the board_buzzer_* API silent.
 * This also avoids RMT/LEDC conflict with the EPD CKV RMT channel.
 */

#include "board_buzzer.h"

#include <string.h>

esp_err_t board_buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms) {
    (void)frequency_hz;
    (void)duration_ms;
    return ESP_OK;
}

esp_err_t board_buzzer_score_play(void) { return ESP_OK; }
esp_err_t board_buzzer_score_stop(void) { return ESP_OK; }
bool board_buzzer_score_busy(void) { return false; }

esp_err_t board_buzzer_probe(board_buzzer_probe_t probe) {
    (void)probe;
    return ESP_OK;
}

void board_buzzer_out_stats_get(board_buzzer_out_stats_t* st) {
    if (st != NULL) memset(st, 0, sizeof(*st));
}

uint32_t board_buzzer_pwm_carrier_hz(void) { return 0; }
uint32_t board_buzzer_pwm_carrier_hz_nominal(void) { return 0; }

void board_buzzer_duty_map_get(buzzer_duty_map_t* map) {
    if (map == NULL) return;
    const buzzer_duty_map_t defaults = BUZZER_DUTY_MAP_EXPERIMENTAL;
    *map = defaults;
}

esp_err_t board_buzzer_duty_map_set(const buzzer_duty_map_t* map) {
    (void)map;
    return ESP_OK;
}

esp_err_t board_buzzer_effect(void) { return ESP_OK; }
