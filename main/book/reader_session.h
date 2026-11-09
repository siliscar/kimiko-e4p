/*
 * 中文：单 UI 会话的书籍信息、书源、章节内容、布局归属与当前位置；页面决定排版参数和预绘许可。
 * English: Book metadata, source, chapter, layout ownership and position for one UI session; the page selects layout parameters and prefetch eligibility.
 */
#pragma once
#include <stdbool.h>
#include "html_text.h"
#include "reader_position.h"
#include "reader_book.h"

typedef struct {
    reader_book_t book; ///< UI 在书源打开后填写，关闭后路径与书名失效。/ UI fills metadata after source open; close invalidates path and book title.
    html_text_t content; ///< 会话独占的章节；仅通过下面的接口替换。/ Session-owned chapter; replace only through this API.
    reader_position_t position; ///< UI 在加载或重排成功后提交；章节交接不隐式修改。/ Committed by the UI after load or reflow succeeds; chapter transfer never changes it implicitly.
    bool layout_owned; ///< 仅布局接口修改；包括失败构建后需要解除的借用。/ Only layout APIs modify this, including borrows left by a failed build.
} reader_session_t;

/// 静态或栈上会话用零初始化；单实例书源同时只允许一个会话拥有。
/// Zero-initialize static or stack sessions; only one may own the singleton source at a time.
/// 已有书源时返回 INVALID_STATE，保持旧会话；不会隐式释放旧章节。
/// An existing source returns INVALID_STATE, preserving its session and chapter.
esp_err_t reader_session_open(reader_session_t *session, const char *path);
/// 当前会话是否拥有书源。/ Whether this session owns the source.
bool reader_session_has_source(const reader_session_t *session);
/// 借用章节视图，不转移所有权；替换或释放后失效。
/// Borrow a chapter view without ownership; replacement or release invalidates it.
const html_text_t *reader_session_content(const reader_session_t *session);
/// 新布局已成功绑定 next 且旧预绘已交还时，释放旧章并接收 next，随后清空 next。
/// After the new layout binds next and old prefetch returns, free the old chapter, take next and clear next.
/// 不执行解析、排版、加锁或推屏；失败候选章仍由调用方释放，不能传入这里。
/// No parsing, layout, locking or presentation; callers free failed candidates instead of adopting them.
void reader_session_adopt_chapter(reader_session_t *session, html_text_t *next);
/// 预绘已交还且无其他绘制者时，解除本会话的布局借用；不会释放其他会话的布局。
/// After prefetch returns with no other drawing borrower, detach this session's layout, never another session's layout.
void reader_session_release_layout(reader_session_t *session);
/// 预绘已交还且无其他绘制者时，先解除布局借用再释放章节；不关闭书源，可重复调用。
/// After prefetch returns with no other drawing borrower, detach layout before freeing the chapter; retain the source and allow repeated calls.
void reader_session_release_chapter(reader_session_t *session);
/// 以同样的交还前提释放章节并关闭本会话的书源；不会关闭其他会话的书源。
/// With the same handoff preconditions, release the chapter and this session's source, never another session's source.
/// 最后清空本会话的路径与书名；位置和章节缓冲字段沿用原页面的覆盖时机。
/// Finally clear this session's path/title; position and chapter scratch fields retain the page's overwrite timing.
void reader_session_close(reader_session_t *session);
