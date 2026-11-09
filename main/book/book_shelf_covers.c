/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：提取书架封面资源与队列状态，保留页面原有取消、复用及显示确认顺序。
 * English: Extract shelf cover resources and queue state, preserving cancellation, reuse and presentation acknowledgement order.
 */
#include "book_shelf_covers.h"
#include <stdlib.h>

const book_shelf_cover_t *book_shelf_covers_slot(const book_shelf_covers_t *state, unsigned row) {
    return &state->slots[row];
}
void book_shelf_covers_cancel(book_shelf_covers_t *state) {
    book_cover_jobs_cancel(state->generation);
    state->generation = 0;
}
static void clear_masks(book_shelf_covers_t *state) {
    state->pending = state->warm = state->dirty = state->presenting = 0;
}
void book_shelf_covers_drop_fast(book_shelf_covers_t *state, unsigned row) {
    free(state->slots[row].fast_bits);
    state->slots[row].fast_bits = NULL;
}
void book_shelf_covers_drop_gray(book_shelf_covers_t *state, unsigned row) {
    free(state->slots[row].gray);
    state->slots[row].gray = NULL;
}
void book_shelf_covers_release(book_shelf_covers_t *state) {
    clear_masks(state);
    for (unsigned row = 0; row < BOOK_SHELF_COVER_SLOTS; ++row) {
        book_shelf_covers_drop_gray(state, row);
        book_shelf_covers_drop_fast(state, row);
        state->slots[row].index = -1;
    }
}
void book_shelf_covers_page(book_shelf_covers_t *state, int first_index) {
    if (state->slots[0].index == first_index) return;
    book_shelf_covers_cancel(state);
    state->generation = book_cover_jobs_begin();
    clear_masks(state);
}
bool book_shelf_covers_fast_matches(const book_shelf_covers_t *state, unsigned row, unsigned contrast, unsigned phase) {
    const book_shelf_cover_t *slot = &state->slots[row];
    return slot->fast_bits && slot->fast_contrast == contrast && slot->fast_phase == phase;
}
void book_shelf_covers_prepare(book_shelf_covers_t *state, unsigned row, int index, bool valid,
                               bool packed, unsigned contrast, unsigned phase, int64_t now_ms) {
    book_shelf_cover_t *slot = &state->slots[row];
    if (slot->index == index && (packed ? book_shelf_covers_fast_matches(state, row, contrast, phase) : slot->gray != NULL)) return;
    if (slot->index == index && !slot->gray && !slot->fast_bits) return;
    book_shelf_covers_drop_gray(state, row);
    book_shelf_covers_drop_fast(state, row);
    slot->index = index;
    state->dirty &= ~(1u << row);
    if (!valid) return;
    state->pending |= 1u << row;
    state->warm |= 1u << row;
    state->warm_after_ms = now_ms + 1500;
}
unsigned book_shelf_covers_pending(const book_shelf_covers_t *state) { return state->pending; }
void book_shelf_covers_skip(book_shelf_covers_t *state, unsigned row) { state->pending &= ~(1u << row); }
void book_shelf_covers_set_fast(book_shelf_covers_t *state, unsigned row, uint8_t *pixels,
                               bool white_edge, unsigned contrast, unsigned phase) {
    book_shelf_cover_t *slot = &state->slots[row];
    slot->fast_bits = pixels;
    slot->fast_white_edge = white_edge;
    slot->fast_contrast = contrast;
    slot->fast_phase = phase;
}
bool book_shelf_covers_collect(book_shelf_covers_t *state, unsigned row, const book_cover_job_t *request) {
    if (!state->generation) state->generation = book_cover_jobs_begin();
    if (!book_cover_jobs_submit(state->generation, row, request)) {
        state->generation = book_cover_jobs_begin();
        if (!book_cover_jobs_submit(state->generation, row, request)) {
            book_shelf_covers_skip(state, row);
            return false;
        }
    }
    book_cover_result_t result = {0};
    int status = book_cover_jobs_take(state->generation, row, &result);
    if (!status) return false;
    book_shelf_covers_skip(state, row);
    if (status < 0) return false;
    book_shelf_covers_drop_gray(state, row);
    book_shelf_covers_drop_fast(state, row);
    book_shelf_cover_t *slot = &state->slots[row];
    slot->gray = result.fast ? NULL : result.pixels;
    slot->fast_bits = result.fast ? result.pixels : NULL;
    slot->width = result.width;
    slot->height = result.height;
    slot->fast_white_edge = result.white_edge;
    slot->fast_contrast = request->contrast;
    slot->fast_phase = request->phase;
    state->dirty |= 1u << row;
    return true;
}
unsigned book_shelf_covers_reveal(const book_shelf_covers_t *state, bool grouped) {
    return grouped && state->pending ? 0 : state->dirty;
}
void book_shelf_covers_presenting(book_shelf_covers_t *state, unsigned mask) { state->presenting = mask; }
void book_shelf_covers_ack(book_shelf_covers_t *state, bool success, bool whole_ready, bool batch) {
    if (success) {
        if (whole_ready) state->dirty = 0;
        else if (batch) state->dirty &= ~state->presenting;
    }
    state->presenting = 0;
}
bool book_shelf_covers_warm_due(const book_shelf_covers_t *state, int64_t now_ms) {
    return !state->pending && state->warm && now_ms >= state->warm_after_ms;
}
int book_shelf_covers_warm_take(book_shelf_covers_t *state, unsigned rows, int64_t now_ms) {
    for (unsigned row = 0; row < rows; ++row) {
        if (!(state->warm & (1u << row))) continue;
        state->warm &= ~(1u << row);
        state->warm_after_ms = now_ms + 1000;
        return (int)row;
    }
    return -1;
}
