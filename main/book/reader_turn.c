/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从阅读页面提取目标页选择和刷新许可，保留跨章保存及加载顺序。
 * English: Target selection and refresh eligibility extracted from the book page, retaining cross-chapter save/load order.
 */
#include "reader_turn.h"

reader_turn_result_t reader_turn_move(reader_position_t *position, int direction,
                                      size_t pages, size_t chapters,
                                      const reader_turn_ops_t *ops) {
    if (direction > 0 && position->page + 1 < pages) ++position->page;
    else if (direction < 0 && position->page) --position->page;
    else if (direction > 0 && position->chapter + 1 < chapters) {
        ops->save(ops->context);
        return ops->load(ops->context, position->chapter + 1, false)
            ? READER_TURN_MOVED : READER_TURN_LOAD_FAILED;
    } else if (direction < 0 && position->chapter) {
        ops->save(ops->context);
        return ops->load(ops->context, position->chapter - 1, true)
            ? READER_TURN_MOVED : READER_TURN_LOAD_FAILED;
    } else return READER_TURN_EDGE;
    return READER_TURN_MOVED;
}

reader_turn_effects_t reader_turn_effects(const reader_turn_frame_t *frame) {
    return (reader_turn_effects_t){
        .text = !frame->chrome_changed && frame->area_redraw && frame->from_text &&
                !frame->images && !frame->background,
        .water = !frame->chrome_changed && frame->area_redraw && !frame->cleanup &&
                 frame->water_effect && frame->from_stable && !frame->images,
        .footer_strong = !frame->chrome_changed && !frame->cleanup && frame->chapter_changed,
    };
}
