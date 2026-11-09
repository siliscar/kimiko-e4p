/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从主循环提取介质检测、文件消费者失效通知和字体恢复；调度与重绘仍由主循环负责。
 * English: Media detection, file-consumer invalidation and font recovery extracted from the loop; scheduling and repainting remain in the loop.
 */
#include "app_media.h"
#include "board_sd.h"
#include "app_font_context.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_avatar.h"
#include "book/book_cover_jobs.h"
#include "book/book_lock_collage.h"
#include "storage_jobs.h"
#include "usb_storage.h"

// 失效锁存后卡回位的自动重挂间隔；重挂本身失败时下次再试。
// Auto-remount interval once a latched card is back; a failed remount retries next time.
#define MEDIA_REMOUNT_INTERVAL_MS 2000

bool app_media_initial_mounted(void) {
    board_sd_info_t initial_media = {0};
    (void)board_sd_get_info(&initial_media);
    return initial_media.mounted || (ttf_font_ready() && !ttf_font_is_builtin());
}

bool app_media_retry_font(bool allow_probe) {
    board_sd_info_t info;
    esp_err_t err = board_sd_get_info(&info);
    if (err == ESP_ERR_NOT_FINISHED) return false;
    if (!info.mounted) {
        if (allow_probe && err == ESP_ERR_INVALID_STATE) board_sd_start_probe();
        return false;
    }
    return app_font_retry_active();
}

// 先停文件使用者，再换字体；重挂只发生在失效后卡已回位、句柄都已放开时。
// Stop file consumers before font fallback; remount only after a latched card is back and
// every consumer has released its handles.
static int64_t s_media_remount_ms;

bool app_media_poll(app_ctx_t* ctx, const app_desc_t* current,
                       bool* mounted, bool* invalidated) {
    if (usb_storage_active()) { *mounted = false; *invalidated = false; return false; }
    board_sd_info_t info = {0};
    esp_err_t err = board_sd_get_info(&info);
    // 判失效后卡又回到位：消费者已在 lost 里关掉句柄，重挂一次让字体和目录自己回来。
    // After the latch, a card back in place can be remounted: consumers closed their handles
    // during the lost notification, so the face and the listings recover by themselves.
    if (*invalidated && info.present && !info.mounted && err != ESP_ERR_NOT_FINISHED &&
        ctx->now_ms - s_media_remount_ms >= MEDIA_REMOUNT_INTERVAL_MS) {
        s_media_remount_ms = ctx->now_ms;
        // 取消通知后仍可能有旧文件操作；任务交还前保持失效锁存，下轮再试。
        // Cancellation may leave old file I/O running; retain invalidation and retry after handoff.
        if (storage_jobs_quiesce(0)) (void)board_sd_remount();
        return false;
    }
    if (err == ESP_ERR_INVALID_STATE && info.present && !*invalidated) {
        (void)board_sd_start_probe();
        return false;
    }
    if (err == ESP_ERR_NOT_FINISHED) return false;
    bool ready = err == ESP_OK && info.mounted;
    bool lost = *mounted && !ready;
    bool gained = !*mounted && ready;
    *mounted = ready;
    if (ready) *invalidated = false;
    if (gained || lost) { ui_avatar_invalidate(); (void)book_cover_jobs_quiesce(0); book_lock_collage_invalidate(); }
    if (gained && current->on_media_ready) current->on_media_ready(ctx);
    if (!lost) return false;
    *invalidated = true;
    if (current->on_media_lost) current->on_media_lost(ctx);
    if (ttf_font_ready() && !ttf_font_is_builtin()) ttf_font_open_builtin();
    ui_text_set_system_font(false);
    return true;
}
