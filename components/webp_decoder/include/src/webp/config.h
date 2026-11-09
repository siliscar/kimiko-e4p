/* SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：可移植 C 解码；互斥保护共享初始化，调用方不启用解码工作线程。
 * English: Portable C decoding with synchronized initialization and no decoding worker threads.
 */
#pragma once
#define HAVE_STDINT_H 1
#define WEBP_USE_THREAD 1
