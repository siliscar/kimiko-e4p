/*
 * 中文：同步翻页控制；页面提供章节加载与保存，保留资源交还和绘制责任。
 * English: Synchronous turn control; the page supplies chapter loading/saving and retains resource handoff and painting.
 */
#pragma once
#include <stdbool.h>
#include "reader_position.h"

typedef enum {
    READER_TURN_MOVED, ///< 已切到目标页 / Moved to the target page
    READER_TURN_EDGE, ///< 到达整书边界 / Reached a book boundary
    READER_TURN_LOAD_FAILED, ///< 跨章失败，页面决定回退 / Chapter load failed; page chooses fallback
} reader_turn_result_t;

typedef struct {
    void *context; ///< 仅本次同步调用借用 / Borrowed only during this synchronous call
    void (*save)(void *context); ///< 跨章前保存，失败仍沿用原加载策略 / Save before crossing; retain the original load policy after save failure
    bool (*load)(void *context, size_t chapter, bool last); ///< 沿用原章节交还、布局和位置提交 / Retain original chapter handoff, layout and position commit
} reader_turn_ops_t;

/// 调用方保证正文有效、计数属于当前布局；同章只改页码，跨章先保存再同步加载。
/// Callers provide valid content and current counts; same-chapter turns change only page, crossings save then load synchronously.
/// 不代替加载器提交章节位置，也不在失败后回滚位置或释放资源。
/// Never commit chapter position for the loader, roll position back on failure, or free resources.
reader_turn_result_t reader_turn_move(reader_position_t *position, int direction,
                                      size_t pages, size_t chapters,
                                      const reader_turn_ops_t *ops);

typedef struct {
    bool chrome_changed; ///< 本次收起了覆盖层 / This turn dismissed an overlay
    bool area_redraw; ///< 绘制返回局部更新 / Paint returned an area update
    bool from_text; ///< 实际前帧是纯文字 / Displayed prior frame was text
    bool from_stable; ///< 实际前帧可以差分 / Displayed prior frame allows a differential
    bool images; ///< 目标页有图片槽 / Target page contains image slots
    bool background; ///< 当前启用阅读背景 / Reading background is enabled
    bool cleanup; ///< 本次需要周期全刷 / This turn requires periodic full refresh
    bool water_effect; ///< 用户选择水波纹 / User selected the water effect
    bool chapter_changed; ///< 加载后章节发生变化 / Chapter changed after loading
} reader_turn_frame_t;

typedef struct {
    bool text; ///< 普通文字翻页许可 / Ordinary text-turn eligibility
    bool water; ///< 水波纹翻页许可 / Water-turn eligibility
    bool footer_strong; ///< 换章页脚使用 GL16 / Chapter footer uses GL16
} reader_turn_effects_t;

/// 绘制后判定原有翻页许可；不改帧、不推屏、不读取全局设置。
/// Decide existing turn eligibility after painting; never change frames, present, or read global settings.
reader_turn_effects_t reader_turn_effects(const reader_turn_frame_t *frame);
