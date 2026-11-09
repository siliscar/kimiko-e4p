/*
 * 中文：单 UI 会话的同步开书与恢复控制；只借用进度队列及页面资源适配，不绘制或导航。
 * English: Synchronous opening and resume control for one UI session; borrow progress and page resource adapters without drawing or navigation.
 */
#pragma once
#include "reader_session.h"
#include "book_progress.h"

typedef struct {
    reader_session_t *session; ///< 同步借用书源与书籍信息 / Synchronously borrowed source and book metadata
    int *px; ///< 页面正文字号，在选好进度后按全局设置更新 / Page body size, updated from global settings after progress selection
    bool (*reserve)(const char *path); ///< 幂等预留待保存进度；旧书交还前后各一次 / Idempotent pending-progress reservation, once before and once after old-book handoff
    void (*replace)(void); ///< 保存旧进度、释放旧书，再应用全屏偏好并激活阅读字体 / Save old progress, release the old book, then apply full-screen preference and activate reading fonts
    void (*describe)(const char *path); ///< 仅书源成功后应用书内字体并填写路径和书名 / Apply embedded fonts and fill path/title only after source success
    bool (*pending_restore)(const char *path, uint32_t size, book_progress_t *out); ///< 优先恢复匹配尺寸的脏进度 / Prefer dirty progress with a matching file size
    bool (*load)(void *ctx, size_t chapter, size_t offset); ///< 加载目标并跳过隐藏图页；保留页面提示及锁顺序 / Load the target and skip hidden image pages, retaining page notices and lock order
    void (*close)(void); ///< 正文恢复失败后交还书源、布局、图片和字体；允许重复退出 / Release source, layout, images and fonts after content restore failure; allow repeated exit
    void *load_ctx; ///< 仅在同步正文加载期间借用 / Borrowed only during synchronous content loading
} reader_open_t;

typedef enum {
    READER_OPEN_READY, ///< 正文已恢复，调用方可提交阅读 UI / Content restored; caller may commit the reading UI
    READER_OPEN_RESERVE_OLD, ///< 首次预留失败，保留旧书 / First reservation failed; retain the old book
    READER_OPEN_RESERVE_NEW, ///< 旧书已交还，再次预留失败 / Old book handed back; second reservation failed
    READER_OPEN_SOURCE_FAILED, ///< 书源失败，调用方清理干净预留、提示并恢复系统字体 / Source failed; caller discards a clean reservation, reports and restores system fonts
    READER_OPEN_CONTENT_FAILED, ///< 正文恢复失败，已调用 close / Content restore failed; close has run
} reader_open_result_t;

/// 调用方已校验路径与文件尺寸并交还可重建缓存；所有指针和回调必须有效，仅同步借用。
/// Caller has validated path/size and handed back rebuildable caches; all pointers/hooks must be valid and are borrowed synchronously.
/// 两次预留不可合并：同书重开时 replace 可能移除干净预留；失败不会回滚到旧书。
/// Never merge the two reservations: replace may remove a clean reservation during same-book reopen; failure never reopens the old book.
/// source_error 仅在 SOURCE_FAILED 时有效；提示、导航及成功后的进度保存仍归调用方。
/// source_error is valid only for SOURCE_FAILED; caller retains notices, navigation and post-success progress saving.
reader_open_result_t reader_open_run(const reader_open_t *open, const char *path,
                                     uint32_t file_size, esp_err_t *source_error);
