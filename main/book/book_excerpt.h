/* SPDX-License-Identifier: Apache-2.0
 * 中文：只从确认显示的正文缓存短句，不开书或改进度。/ English: Cache sentences only from confirmed body frames without opening books or changing progress.
 */
#pragma once
#include "html_text.h"
#define BOOK_EXCERPT_CAP 240
#define BOOK_EXCERPT_INTERVAL 4u
/// 首页读取最新书摘，兼容旧缓存；没有可用句子时清空输出。/ Read the latest or legacy excerpt, clearing output when absent.
bool book_excerpt_load(const char *path, char out[BOOK_EXCERPT_CAP]);
/// 开书成功后读取一次状态并重置连续显示区间；仅支持 TF 书籍。/ Load state after a successful open and reset contiguous visibility; TF books only.
void book_excerpt_begin(const char *path);
/// 成功跳转或倒读后重置间隔起点，不摘句；仅操作内存并保留旧句与历史。
/// Restart cadence at a successful jump/reverse destination without selecting, IO or losing the quote/history.
void book_excerpt_reposition(size_t chapter, size_t begin, size_t end);
/// 预绘交还且正文推屏成功后调用；[begin,end) 必须完全显示，forward 只标记向前翻页。
/// Call after successful body output and prefetch handoff; [begin,end) must be displayed, and forward marks only a forward turn.
/// 只操作内存，重复画面不计页；不借用正文到返回之后。/ Memory only; redraws never count, and text is not retained after return.
void book_excerpt_observe(const html_text_t *text, size_t chapter, size_t begin, size_t end, bool forward);
/// 随现有进度保存写入另一份缓存，失败保留旧副本与待保存状态。/ Save to the alternate cache with progress; retain the old copy and dirty state on failure.
bool book_excerpt_flush(const char *path);
/// 介质丢失时丢弃本机借用与待保存状态，不访问存储。/ Discard local state without storage access on media loss.
void book_excerpt_reset(void);
