/*
 * 中文：阅读设置的同步重排事务；借用 UI 状态与资源交还入口，不持有章节或字体。
 * English: Synchronous reader-setting reflow transactions borrow UI state and resource handoff hooks without owning chapters or fonts.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "reader_position.h"
#include "book_font_size.h"

enum {
    READER_REFLOW_LEFT, ///< 左边距 / Left margin
    READER_REFLOW_RIGHT, ///< 右边距 / Right margin
    READER_REFLOW_TOP, ///< 上边距 / Top margin
    READER_REFLOW_BOTTOM, ///< 下边距 / Bottom margin
    READER_REFLOW_MARGINS, ///< 四向边距数量 / Number of margins
};
#define READER_REFLOW_MARGIN_MIN 0
#define READER_REFLOW_MARGIN_MAX 96

typedef struct {
    reader_position_t *position; ///< 同步借用当前阅读位置 / Synchronously borrowed reader position
    int *px; ///< UI 持有的正文字号 / UI-owned body size
    int *margins; ///< 按左、右、上、下排列的四边距 / Four margins ordered left, right, top, bottom
    int *line_spacing; ///< UI 当前行距 / Current UI line spacing
    void (*begin)(void); ///< 原顺序获取绘制锁并交还预绘 / Acquire draw lock and hand prefetch back in the original order
    void (*end)(void); ///< 按原顺序释放绘制锁 / Release the draw lock in the original order
    void (*refresh_lead)(void); ///< 重新计算页面题头预留 / Recompute page heading reservation
    bool (*build)(void); ///< 使用当前会话、参数和正文区域排版 / Build with the current session, parameters and body area
    void (*save)(void); ///< 成功并解锁后保存进度 / Save progress after success and unlock
} reader_reflow_t;

/// 指针与回调仅在同步调用内借用；调用方串行化 UI、章节和字体替换。
/// Pointers and hooks are borrowed only synchronously; callers serialize UI, chapter and font replacement.
/// 返回 true 表示新布局和设置已提交；失败尝试重建旧布局，沿用原有失败返回策略。
/// True means new layout/settings committed; failure attempts to rebuild the old layout and retains the original failure result.
bool reader_reflow_layout(const reader_reflow_t *reflow, int next_px,
                          const int next_margins[READER_REFLOW_MARGINS],
                          uint8_t next_line, uint8_t next_paragraph);
/// 调用方先确认有正文；字距仍使用设置层原有索引与编码。
/// Callers first confirm content exists; tracking retains the settings layer's existing index and encoding.
bool reader_reflow_typography(const reader_reflow_t *reflow, int tracking_index);
/// 调用方先确认有正文；缩进失败不写设置，也不隐式修正阅读位置。
/// Callers first confirm content exists; failed indentation writes no settings and never implicitly corrects position.
bool reader_reflow_indent(const reader_reflow_t *reflow, int em, int adjustment);

/// 调用方已校验正文和 kind（0状态栏、1隐藏图片、2竖排），并按原顺序获取锁、读取锚点、交还预绘。
/// Caller validates content and kind (0 status bar, 1 hidden images, 2 vertical), then locks, captures the anchor and hands prefetch back in that order.
/// 锁内写设置并重排；失败恢复旧设置和布局；调用方负责解锁、提示及成功后的图片与进度处理。
/// Write settings and reflow under the held lock; restore old settings/layout on failure. Caller unlocks, reports failure and handles images/progress on success.
bool reader_reflow_option_locked(const reader_reflow_t *reflow, int kind, bool previous, size_t offset);

typedef enum {
    READER_REFLOW_APPLIED, ///< 已提交新布局 / New layout committed
    READER_REFLOW_RESTORED, ///< 新布局失败，旧布局已恢复 / New layout failed and old layout restored
    READER_REFLOW_LOST_LAYOUT, ///< 新旧布局均失败 / Both new and old layout builds failed
} reader_reflow_result_t;

/// 调用方确认有正文；锁前读取锚点，锁内交还预绘并切换或恢复全屏布局。
/// Caller confirms content exists; capture the anchor before locking, then hand prefetch back and toggle or restore the full-screen layout under the lock.
/// 返回前已解锁；偏好、图片、进度和双失败后的退回书架由调用方处理。
/// Unlock before returning; caller handles preferences, images, progress and shelf fallback after both builds fail.
reader_reflow_result_t reader_reflow_fullscreen(const reader_reflow_t *reflow, bool *fullscreen);
