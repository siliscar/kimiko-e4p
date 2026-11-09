#!/usr/bin/env python3
"""中文：验证真实书架封面模块的资源所有权、代次重试、预热与显示确认。
English: Verify real shelf-cover ownership, generation retries, warming and presentation acknowledgement.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UNIT = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "book_shelf_covers.h"
static unsigned generation, begins, cancels, submits, takes, failures, ready, failed;
static bool take_stale;
static book_cover_job_t queued[9];
static bool submitted[9];
unsigned book_cover_jobs_begin(void) {
    ++begins; memset(submitted, 0, sizeof(submitted)); return ++generation;
}
void book_cover_jobs_cancel(unsigned g) {
    ++cancels; if (g == generation) { ++generation; memset(submitted, 0, sizeof(submitted)); }
}
bool book_cover_jobs_submit(unsigned g, unsigned row, const book_cover_job_t *q) {
    ++submits; assert(row < 9);
    if (failures) { --failures; return false; }
    if (g != generation) return false;
    submitted[row] = true; queued[row] = *q; return true;
}
int book_cover_jobs_take(unsigned g, unsigned row, book_cover_result_t *result) {
    ++takes; assert(g == generation && submitted[row]);
    if (take_stale) { ++generation; take_stale = false; return -1; }
    if (failed & (1u << row)) return -1;
    if (!(ready & (1u << row))) return 0;
    *result = (book_cover_result_t){.pixels = malloc(8), .width = 2, .height = 4,
        .fast = queued[row].fast, .white_edge = true};
    assert(result->pixels); memset(result->pixels, (int)row, 8); return 1;
}
static void reset(book_shelf_covers_t *state) {
    book_shelf_covers_cancel(state); book_shelf_covers_release(state);
    ready = failed = failures = 0;
}
static void page(book_shelf_covers_t *state, int first, unsigned rows, int64_t now) {
    book_shelf_covers_page(state, first);
    for (unsigned row = 0; row < rows; ++row)
        book_shelf_covers_prepare(state, row, first + (int)row, true, false, 100, 0, now);
}
int main(void) {
    book_shelf_covers_t state = {0}, other = {0};
    book_cover_job_t q = {.contrast = 100, .phase = 3}; strcpy(q.path, "/book.epub");
    reset(&state); page(&state, 0, 9, 10);
    assert(book_shelf_covers_pending(&state) == 511);
    unsigned before = begins; book_shelf_covers_page(&state, 0); assert(begins == before);
    assert(!book_shelf_covers_collect(&state, 0, &q));
    assert(book_shelf_covers_pending(&state) == 511);
    ready = 3;
    assert(book_shelf_covers_collect(&state, 0, &q));
    assert(book_shelf_covers_collect(&state, 1, &q));
    assert(!book_shelf_covers_reveal(&state, true));
    assert(book_shelf_covers_reveal(&state, false) == 3);
    book_shelf_covers_ack(&state, true, false, false);
    assert(book_shelf_covers_reveal(&state, false) == 3);
    book_shelf_covers_presenting(&state, 3);
    book_shelf_covers_ack(&state, false, false, true);
    assert(book_shelf_covers_reveal(&state, false) == 3);
    // 失败推屏清除本帧掩码，但不能清除待显示像素。/ Failed output clears the frame mask, never dirty artwork.
    book_shelf_covers_ack(&state, true, false, true);
    assert(book_shelf_covers_reveal(&state, false) == 3);
    book_shelf_covers_presenting(&state, 1);
    book_shelf_covers_ack(&state, true, false, true);
    assert(book_shelf_covers_reveal(&state, false) == 2);
    failed = 4; assert(!book_shelf_covers_collect(&state, 2, &q));
    for (unsigned row = 3; row < 9; ++row) book_shelf_covers_skip(&state, row);
    assert(!book_shelf_covers_pending(&state) && book_shelf_covers_reveal(&state, true) == 2);
    book_shelf_covers_ack(&state, true, true, false); assert(!book_shelf_covers_reveal(&state, true));
    assert(!book_shelf_covers_warm_due(&state, 1509)); assert(book_shelf_covers_warm_due(&state, 1510));
    assert(book_shelf_covers_warm_take(&state, 9, 1510) == 0);
    assert(!book_shelf_covers_warm_due(&state, 2509)); assert(book_shelf_covers_warm_due(&state, 2510));
    for (int row = 1; row < 9; ++row) assert(book_shelf_covers_warm_take(&state, 9, 2510) == row);
    assert(book_shelf_covers_warm_take(&state, 9, 2510) == -1);
    assert(!book_shelf_covers_warm_due(&state, 9999));
    const uint8_t *retained = book_shelf_covers_slot(&state, 0)->gray;
    before = cancels; book_shelf_covers_cancel(&state); assert(cancels == before + 1);
    assert(book_shelf_covers_slot(&state, 0)->gray == retained);
    // 已失败空槽在同一页不反复排队。/ Failed empty slots do not requeue on the same page.
    book_shelf_covers_prepare(&state, 2, 2, true, false, 100, 0, 5000);
    assert(!book_shelf_covers_pending(&state));
    reset(&state); page(&state, 9, 4, 100);
    ready = 15; q.fast = true;
    assert(book_shelf_covers_collect(&state, 0, &q));
    const book_shelf_cover_t *slot = book_shelf_covers_slot(&state, 0);
    assert(!slot->gray && slot->fast_bits && slot->width == 2 && slot->height == 4 && slot->fast_white_edge);
    assert(book_shelf_covers_fast_matches(&state, 0, 100, 3));
    assert(!book_shelf_covers_fast_matches(&state, 0, 101, 3));
    assert(!book_shelf_covers_fast_matches(&state, 0, 100, 2));
    unsigned pending = book_shelf_covers_pending(&state);
    book_shelf_covers_prepare(&state, 0, 9, true, true, 100, 3, 9999);
    assert(book_shelf_covers_pending(&state) == pending);
    book_shelf_covers_prepare(&state, 0, 9, true, true, 110, 3, 9999);
    assert(!slot->fast_bits && !slot->gray);
    // 全局队列被其他页面换代后重试；当前上下文像素不受影响。/ Retry after another page replaces the shared queue generation without changing retained pixels.
    before = begins; (void)book_cover_jobs_begin();
    assert(book_shelf_covers_collect(&state, 0, &q)); assert(begins == before + 2);
    failures = 1; before = begins;
    assert(book_shelf_covers_collect(&state, 1, &q)); assert(begins == before + 1);
    failures = 2; before = takes;
    assert(!book_shelf_covers_collect(&state, 2, &q)); assert(takes == before);
    assert(!(book_shelf_covers_pending(&state) & 4));
    take_stale = true; assert(!book_shelf_covers_collect(&state, 3, &q));
    assert(!book_shelf_covers_slot(&state, 3)->gray && !book_shelf_covers_slot(&state, 3)->fast_bits);
    assert(!book_shelf_covers_pending(&state));
    // 清空另一上下文不会释放本上下文像素。/ Clearing another context must not release this context's artwork.
    reset(&other); assert(book_shelf_covers_slot(&state, 0)->fast_bits);
    uint8_t *bits = malloc(8); assert(bits);
    book_shelf_covers_set_fast(&other, 12, bits, false, 80, 7);
    assert(book_shelf_covers_fast_matches(&other, 12, 80, 7));
    book_shelf_covers_drop_fast(&other, 12); book_shelf_covers_drop_fast(&other, 12);
    reset(&other); reset(&state); reset(&state);
    for (unsigned row = 0; row < BOOK_SHELF_COVER_SLOTS; ++row) {
        slot = book_shelf_covers_slot(&state, row);
        assert(!slot->gray && !slot->fast_bits && slot->index == -1);
    }
    page(&state, 18, 4, 200); ready = 1; q.fast = false;
    assert(book_shelf_covers_collect(&state, 0, &q));
    retained = book_shelf_covers_slot(&state, 0)->gray;
    book_shelf_covers_prepare(&state, 0, 18, false, false, 100, 0, 300);
    assert(book_shelf_covers_slot(&state, 0)->gray == retained);
    book_shelf_covers_prepare(&state, 0, 50, false, false, 100, 0, 300);
    assert(!book_shelf_covers_slot(&state, 0)->gray);
    book_shelf_covers_page(&state, 51); assert(!book_shelf_covers_pending(&state));
    reset(&state);
    puts("PASS: real cover context ownership, page reuse, group reveal, failed/partial/full acknowledgement, warm deadlines, cancellation, generation retry, stale/failing jobs and repeated resource release");
}
'''
with tempfile.TemporaryDirectory(prefix='shelf-cover-state-') as folder:
    source, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    source.write_text(UNIT)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-g', '-fsanitize=address,undefined', '-I' + str(ROOT / 'main/book'),
                    str(source), str(ROOT / 'main/book/book_shelf_covers.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
