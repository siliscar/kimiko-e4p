/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：从阅读页面提取同步刷新路由；保留原显示出口与水波纹节拍恢复，局部翻页优先输出正文。
 * English: Synchronous output routing extracted from the book page, retaining display exits and water pacing restoration; partial turns present the body first.
 */
#include "reader_present.h"
#include "display.h"
#include "display_refresh.h"

reader_present_result_t reader_present_output(EpdiyHighlevelState *hl,
                                               const reader_present_request_t *request) {
    reader_present_result_t result = {0};
    enum EpdDrawError err;
    bool held = display_present_begin();
    // 含插图的一帧不用背景派生表：那张表把深色对的前导擦白剥掉了，旧字（from=0）落到图片灰
    // 时从黑轨直接进灰尾，落点和纸灰起点不一致，差分会把上一页的字形留在图上。纯文字帧仍走
    // 背景派生表。插图表还保留相等对保持，脏区膨胀带上的未变化像素不会被黑白往返推出一圈细线。
    // Frames holding an illustration skip the background derivative: it strips the dark pairs'
    // leading erase, so old glyphs (from=0) settle on an image gray from the black rail instead
    // of the white rail, land differently from paper-origin pixels and keep their shape. Text
    // frames keep the derivative. The illustration table also holds equal pairs, so the
    // unchanged pixels that dilation brings along are not pushed through a white-black trip
    // that draws a thin outline.
    const unsigned frame_flags = request->image_page ? DISPLAY_REFRESH_IMAGE : DISPLAY_REFRESH_DEFAULT;
    if (request->image_gray_refresh && request->refresh == READER_PRESENT_AREA && !request->cleanup && request->prior_reader) {
        // 灰阶只驱动正文，避免每张插图都把整屏先清白再闪回。
        // Drive the grayscale body from the current frame instead of clearing the whole screen to white.
        err = request->water_effect
            ? display_refresh_area(hl, DISPLAY_REFRESH_READER_BODY, request->body_area, frame_flags)
            : display_refresh_area(hl, DISPLAY_REFRESH_READER_IMAGE, request->body_area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->image_gray_refresh) err = request->water_effect &&
        request->refresh != READER_PRESENT_FULL && !request->cleanup
        // 换章那一帧走整屏差分，表的选择与正文带差分一致。
        // The chapter-change frame diffs the whole screen and takes the same table.
        ? display_refresh_screen(hl, DISPLAY_REFRESH_READER_BODY, frame_flags)
        : display_refresh_screen(hl, DISPLAY_REFRESH_READER_IMAGE, DISPLAY_REFRESH_DEFAULT);
    else if (request->cleanup && request->refresh != READER_PRESENT_FULL)
        // 周期清理使用完整原厂灰阶定稿，保留饱和驱动；一次输出，不叠加擦白或文字补刷。
        // Periodic cleanup retains the complete vendor settling/saturation phases in one output, without erase or text replay.
        err = display_refresh_area(hl, DISPLAY_REFRESH_READER_CLEANUP, request->screen_area, DISPLAY_REFRESH_DEFAULT);
    else if (request->refresh == READER_PRESENT_FULL) err = update_display_full(hl);
    // 工具栏开关只改底部一条带：GL16 差分只驱动带内变化像素，脏区膨胀补推进度条边缘的
    // 白色邻行/邻列，实心进度条擦除后不再留轮廓。收起帧例外：被工具栏盖过的字多走了
    // 一轮擦白推黑，带内差分追不平驱动历史，纯文字页放行到下方的整屏归一分支。
    // A tools toggle changes only the bottom band: a GL16 differential drives the changed
    // pixels inside it, and dirty-mask dilation re-drives the white neighbours around the
    // solid progress bar so erasing it leaves no outline. The closing frame is the
    // exception: the covered glyphs ran one extra erase-then-black cycle that a band
    // differential cannot match, so pure-text pages fall through to the whole-screen
    // normalize branch below.
    else if (request->tools_toggle && request->refresh == READER_PRESENT_PAGE && request->reader_body &&
             request->prior_reader &&
             !(request->closing_tools_to_text)) {
        result.tools_band = true;
        err = display_refresh_area(hl, DISPLAY_REFRESH_READER_BODY, request->tools_area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->refresh == READER_PRESENT_AREA && request->feedback) {
        // 抽出与复位只驱动发生变化的像素，避免整张封面被反复压黑。
        // Lift and restore only changed pixels, avoiding a dark pulse over the whole cover.
        err = display_refresh_area(hl, request->mode == MODE_DU ? DISPLAY_REFRESH_FEEDBACK : DISPLAY_REFRESH_DETAIL, request->area, DISPLAY_REFRESH_DEFAULT);
    }
    else if (request->refresh == READER_PRESENT_AREA) {
        // 快档每次显式设置并在推屏后恢复，不能把阅读节拍带入主页水波纹。
        // Explicitly select fast pacing per turn and restore afterward, without leaking reader timing into main-page ripple.
        const int prior_tick = request->water_turn_pending ? e0470_page_turn_tick_us() : 0;
        if (request->water_turn_pending) e0470_page_turn_set_tick_us(E0470_TURN_FAST_TICK_US);
        // 翻页动画与页脚刷新独立：全屏没有页脚，仍使用用户选择的水波纹。
        // The turn effect is independent of the footer: full-screen turns still use the selected water effect.
        err = request->water_turn_pending
            ? update_display_water_turn(hl, request->area, request->water_dir)
            // 普通翻页与全屏阅读都只驱动变化像素；设置面板等局部绘制保留原路径。
            // Ordinary turns and full-screen reading drive changed pixels only; other local UI keeps its path.
            : (request->fullscreen || request->turn_pending) && request->mode == MODE_GL16
                ? display_refresh_area(hl, request->turn_pending ? DISPLAY_REFRESH_READER_TEXT : DISPLAY_REFRESH_READER_BODY, request->area, DISPLAY_REFRESH_DEFAULT)
                : display_refresh_area(hl, DISPLAY_REFRESH_READER_LAYOUT, request->area, request->mode == MODE_DU ? DISPLAY_REFRESH_QUICK : DISPLAY_REFRESH_DEFAULT);
        if (request->water_turn_pending) e0470_page_turn_set_tick_us(prior_tick);

    }
    // 关闭阅读设置等面板时，被白底盖过的字已经多走了一轮「擦白 → 推黑」，比没被盖过的
    // 字更黑；单次还原追不平。这里整屏再走一遍归一表，全页字芯落在同一墨度。
    // Closing a sheet leaves the text it covered one erase-then-black cycle ahead of the rest,
    // which no single restore can match. One whole-screen normalize pass puts every glyph core
    // on the same ink.
    else if (request->refresh == READER_PRESENT_PAGE && request->reader_body &&
             request->closing_panel_to_text) {
        err = display_refresh_area(hl, DISPLAY_REFRESH_RESTORE, request->screen_area, request->hold_gray ? DISPLAY_REFRESH_HOLD_GRAY : DISPLAY_REFRESH_DEFAULT);
    }
    else if (!request->reading && request->prior_reader && request->refresh == READER_PRESENT_PAGE)
        // 同页退出正文回书架也全刷，清掉阅读文字残影。
        // Fully refresh body-to-shelf returns within this page to clear reading-text residue.
        err = update_display_full(hl);
    else err = request->reading
        ? display_refresh_screen(hl, DISPLAY_REFRESH_READER_BODY, frame_flags |
            (request->page_mode == MODE_DU ? DISPLAY_REFRESH_QUICK : DISPLAY_REFRESH_DEFAULT))
        : update_display_fast_page(hl);
    // 正文成功后再更新页脚、进度和顶部；任一步失败都保留错误并停止后续输出。
    // Update footer, progress and status after a successful body; preserve errors and stop subsequent output on failure.
    if (err == EPD_DRAW_SUCCESS && request->refresh == READER_PRESENT_AREA &&
        request->reader_body && request->prior_reader && !request->cleanup && !request->feedback) {
        if (request->footer_pending) {
            // 恢复旧页脚：普通翻页走短 DU，换章与插图帧走默认 GL16 清理。
            // Restore the legacy footer: short DU for ordinary turns, default GL16 for chapters and illustration frames.
            err = display_refresh_area(hl, request->footer_strong || request->image_gray_refresh
                ? DISPLAY_REFRESH_DETAIL : DISPLAY_REFRESH_READER_FOOTER, request->footer_area, DISPLAY_REFRESH_DEFAULT);
            if (err != EPD_DRAW_SUCCESS) goto done;
            result.footer_updated = true;
        }
        if (request->fullscreen) {
            err = display_refresh_area(hl, DISPLAY_REFRESH_DETAIL, request->progress_area, DISPLAY_REFRESH_DEFAULT);
            if (err != EPD_DRAW_SUCCESS) goto done;
        }
        if (request->status_pending) {
            err = display_refresh_area(hl, DISPLAY_REFRESH_DETAIL, request->status_area, DISPLAY_REFRESH_DEFAULT);
            if (err != EPD_DRAW_SUCCESS) goto done;
            result.status_updated = true;
        }
    }
done:
    display_present_end(held);
    result.error = err;
    return result;
}
