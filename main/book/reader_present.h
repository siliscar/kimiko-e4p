/*
 * 中文：同步呈现请求；借用已画好的前后帧，调用现有显示出口，不管理字体、布局或预绘。
 * English: Synchronous presentation requests borrow completed frames and use existing display exits; no font, layout or prefetch ownership.
 */
#pragma once
#include <stdbool.h>
#include "epd_highlevel.h"
#include "e0470_page_turn.h"

typedef enum {
    READER_PRESENT_AREA, ///< 局部更新 / Area update
    READER_PRESENT_PAGE, ///< 普通整页 / Ordinary page
    READER_PRESENT_FULL, ///< 强制全刷 / Forced full update
} reader_present_kind_t;

typedef struct {
    reader_present_kind_t refresh; ///< 已完成绘制的请求类型 / Kind of completed frame
    enum EpdDrawMode mode; ///< 局部刷新模式 / Area mode
    enum EpdDrawMode page_mode; ///< 普通整页模式 / Ordinary page mode
    EpdRect area; ///< 页面给出的局部区域 / Requested area
    EpdRect body_area; ///< 阅读正文带 / Reader body band
    EpdRect status_area; ///< 随翻页更新的顶部带 / Top band refreshed with page turns
    EpdRect footer_area; ///< 窗口化页脚带 / Windowed footer band
    EpdRect progress_area; ///< 全屏进度带 / Full-screen progress band
    EpdRect tools_area; ///< 阅读工具栏覆盖带 / Reader tools band
    EpdRect screen_area; ///< 完整逻辑屏幕 / Complete logical screen
    e0470_turn_dir_t water_dir; ///< 水波纹方向 / Water-turn direction
    bool reading; ///< 当前为阅读视图 / Current view is the reader
    bool reader_body; ///< 当前有阅读正文 / Current view has reader content
    bool prior_reader; ///< 前帧是阅读视图 / Previous frame was the reader
    bool image_page; ///< 目标页含插图槽 / Target has illustration slots
    bool image_gray_refresh; ///< 请求保护插图灰阶 / Preserve illustration grays
    bool water_effect; ///< 用户选择水波纹效果 / Selected water-turn effect
    bool cleanup; ///< 正文周期清理 / Periodic reader cleanup
    bool tools_toggle; ///< 本帧切换工具栏 / Tools toggle in this frame
    bool closing_tools_to_text; ///< 工具栏收起回到纯文字 / Closing tools onto text
    bool closing_panel_to_text; ///< 面板收起回到纯文字 / Closing a panel onto text
    bool feedback; ///< 书架按压局部反馈 / Shelf press feedback
    bool fullscreen; ///< 阅读全屏偏好 / Reader full-screen state
    bool turn_pending; ///< 普通文字翻页 / Ordinary text turn
    bool water_turn_pending; ///< 已允许水波纹翻页 / Approved water turn
    bool status_pending; ///< 本次翻页顶部状态有变化 / Top status changed for this turn
    bool footer_pending; ///< 还需更新页脚 / Footer still needs output
    bool footer_strong; ///< 换章页脚使用旧 GL16 清理 / Use legacy GL16 cleanup for chapter footers
    bool hold_gray; ///< 归一时保持图片灰阶 / Hold picture grays during normalization
} reader_present_request_t;

typedef struct {
    enum EpdDrawError error; ///< 原显示出口返回的错误位 / Original display error bits
    bool tools_band; ///< 只更新了工具栏带 / Only the tools band was updated
    bool status_updated; ///< 顶部带已成功输出 / Top band was successfully presented
    bool footer_updated; ///< 普通局部路径更新了页脚 / Ordinary area path updated the footer
} reader_present_result_t;

/// 同步借用 hl 的真实前后帧；调用方保证几何、波形和帧在返回前稳定。
/// Borrow hl's real front/back frames synchronously; callers keep geometry, waveforms and frames stable until return.
/// 只选择原有显示出口；欠载恢复、性能收尾和预绘交还仍由调用方按原顺序执行。
/// Select existing display exits only; callers retain underrun recovery, performance completion and prefetch handoff order.
reader_present_result_t reader_present_output(EpdiyHighlevelState *hl,
                                               const reader_present_request_t *request);
