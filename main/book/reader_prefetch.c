/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从阅读页提取逐行预绘与资源回收；页面在推屏结束前交还字体和章节借用。
 * English: Row prefetch and resource cleanup extracted from the reader; the page returns font and chapter borrows before presentation ends.
 */
#include "../diagnostics/reader_memory.h"
#include "reader_prefetch.h"
#include "book_layout.h"
#include "reader_perf.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define BOOK_PREP_RESERVE (1024u * 1024u)
#define BOOK_PREP_IDLE_US 4000
static SemaphoreHandle_t s_draw_lock, s_prep_done;
static TaskHandle_t s_prep_task;
static atomic_bool s_prep_stop;
static book_layout_draw_cursor_t s_prep_cursor;
static uint32_t s_next_generation;
static uint8_t *s_next_fb;
static int s_next_page = -1, s_prep_page = -1;
static EpdRect s_prep_body;
static int s_prep_px;
static size_t s_prep_bytes;

void reader_prefetch_lock(void) {
#if CONFIG_READER_PERF
    READER_PERF_SCOPE(READER_PERF_LOCK);
#endif
    if (s_draw_lock) xSemaphoreTake(s_draw_lock, portMAX_DELAY);
}
void reader_prefetch_unlock(void) {
    if (s_draw_lock) xSemaphoreGive(s_draw_lock);
}
void reader_prefetch_invalidate(void) {
    s_next_page = s_prep_page = -1;
    s_next_generation = 0;
    memset(&s_prep_cursor, 0, sizeof(s_prep_cursor));
}
static int reader_prefetch_step(void) {
    if (!s_next_fb || s_prep_page < 0) return -1;
    if (!s_prep_cursor.generation) memset(s_next_fb, 0xff, s_prep_bytes);
    int result = book_layout_draw_slice(s_next_fb, (size_t)s_prep_page,
                                        s_prep_body, s_prep_px, &s_prep_cursor);
    if (result < 0) reader_prefetch_invalidate();
    else if (result > 0) {
        s_next_page = s_prep_page;
        s_next_generation = book_layout_generation();
    }
    return result;
}
static bool reader_prefetch_buffer(void) {
    if (!s_next_fb && heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) >
            s_prep_bytes + BOOK_PREP_RESERVE)
        s_next_fb = heap_caps_aligned_alloc(16, s_prep_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_next_fb != NULL;
}
static void prep_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        reader_prefetch_lock();
        while (!atomic_load_explicit(&s_prep_stop, memory_order_acquire)) {
            if (reader_prefetch_step() != 0) break;
        }
        reader_prefetch_unlock();
        xSemaphoreGive(s_prep_done);
    }
}
bool reader_prefetch_start(size_t page, EpdRect body, int px, size_t frame_bytes) {
    int target = (int)page;
    if (s_next_page == target && s_next_generation == book_layout_generation()) return false;
    if (s_prep_page != target || s_prep_cursor.generation != book_layout_generation()) {
        reader_prefetch_invalidate();
        s_prep_page = target;
    }
    // 同步推屏期间的几何快照不再读取页面状态。/ Snapshot geometry instead of reading page state during synchronous output.
    s_prep_body = body;
    s_prep_px = px;
    s_prep_bytes = frame_bytes;
    if (!reader_prefetch_buffer()) return false;
    if (!s_draw_lock) s_draw_lock = xSemaphoreCreateMutex();
    if (!s_prep_done) s_prep_done = xSemaphoreCreateBinary();
    if (!s_prep_task && s_draw_lock && s_prep_done &&
        xTaskCreatePinnedToCore(prep_task, "book_prep", 12 * 1024, NULL, 3, &s_prep_task, 1) != pdPASS)
        s_prep_task = NULL;
    if (!s_prep_task) return false;
    xSemaphoreTake(s_prep_done, 0);
    atomic_store_explicit(&s_prep_stop, false, memory_order_release);
    xTaskNotifyGive(s_prep_task);
    return true;
}
void reader_prefetch_end(bool started) {
    if (!started) return;
    atomic_store_explicit(&s_prep_stop, true, memory_order_release);
    // 最多交还当前一行，之后 UI 才可替换字体、章节或页面。
    // Return the current row before the UI replaces fonts, chapters or pages.
    xSemaphoreTake(s_prep_done, portMAX_DELAY);
}
bool reader_prefetch_resume(size_t page) {
    if (!s_next_fb || s_prep_page != (int)page || !s_prep_cursor.started ||
        s_prep_cursor.generation != book_layout_generation()) return false;
    // 连按到部分目标时只补余下行，不清空已有正文。/ Rapid turns finish remaining rows without clearing completed text.
    while (reader_prefetch_step() == 0) {}
    return s_next_page == (int)page && s_next_generation == book_layout_generation();
}
const uint8_t *reader_prefetch_frame(size_t page) {
    return s_next_page == (int)page && s_next_generation == book_layout_generation() ? s_next_fb : NULL;
}
void reader_prefetch_work_idle(size_t page) {
    if (s_prep_page != (int)page || reader_prefetch_frame(page)) return;
    if (!reader_prefetch_buffer()) return;
    const int64_t deadline = esp_timer_get_time() + BOOK_PREP_IDLE_US;
    for (unsigned row = 0; row < 8; ++row) {
        if (reader_prefetch_step() != 0 || esp_timer_get_time() >= deadline) break;
    }
}
void reader_prefetch_discard(void) {
    reader_prefetch_invalidate();
    free(s_next_fb);
    s_next_fb = NULL;
}
void reader_prefetch_release(void) {
    reader_prefetch_discard();
    if (s_prep_task) { vTaskDelete(s_prep_task); s_prep_task = NULL; }
    if (s_prep_done) { vSemaphoreDelete(s_prep_done); s_prep_done = NULL; }
    if (s_draw_lock) { vSemaphoreDelete(s_draw_lock); s_draw_lock = NULL; }
}


#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
void reader_prefetch_memory_stats(reader_memory_stats_t *stats) {
    stats->prefetch = reader_memory_owned(s_next_fb);
    stats->prefetch_worker = s_prep_task != NULL;
    stats->prefetch_stack_free = s_prep_task ? (unsigned)uxTaskGetStackHighWaterMark(s_prep_task) : 0;
}
#endif
