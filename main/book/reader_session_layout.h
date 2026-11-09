/*
 * 中文：阅读会话对单实例排版引擎的串行访问；不修改分页算法和页面参数。
 * English: Session-owned serial access to the singleton layout engine; no pagination or page-policy changes.
 */
#pragma once
#include "reader_session.h"
#include "epdiy.h"

/// 预绘已交还且持有绘制锁时，为拥有书源的会话构建候选章或当前章布局。
/// After prefetch returns under the draw lock, build a candidate or current chapter for the source owner.
/// 借用章的文本、块、字体段及内链；失败仍按原引擎清空布局，调用方负责回滚。
/// Borrow chapter text, blocks, runs and links; failure clears layout as before and the caller rolls back.
/// 成功候选通过 adopt_chapter 接收；失败候选仍由调用方释放，恢复完成前不可绘制。
/// Transfer successful candidates with adopt_chapter; callers free failed candidates and must restore before drawing.
bool reader_session_build_layout(reader_session_t *session, const html_text_t *chapter,
                                 EpdRect rect, int px);
