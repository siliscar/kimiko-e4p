/*
 * 中文：默认关闭的阅读计时，内存低水位覆盖整个操作，输出在计时结束后进行。
 * English: Default-off reader timing with operation-local heap lows; emit only after timing stops.
 */
#include "sdkconfig.h"
#if CONFIG_READER_PERF
#include "reader_perf.h"
#include "reader_memory.h"
#include <inttypes.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ttf_font.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    const char *operation;
    unsigned generation;
    bool active, local_heap, display_started;
    int64_t started, input_us, software_us;
    uint64_t stages[READER_PERF_STAGE_COUNT];
} s_sample;
static int64_t s_input_started;

void reader_perf_input_begin(void) {
    s_input_started = esp_timer_get_time();
}

void reader_perf_begin(const char *operation) {
    reader_perf_finish(false);
    const int64_t now = esp_timer_get_time();
    bool local_heap = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
    ttf_bench_begin();
    portENTER_CRITICAL(&s_lock);
    unsigned generation = s_sample.generation + 1;
    if (!generation) generation = 1;
    memset(&s_sample, 0, sizeof(s_sample));
    s_sample.generation = generation;
    s_sample.operation = operation;
    s_sample.started = now;
    s_sample.input_us = s_input_started && now >= s_input_started ? now - s_input_started : 0;
    s_sample.local_heap = local_heap;
    s_sample.active = true;
    portEXIT_CRITICAL(&s_lock);
}

reader_perf_scope_t reader_perf_scope_begin(reader_perf_stage_t stage) {
    reader_perf_scope_t scope = {.stage = stage, .started = esp_timer_get_time()};
    portENTER_CRITICAL(&s_lock);
    scope.generation = s_sample.active ? s_sample.generation : 0;
    if (s_sample.active && stage == READER_PERF_DISPLAY && !s_sample.display_started) {
        s_sample.software_us = scope.started - s_sample.started;
        s_sample.display_started = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return scope;
}

void reader_perf_scope_end(reader_perf_scope_t *scope) {
    const int64_t elapsed = esp_timer_get_time() - scope->started;
    portENTER_CRITICAL(&s_lock);
    if (scope->generation && scope->generation == s_sample.generation &&
        s_sample.active && (unsigned)scope->stage < READER_PERF_STAGE_COUNT && elapsed >= 0)
        s_sample.stages[scope->stage] += (uint64_t)elapsed;
    portEXIT_CRITICAL(&s_lock);
}

void reader_perf_finish(bool presented) {
    portENTER_CRITICAL(&s_lock);
    if (!s_sample.active) { portEXIT_CRITICAL(&s_lock); return; }
    s_sample.active = false;
    portEXIT_CRITICAL(&s_lock);
    const int64_t total = esp_timer_get_time() - s_sample.started;
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal_free = heap_caps_get_free_size(internal);
    const size_t psram_free = heap_caps_get_free_size(psram);
    const size_t internal_min = s_sample.local_heap ? heap_caps_get_minimum_free_size(internal) : 0;
    const size_t psram_min = s_sample.local_heap ? heap_caps_get_minimum_free_size(psram) : 0;
    if (s_sample.local_heap) (void)heap_caps_monitor_local_minimum_free_size_stop();
    ttf_bench_stats_t glyphs = {0};
    ttf_bench_end(&glyphs);
    // 字体时间与预绘可能重叠，各阶段不是可直接相加的独占分段。
    // Font time may overlap prefetch; stage totals are not additive exclusive slices.
    ESP_LOGI("readerperf", "READER_PERF v=1 id=%u op=%s complete=%d total_us=%lld input_us=%lld software_us=%lld "
             "action_us=%" PRIu64 " source_us=%" PRIu64 " read_us=%" PRIu64 " parse_us=%" PRIu64
             " layout_us=%" PRIu64 " paint_us=%" PRIu64 " display_us=%" PRIu64
             " join_us=%" PRIu64 " lock_us=%" PRIu64
             " glyph_read_us=%lld raster_us=%lld glyph_hits=%" PRIu32 " glyph_misses=%" PRIu32
             " heap_local=%d internal_free=%u internal_min=%u internal_largest=%u"
             " psram_free=%u psram_min=%u psram_largest=%u ui_stack_free=%u",
             s_sample.generation, s_sample.operation, presented, (long long)total,
             (long long)s_sample.input_us, (long long)s_sample.software_us,
             s_sample.stages[READER_PERF_ACTION], s_sample.stages[READER_PERF_SOURCE],
             s_sample.stages[READER_PERF_READ], s_sample.stages[READER_PERF_PARSE],
             s_sample.stages[READER_PERF_LAYOUT], s_sample.stages[READER_PERF_PAINT],
             s_sample.stages[READER_PERF_DISPLAY], s_sample.stages[READER_PERF_JOIN],
             s_sample.stages[READER_PERF_LOCK], (long long)glyphs.read_us,
             (long long)glyphs.raster_us, glyphs.hits, glyphs.misses, s_sample.local_heap,
             (unsigned)internal_free, (unsigned)internal_min,
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)psram_free, (unsigned)psram_min,
             (unsigned)heap_caps_get_largest_free_block(psram),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

#if CONFIG_READER_MEMORY
bool reader_perf_memory_ready(void) { return !s_sample.active; }

void reader_perf_memory(const char *event, const reader_memory_stats_t *stats) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI("readerperf", "READER_MEMORY v=1 id=%u event=%s "
             "font_source=%u font_tables=%u font_blocks=%u font_glyphs=%u font_work=%u font_catalog=%u "
             "layout=%u chapter=%u prefetch=%u background=%u page_images=%u shelf_ui=%u "
             "font_slots=%u prefetch_worker=%u prefetch_stack_free=%u "
             "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u ui_stack_free=%u",
             s_sample.generation, event,
             (unsigned)stats->font_source, (unsigned)stats->font_tables,
             (unsigned)stats->font_blocks, (unsigned)stats->font_glyphs,
             (unsigned)stats->font_work, (unsigned)stats->font_catalog,
             (unsigned)stats->layout, (unsigned)stats->chapter, (unsigned)stats->prefetch,
             (unsigned)stats->background, (unsigned)stats->page_images, (unsigned)stats->shelf_ui,
             stats->font_slots, stats->prefetch_worker, stats->prefetch_stack_free,
             (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_free_size(psram),
             (unsigned)heap_caps_get_largest_free_block(psram),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
#endif
#endif

#if CONFIG_READER_PERF && CONFIG_READER_MEMORY
void reader_perf_source_memory(const char *event, const reader_source_memory_stats_t *stats) {
    ESP_LOGI("readerperf", "READER_SOURCE_MEMORY v=1 id=%u event=%s "
             "txt_index=%u epub_state=%u epub_index=%u epub_text=%u epub_faces=%u "
             "mobi_state=%u mobi_index=%u zip_state=%u zip_index=%u zip_names=%u zip_directory=%u open_files=%u",
             s_sample.generation, event, (unsigned)stats->txt_index,
             (unsigned)stats->epub_state, (unsigned)stats->epub_index,
             (unsigned)stats->epub_text, (unsigned)stats->epub_faces,
             (unsigned)stats->mobi_state, (unsigned)stats->mobi_index,
             (unsigned)stats->zip_state, (unsigned)stats->zip_index,
             (unsigned)stats->zip_names, (unsigned)stats->zip_directory, stats->open_files);
}
#endif
