/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：异常启动暂用内建字体，避免自动重载故障资源；不覆盖用户字体设置。
 * User revision: recovery starts use the built-in face without retrying faulty resources or overwriting saved font choices.
 * 中文：系统默认使用内建思源黑体子集，TF 卡黑体补足缺字；用户选择的字体覆盖默认外观。
 * English: The built-in Source Han Sans face is the default; the SD face fills missing glyphs and explicit choices override it.
 * 用户修订：票根始终借用系统字体，浅睡解锁恢复锁屏前的实际字体和上下文，不覆盖保存的选择。
 * User revision: tickets always borrow the system face; light-sleep wake restores the actual previous face/context without altering saved choices.
 * 用户修订：已装载同路径字体时先跳过卡上 stat，瞬时读错误不再把阅读页换成内建字体。
 * User revision: an already-loaded same-path face skips the card stat, so a transient read error can no longer swap the reading page to the built-in face.
 * 用户修订：卡上读错不等于字体文件无效；读错只保留当前字体等下一个周期，失败路径也不整会话拉黑。
 * User revision: a card read error does not mean an invalid face; keep the live face for the next tick and never blacklist a path for the whole session.
 */
#include "app_font_context.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "boot_state.h"
#include "esp_timer.h"
#include "ttf_font.h"
#include "ui_kit.h"
#include "ui_nav.h"

static bool s_reading;
// Failed unchanged files are not repeatedly opened (opening unloads the active slot).
static char s_rejected[TTF_FONT_PATH_MAX];
static time_t s_rejected_mtime;
static off_t s_rejected_size;
static int64_t s_rejected_us;
// 拉黑也要过期：卡上读错可能让一次打开失败，半分钟后给文件再一次机会。
// A rejection expires: a card read error can fail one open, so give the file another chance later.
#define FONT_REJECT_RETRY_US (30 * 1000 * 1000)

// 卡上瞬时读错与「文件真的不在了」必须分开：前者不能把正在用的字体换掉。
// A transient card read error is not a missing file: it must never swap the live face.
static bool font_file(const char *path, struct stat *st, bool *transient) {
    if (transient) *transient = false;
    if (path == NULL) return false;
    if (stat(path, st) != 0) {
        if (transient) *transient = errno != ENOENT && errno != ENOTDIR;
        return false;
    }
    return S_ISREG(st->st_mode) && st->st_size > 0;
}

// 读不到卡时继续用已经装好的那张脸，而不是退回内建。
// Keep the face already loaded when the card cannot be read, instead of dropping to built-in.
static const char* live_face(void) {
    return ttf_font_ready() && !ttf_font_is_builtin() ? ttf_font_path() : TTF_FONT_BUILTIN;
}

static const char* system_path(char fallback[TTF_FONT_PATH_MAX]) {
    if (pico_boot_recovery()) return TTF_FONT_BUILTIN;
    const char* chosen = app_settings_system_font_path();
    if (chosen[0]) return chosen;
    if (snprintf(fallback, TTF_FONT_PATH_MAX, "%s/Hei.ttf", app_settings_fonts_dir()) >= TTF_FONT_PATH_MAX)
        return TTF_FONT_BUILTIN;
    struct stat st;
    bool transient = false;
    if (font_file(fallback, &st, &transient)) return fallback;
    if (transient) return live_face();
    if (!ttf_font_count()) ttf_font_scan();
    const ttf_font_item_t *item = ttf_font_item(0);
    if (item && font_file(item->path, &st, &transient)) return item->path;
    return transient ? live_face() : TTF_FONT_BUILTIN;
}

static bool activate(const char* path) {
    // 已装载同路径就不再碰卡：周期性重试每次 stat 都是一次 SD 读，读错的那个周期
    // 会把阅读页静默换成内建字体。字体未装载或要换目标时才做校验。
    // Skip the card when the same face is already loaded: every periodic retry does a stat,
    // and one failed read silently swapped the reading page to the built-in face. Validate
    // only when nothing is loaded or the target differs.
    if (ttf_font_ready() && !strcmp(ttf_font_path(), path)) return false;
    struct stat st = {0};
    bool builtin = ttf_font_path_is_builtin(path);
    bool transient = false;
    bool valid = builtin || font_file(path, &st, &transient);
    // 卡上读错：保留还在用的脸，下个周期再试，不要掉到内建。
    // A card read error keeps the live face and retries next tick instead of dropping to built-in.
    if (transient && ttf_font_ready()) return false;
    bool rejected = !builtin && !strcmp(s_rejected, path) &&
        s_rejected_mtime == st.st_mtime && s_rejected_size == st.st_size &&
        esp_timer_get_time() - s_rejected_us < FONT_REJECT_RETRY_US;
    if (!valid || rejected) path = TTF_FONT_BUILTIN;
    if (ttf_font_ready() && !strcmp(ttf_font_path(), path)) return false;
    char before[TTF_FONT_PATH_MAX];
    snprintf(before, sizeof(before), "%s", ttf_font_path());
    if (ttf_font_open(path) == ESP_OK) {
        if (!ttf_font_path_is_builtin(path)) s_rejected[0] = 0;
        return true;
    }
    // 打不开不等于文件坏了：卡上 I/O 失败不拉黑，已有一张能用的脸时也不要强行换内建。
    // A failed open does not prove a bad file: card I/O is not blacklisted, and a usable
    // face already loaded is kept instead of forcing the built-in one.
    if (!ttf_font_path_is_builtin(path) && !ttf_font_open_io_error()) {
        snprintf(s_rejected, sizeof(s_rejected), "%s", path);
        s_rejected_mtime = st.st_mtime;
        s_rejected_size = st.st_size;
        s_rejected_us = esp_timer_get_time();
    }
    if (ttf_font_ready()) return strcmp(before, ttf_font_path()) != 0;
    return ttf_font_open_builtin() == ESP_OK;
}

bool app_font_activate_system(void) {
    s_reading = false;
    ttf_set_stroke_adjust(0);
    ttf_set_antialias(true);
    const char* chosen = app_settings_system_font_path();
    char fallback[TTF_FONT_PATH_MAX];
    const char* path = system_path(fallback);
    bool changed = activate(path);
    // 未选字体时由内建思源黑体绘制界面；TTF 仍保留完整字库供文件名缺字回退。
    // Built-in Source Han Sans draws default UI; the full TTF remains loaded for missing glyphs.
    ui_text_set_system_font(!pico_boot_recovery() && chosen[0] && ttf_font_ready() &&
                           !strcmp(chosen, ttf_font_path()) && !ttf_font_is_builtin());
    ui_text_set_system_scale(true);
    ui_nav_status_font_context(false, changed);
    return changed;
}

bool app_font_activate_reading(void) {
    s_reading = true;
    ttf_set_stroke_adjust_fine(app_settings_book_weight_fine());
    ttf_set_antialias(app_settings_book_antialias());
    const char* saved = pico_boot_recovery() ? TTF_FONT_BUILTIN : app_settings_font_path();
    bool changed = activate(ttf_font_path_is_builtin(saved) ? TTF_FONT_BUILTIN : saved);
    ui_text_set_system_font(false);
    ui_text_set_system_scale(false);
    ui_nav_status_font_context(true, changed);
    return changed;
}

bool app_font_retry_active(void) {
    return s_reading ? app_font_activate_reading() : app_font_activate_system();
}

void app_font_begin_lock(app_lock_font_t *saved) {
    if (!saved) return;
    snprintf(saved->path, sizeof(saved->path), "%s", ttf_font_path());
    saved->reading = s_reading;
    app_font_activate_system();
}

void app_font_end_lock(const app_lock_font_t *saved) {
    if (!saved) return;
    (void)activate(saved->path);
    s_reading = saved->reading;
    ttf_set_stroke_adjust_fine(s_reading ? app_settings_book_weight_fine() : 0);
    ttf_set_antialias(!s_reading || app_settings_book_antialias());
    const char *chosen = app_settings_system_font_path();
    ui_text_set_system_font(!s_reading && !pico_boot_recovery() && chosen[0] &&
                           !strcmp(chosen, ttf_font_path()) && !ttf_font_is_builtin());
    ui_text_set_system_scale(!s_reading);
    ui_nav_status_font_context(s_reading, false);
}
