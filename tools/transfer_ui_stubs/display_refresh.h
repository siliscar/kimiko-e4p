/*
 * SPDX-FileCopyrightText: 2026 chopper1026
 * SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 配网测试的显示出口，不访问面板。/ Provisioning-test display boundary without panel access.
 */
#pragma once
#include "transfer_ui_test_env.h"
enum { DISPLAY_REFRESH_DETAIL, DISPLAY_REFRESH_LAYOUT, DISPLAY_REFRESH_SETTLE,
       DISPLAY_REFRESH_FEEDBACK, DISPLAY_REFRESH_MOTION, DISPLAY_REFRESH_DEFAULT };
static inline int display_refresh_area(void *hl, int intent, EpdRect area, unsigned flags) {
    (void)hl; (void)intent; (void)area; (void)flags; return 0;
}
