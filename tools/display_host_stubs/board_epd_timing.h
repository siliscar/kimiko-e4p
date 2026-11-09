/* SPDX-License-Identifier: Apache-2.0
 * 扫描时序替身。/ Scan timing shim.
 */
#pragma once
#define BOARD_EPD_PCLK_MIN_MHZ 12
#define BOARD_EPD_PCLK_MAX_MHZ 24
typedef enum { BOARD_EPD_SCAN_FULL, BOARD_EPD_SCAN_FAST } board_epd_scan_t;
void board_epd_set_pclk(int mhz);
void board_epd_use_scan(board_epd_scan_t scan);
