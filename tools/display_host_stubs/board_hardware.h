/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 板级中性状态边界替身。/ Board neutral-drive boundary shim.
 */
#pragma once
#include <stdbool.h>
bool board_epd_release_drive(void);
bool board_rails_on(void);
