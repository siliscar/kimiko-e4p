/*
 * 中文：控制旧书交还、书源打开、进度选择和正文恢复，保留原失败边界及执行顺序。
 * English: Control old-book handoff, source opening, progress selection and content restore while retaining failure boundaries and execution order.
 */
#include "reader_open.h"
#include "book_source.h"
#include "book_font_size.h"
#include "settings.h"

reader_open_result_t reader_open_run(const reader_open_t *open, const char *path,
                                     uint32_t file_size, esp_err_t *source_error) {
    if (!open->reserve(path)) return READER_OPEN_RESERVE_OLD;
    open->replace();
    if (!open->reserve(path)) return READER_OPEN_RESERVE_NEW;
    esp_err_t error = reader_session_open(open->session, path);
    if (error != ESP_OK) {
        *source_error = error;
        return READER_OPEN_SOURCE_FAILED;
    }
    open->describe(path);
    open->session->book.file_size = file_size;
    book_progress_t progress = {0};
    bool resume = open->pending_restore(path, file_size, &progress) ||
                  book_progress_load(path, file_size, &progress);
    // 旧进度只决定位置，字号沿用当前全局阅读设置。
    // Old progress selects position only; size follows the current global reading setting.
    *open->px = app_settings_book_px();
    if (*open->px < BOOK_READER_PX_MIN || *open->px > BOOK_READER_PX_MAX) *open->px = 48;
    size_t chapter = resume && progress.chapter < book_chapter_count() ? progress.chapter : 0;
    size_t offset = resume && progress.chapter == chapter ? progress.byte_off : 0;
    if (!open->load(open->load_ctx, chapter, offset)) {
        open->close();
        return READER_OPEN_CONTENT_FAILED;
    }
    return READER_OPEN_READY;
}
