/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：文件管理与首页请求内容页打开指定文件。
 * English: File manager and home request a specific file in the content pages.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

bool app_book_request_open(const char *path);
bool app_book_request_open_from_home(const char *path);
/// 一次性深睡恢复；保持保存的正文位置与全屏状态。/ One-shot deep-sleep resume with saved position and fullscreen state.
bool app_book_request_resume(const char *path, bool fullscreen);
/// 从其他页面回到正文：按离开时的书与全屏状态重开，返回来源仍是书架。
/// Return to the reading body from another page: reopen the same book and
/// full-screen state, keeping the shelf as the reader's back target.
bool app_book_request_return_to_body(const char *path, bool fullscreen);
/// 仅导出已打开阅读上下文。/ Export only an already open reader context.
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen);
bool app_image_request_open(const char *path);
void app_files_request_folder(int folder);
void app_book_request_manage(void);
bool app_book_reader_body_visible(void);
/// 模式/对比度变化时释放旧格式封面；绘制前由主循环一次调用。/ Drop obsolete cover formats once on mode/contrast changes, before drawing.
void app_book_cover_mode_changed(void);
void app_home_cover_mode_changed(void);
