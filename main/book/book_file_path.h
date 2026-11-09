/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：本地阅读器支持的文件格式；持久历史不是支持书源的依据。
 * English: Local reader file formats; persistent history never implies source support.
 */
#pragma once
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/// 仅检查完整文件名的TXT/EPUB/MOBI扩展名，真实格式由后端校验。
/// Check TXT/EPUB/MOBI extensions only; each backend validates the actual format.
static inline bool book_file_path_supported(const char *path) {
    if (!path || !*path) return false;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *ext = strrchr(name, '.');
    return ext && (!strcasecmp(ext, ".txt") || !strcasecmp(ext, ".epub") || !strcasecmp(ext, ".mobi"));
}
