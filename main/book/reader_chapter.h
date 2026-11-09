/*
 * 中文：同步章节候选与布局交接事务；不持有页面、字体、绘制锁或导航状态。
 * English: Synchronous chapter candidates and layout handoff transactions; no page, font, draw-lock or navigation ownership.
 */
#pragma once
#include "reader_session_layout.h"

typedef struct {
    html_text_t content; ///< 候选章独占资源；成功交接或失败布局后清空。/ Candidate-owned resources, cleared after adoption or failed layout.
    char heading[128]; ///< 题头正文 / Heading title
    char label[32]; ///< 题头编号 / Heading label
    size_t lead_skip; ///< 正文中折叠题头的结束偏移 / End offset of folded body headings
    unsigned lead_height; ///< 页面提供的横排题头带高；竖排由页面测量。/ Page-provided horizontal heading band; the page measures vertical geometry.
    size_t anchor_offset; ///< 书源返回的锚点或源偏移映射 / Source-resolved anchor or source offset
} reader_chapter_t;

typedef enum {
    READER_CHAPTER_APPLIED, ///< 新章已交接给会话 / New chapter adopted by the session
    READER_CHAPTER_RESTORED, ///< 候选失败，旧布局已恢复 / Candidate failed and old layout restored
    READER_CHAPTER_LOST_LAYOUT, ///< 新旧布局均不可用 / Both layouts unavailable
} reader_chapter_result_t;

typedef struct {
    void *ctx; ///< 仅本次同步调用借用的旧布局适配状态 / Old layout adapter state borrowed for this synchronous call only
    void (*restore_images)(void *ctx); ///< 释放候选前还原尺寸回调上下文 / Restore image-dimension context before candidate release
    void (*restore_lead)(void *ctx); ///< 释放候选后还原题头几何 / Restore heading geometry after candidate release
} reader_chapter_rollback_t;

/// out 必须零初始化且未持有候选；读取失败沿用书源的清理契约，不修改会话或布局。
/// out must be zero-initialized with no candidate; failed reads retain source cleanup semantics and never change session/layout.
/// 调用方保持书源和字体稳定；读取解析仍发生在获取绘制锁与交还预绘之前。
/// Callers keep source/fonts stable; reading/parsing still precedes draw locking and prefetch handoff.
esp_err_t reader_chapter_read(reader_chapter_t *out, size_t chapter, const char *anchor,
                             size_t source_offset, int body_height, unsigned heading_height);

/// 调用方已持锁、交还预绘，并应用候选题头与图片上下文；rollback 两回调必须有效。
/// Caller holds the draw lock, has joined prefetch and applied candidate heading/image context; both rollback hooks are required.
/// 失败按图片上下文、候选释放、题头几何、旧布局的顺序恢复；无旧正文时返回 LOST_LAYOUT。
/// Failure restores image context, frees the candidate, restores heading geometry, then rebuilds old layout; no old text returns LOST_LAYOUT.
/// 成功先提供摘录再交接章节；位置、页面借用、图片缓存和解锁仍由调用方按原顺序处理。
/// Success offers the excerpt then adopts the chapter; callers retain ordered position, page borrows, image-cache and unlock handling.
reader_chapter_result_t reader_chapter_apply_locked(reader_session_t *session,
                                                   reader_chapter_t *candidate,
                                                   EpdRect rect, int px,
                                                   const reader_chapter_rollback_t *rollback);
