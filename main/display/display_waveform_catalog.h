/*
 * 中文：显示波形的稳定编号、派生来源与用途；查询实际表时才触发原有惰性构造。
 * English: Stable waveform IDs, derivation sources and roles; resolve a table only when requested.
 */
#pragma once
#include <stdbool.h>
#include "epdiy.h"

typedef enum {
    DISPLAY_WAVE_DEFAULT,
    DISPLAY_WAVE_FULL,
    DISPLAY_WAVE_GRAY8,
    DISPLAY_WAVE_FOLLOW,
    DISPLAY_WAVE_TEXT_BASE,
    DISPLAY_WAVE_CAPSULE_DRAG,
    DISPLAY_WAVE_CAPSULE_WHITE,
    DISPLAY_WAVE_CAPSULE_GRAY,
    DISPLAY_WAVE_APPLY,
    DISPLAY_WAVE_MAIN_GRAY,
    DISPLAY_WAVE_SHELF_EXIT,
    DISPLAY_WAVE_MAIN_WATER,
    DISPLAY_WAVE_MAIN_BW,
    DISPLAY_WAVE_MAIN_NORMAL,
    DISPLAY_WAVE_SHELF_GL16,
    DISPLAY_WAVE_SHELF_GC16,
    DISPLAY_WAVE_READER_TEXT,
    DISPLAY_WAVE_READER_INK,
    DISPLAY_WAVE_READER_WATER,
    DISPLAY_WAVE_READER_BACKGROUND,
    DISPLAY_WAVE_READER_IMAGE,
    DISPLAY_WAVE_NORMALIZE,
    DISPLAY_WAVE_NORMALIZE_GRAY,
    DISPLAY_WAVE_COUNT
} display_waveform_id_t;

typedef enum {
    DISPLAY_WAVE_ACTIVE,
    DISPLAY_WAVE_RESERVED,
    DISPLAY_WAVE_EXECUTOR
} display_waveform_role_t;

typedef struct {
    const char *name; ///< 稳定诊断名 / Stable diagnostic name
    const char *source; ///< 构造实现所在路径 / Builder implementation path
    const char *recipe; ///< 派生方法与适用边界 / Derivation and applicability
    display_waveform_id_t parent; ///< 首要基底；COUNT 表示源表 / Primary base; COUNT denotes source data
    display_waveform_role_t role; ///< 产品、备用或执行辅助 / Product, reserved or executor
} display_waveform_descriptor_t;

typedef struct {
    int phases; ///< 实际返回表的相数，含回退 / Actual resolved phase count, including fallback
    int equal_pairs_driven; ///< 会驱动的相等灰度对数量 / Number of driven equal-gray pairs
    int black_pushes_from_white; ///< 白到黑的推黑次数 / Black pushes for white-to-black
    int white_pushes_from_black; ///< 黑到白的擦白次数 / White pushes for black-to-white
} display_waveform_info_t;

/// 不分配、不构造波形；无效编号返回 NULL。/ No allocation or construction; invalid IDs return NULL.
const display_waveform_descriptor_t *display_waveform_descriptor(display_waveform_id_t id);
/// 返回原构造器的结果，保留其内存不足与源表缺失回退。/ Retain the original builder's allocation and missing-source fallback.
const EpdWaveform *display_waveform_get(display_waveform_id_t id);
/// 按实际结果统计驱动信息，不把表长当作实测耗时。/ Inspect actual pulses without treating table length as measured latency.
bool display_waveform_inspect(display_waveform_id_t id, int mode, display_waveform_info_t *info);
