/* SPDX-License-Identifier: Apache-2.0
 * 中文：显示出口测试的前光替身，只提供决定高压轨保持时长的那一个查询。
 * English: Frontlight shim for the display-output tests; only the query that decides the HV hold.
 */
#pragma once
#include <stdbool.h>

bool frontlight_on(void);
