/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：由阅读整页表派生归一表，只把 to=0 列改成固定的「擦白 18 相 + 推黑 18 相」。
 * 全像素推屏一次后，页面上每个字芯的最后 36 相完全相同，墨度因此一致。
 *
 * English: Derive normalization tables from the reader page table, rewriting only the to=0
 * column to a fixed 18 erase phases followed by 18 black phases. After one full-pixel pass,
 * every glyph core on the page shares the same last 36 phases and therefore the same ink.
 *
 * 冻结：不改白列与相数；基底是与阅读背景无关的整页表。纯文字页保留原厂非黑相等对，
 * 让抗锯齿边缘也一起归一；阅读背景页把非黑相等对清成保持，整屏 pass 不重驱图片灰阶。
 * Frozen: leave the white column and phase count alone, and build on the table that ignores
 * the reading-background switch. Plain-text pages keep the vendor non-black equal pairs so
 * antialiased edges normalize too; background pages hold those pairs so the whole-screen
 * pass never re-drives picture grays.
 */
#include "display_normalize.h"
#include "display_reader_waveform.h"
#include "e0470_epaper_waveform.h"
#include <stdbool.h>
#include <string.h>

// 与 to=0 补强同一组实屏验证过的拍数：白列擦白、原厂 18 次连续推黑。
// The same panel-validated runs as the to=0 boost: the white column's erase and 18 black pushes.
#define NORMALIZE_ERASE 18
#define NORMALIZE_BLACK 18

// 两张表：索引 0 给纯文字页，1 给阅读背景页。构造期固定，不随设置变化。
// Two tables: index 0 for plain-text pages, 1 for pages with a reading background. Both are
// fixed at build time and never follow a setting change.
typedef struct {
    uint8_t data[E0470_GL16_FRAMES][16][4];
    EpdWaveformPhases phases;
    const EpdWaveformPhases* ranges[1];
    EpdWaveformMode mode;
    const EpdWaveformMode* modes[1];
    EpdWaveform waveform;
    bool ready;
} normalize_table_t;

static normalize_table_t s_tables[2];

// from 占该字节两位：0 号位是最高两位。/ from occupies two bits, from=0 is the top pair.
static void set_action(uint8_t (*data)[16][4], int phase, int to, int from, int action) {
    uint8_t *cell = &data[phase][to][from / 4];
    const int shift = 6 - 2 * (from % 4);
    *cell = (uint8_t)((*cell & ~(3u << shift)) | ((unsigned)action << shift));
}

static const EpdWaveform* build_table(normalize_table_t* table, bool hold_gray) {
    if (table->ready) return &table->waveform;
    // 基底取与背景设置无关的整页表：背景派生表相数更少，旧守卫会退化成原样返回，
    // 归一化在开了阅读背景时就静默失效。
    // Build on the background-independent page table: the background derivative carries fewer
    // phases, so the old guard degraded to returning it unchanged and normalization silently
    // stopped working whenever a reading background was set.
    const EpdWaveform* page = display_reader_ink_waveform();
    const EpdWaveformPhases* base = e0470_waveform_phases(page, MODE_GL16);
    if (!base || !base->luts || base->phases != E0470_GL16_FRAMES || base->phase_times ||
        NORMALIZE_ERASE + NORMALIZE_BLACK > E0470_GL16_FRAMES)
        return page;
    memcpy(table->data, base->luts, sizeof(table->data));
    // 背景页：整屏 pass 把整页标脏，未变化像素按相等对查表；原厂除纯白外的 (g,g) 都带
    // 完整擦白推黑往返，背景与图片灰阶会被重驱。清成保持后只有字芯 (0,0) 被归一。
    // Background pages: the whole-screen pass dirties every pixel and unchanged pixels
    // resolve to an equal pair; every vendor (g,g) except pure white carries a full
    // erase-then-black run, which would re-drive background and picture grays. Holding them
    // leaves only the (0,0) glyph core to normalize.
    if (hold_gray)
        for (int g = 1; g < 16; ++g)
            for (int f = 0; f < E0470_GL16_FRAMES; ++f)
                set_action(table->data, f, g, g, 0);
    for (int from = 0; from < 16; ++from) {
        for (int f = 0; f < E0470_GL16_FRAMES; ++f) set_action(table->data, f, 0, from, 0);
        for (int f = 0; f < NORMALIZE_ERASE; ++f) set_action(table->data, f, 0, from, 2);
        for (int f = NORMALIZE_ERASE; f < NORMALIZE_ERASE + NORMALIZE_BLACK; ++f)
            set_action(table->data, f, 0, from, 1);
    }
    table->phases = (EpdWaveformPhases){
        .phases = E0470_GL16_FRAMES, .luts = (const uint8_t*)table->data, .phase_times = NULL};
    table->ranges[0] = &table->phases;
    table->mode = (EpdWaveformMode){5, 1, table->ranges};
    table->modes[0] = &table->mode;
    table->waveform = (EpdWaveform){.num_modes = 1, .num_temp_ranges = 1,
                                    .mode_data = table->modes};
    table->waveform.temp_intervals = page->temp_intervals;
    table->ready = true;
    return &table->waveform;
}

const EpdWaveform* display_normalize_waveform(bool hold_gray) {
    // 变体由调用方按当前画面选择，不看全局背景设置：主页、书架、设置的快捷栏收起
    // 同样要完整归一，否则只有被遮罩盖过的像素多走一轮，整页灰阶拉不齐。
    // The caller picks the variant from what is on screen, never from the global background
    // setting: closing the quick layer on the home, shelf or settings pages needs the full
    // pass too, or only the shaded pixels end one cycle ahead and the grays never line up.
    return build_table(&s_tables[hold_gray ? 1 : 0], hold_gray);
}
