/* SPDX-License-Identifier: Apache-2.0
 * 中文：正文单击翻页分区。全屏切换由阅读工具栏与按键负责，中央双击已取消，
 * 单击即翻页，不再有挂起等待。
 * English: Body tap turn regions only. Full screen toggling lives in the reader
 * toolbar and keys; center double taps are gone and every tap turns at once.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "epdiy.h"

/// rtl 表示竖排从右到左：横向分区的「上一页」在右半边，左右翻转。
/// rtl marks right-to-left vertical text: the previous-page half is the right one.
static inline int book_reader_tap_direction(EpdRect body, int x, int y, bool vertical, bool rtl) {
    if (x < body.x || x >= body.x + body.width || y < body.y || y >= body.y + body.height) return 0;
    if (vertical) return y < body.y + body.height / 3 ? -1 : 1;
    bool previous_half = rtl ? x >= body.x + body.width / 2 : x < body.x + body.width / 2;
    return previous_half ? -1 : 1;
}
