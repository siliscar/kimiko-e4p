/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 *
 * 中文：当月阅读手账、日期及插画轮换模型；不读取时钟或存储。
 * English: Monthly journal, civil dates and scene rotation, without clock or storage IO.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define READING_JOURNAL_SCENES 6
#define READING_JOURNAL_CHAPTERS 8192
#define READING_JOURNAL_RECORDS 512
#define READING_JOURNAL_FILE_LIMIT (640u * 1024u)

typedef struct {
    int year, month, day, weekday, days, first_weekday;
    int32_t serial;
} reading_journal_date_t;

typedef struct reading_journal_record {
    struct reading_journal_record *next;
    uint64_t book;
    uint32_t milliseconds, turns;
    uint16_t chapters;
    uint8_t day;
    char title[128];
    uint8_t visited[READING_JOURNAL_CHAPTERS / 8];
} reading_journal_record_t;

typedef struct {
    reading_journal_record_t *records;
    uint32_t month;
    unsigned count, scene;
    int32_t scene_day;
    bool scene_ready;
} reading_journal_model_t;

typedef struct {
    uint64_t milliseconds;
    uint32_t turns, chapters;
    unsigned books, days;
} reading_journal_totals_t;

/// 固定UTC+8，未对时或越界返回false；周一为0。/ Fixed UTC+8; reject unsynced/out-of-range time; Monday is zero.
bool reading_journal_date(int64_t utc_ms, reading_journal_date_t *out);
void reading_journal_clear(reading_journal_model_t *model);
/// 月变清记录，跨日轮换，回拨保留插画并重设日期基线。/ Clear records on month changes; advance scenes on forward days and rebase rollbacks.
bool reading_journal_sync(reading_journal_model_t *model, const reading_journal_date_t *date);
void reading_journal_next_scene(reading_journal_model_t *model);
uint64_t reading_journal_book_id(const char *path, uint32_t size);
/// 每日每书每章只计一次；失败不删除既有数据。/ Deduplicate chapters per book/day; failures preserve existing data.
bool reading_journal_add(reading_journal_model_t *model, unsigned day, uint64_t book,
                         const char *title, unsigned chapter, uint32_t milliseconds, uint32_t turns);
/// day=0汇总当月，books按书去重，chapters为每日不同章节之和。/ day=0 totals the month; books are distinct, chapters sum daily distinct chapters.
reading_journal_totals_t reading_journal_totals(const reading_journal_model_t *model, unsigned day);
const reading_journal_record_t *reading_journal_at(const reading_journal_model_t *model, unsigned day, unsigned index);
/// 显式小端格式与完整校验；解码失败不替换原模型。/ Explicit little-endian format with checksum; failed decoding leaves the destination intact.
size_t reading_journal_encode(const reading_journal_model_t *model, uint64_t sequence, uint8_t *out, size_t capacity);
bool reading_journal_decode(reading_journal_model_t *model, uint64_t *sequence, const uint8_t *data, size_t length);
