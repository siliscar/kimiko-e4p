/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * Metalio E-INK4 Plus 没有蜂鸣器，产品构建提供静默兼容接口。
 * 参数和统计类型保留原契约；离线曲谱渲染由 tools/buzzer_score_wav.c 使用。
 *
 * Metalio E-INK4 Plus has no buzzer; the product provides silent compatibility APIs.
 * Parameter and statistics types retain their contracts; tools/buzzer_score_wav.c
 * consumes the offline score renderer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "buzzer_duty_map.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_BUZZER_PATH_CLASSIC = 0,
    BOARD_BUZZER_PATH_HF = 1,
    BOARD_BUZZER_PATH_DIRECT_1BIT = 2,
} board_buzzer_path_t;

typedef enum {
    BOARD_BUZZER_PROBE_A_15K = 0,
    BOARD_BUZZER_PROBE_B_15K = 1,
    BOARD_BUZZER_PROBE_1BIT = 2,
    BOARD_BUZZER_PROBE_4K = 3,
} board_buzzer_probe_t;

typedef struct {
    uint32_t carrier_hz;
    uint8_t resolution_bits;
    uint8_t path;
    uint16_t duty_min;
    uint16_t duty_max;
    uint32_t duty_mean_x100;
    uint32_t duty_ac_rms_x100;
    uint32_t duty_clips;
    uint32_t underruns;
    uint32_t samples;
    int32_t pcm_peak;
    uint32_t pcm_rms;
    uint8_t current_ma_valid;
    int16_t current_ma;
    uint32_t tone_hz;
    uint32_t tone_hz_actual;
    int8_t shift_st;
    uint16_t duty_fs;
    uint16_t duty_hw_max;
    uint8_t residual_low_ticks;
    uint8_t voices;
} board_buzzer_out_stats_t;

esp_err_t board_buzzer_tone(uint32_t frequency_hz, uint32_t duration_ms);

esp_err_t board_buzzer_score_play(void);
esp_err_t board_buzzer_score_stop(void);
bool board_buzzer_score_busy(void);
esp_err_t board_buzzer_probe(board_buzzer_probe_t probe);

void board_buzzer_out_stats_get(board_buzzer_out_stats_t *st);

uint32_t board_buzzer_pwm_carrier_hz(void);
uint32_t board_buzzer_pwm_carrier_hz_nominal(void);

void board_buzzer_duty_map_get(buzzer_duty_map_t *map);
esp_err_t board_buzzer_duty_map_set(const buzzer_duty_map_t *map);

esp_err_t board_buzzer_effect(void);

#ifdef __cplusplus
}
#endif
