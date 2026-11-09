/*
 * 中文：单 UI 阅读预绘的有界任务、逐行交还与帧缓存；不决定页面或刷新策略。
 * English: Bounded reader worker, row handoff and frame cache for one UI; no page or refresh policy.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "epdiy.h"

/// 串行化借用布局和字体的绘制；首次启动预绘前无需锁。
/// Serialize drawing that borrows layout and fonts; no lock is needed before the first worker start.
void reader_prefetch_lock(void);
/// 交还绘制锁。/ Return the draw lock.
void reader_prefetch_unlock(void);
/// UI 确认允许预绘后启动目标页；true 必须与同次同步推屏后的 end 配对。
/// Start a UI-approved target; true must pair with end after that synchronous presentation.
/// 布局、字体、几何和章节在 end 返回前不可替换；不预绘插图由页面决定。
/// Keep layout, fonts, geometry and chapter alive until end returns; the page excludes illustrations.
/// 帧容量在 discard/release 前保持不变，来自 S31 的固定帧尺寸。
/// Keep frame capacity unchanged until discard/release, using the fixed S31 frame dimensions.
bool reader_prefetch_start(size_t page, EpdRect body, int px, size_t frame_bytes);
/// 停止当前行之后的工作并等待交还；false 不执行操作。
/// Stop after the current row and await handoff; false does nothing.
void reader_prefetch_end(bool started);
/// 无后台借用时续画部分目标页；代次不匹配时不接管。
/// Finish a partial target without a background borrower; reject a different layout generation.
bool reader_prefetch_resume(size_t page);
/// 借用同代次完整帧；失效、重用或释放后指针失效。
/// Borrow a complete frame of the current generation; invalidation, reuse or release ends the borrow.
const uint8_t *reader_prefetch_frame(size_t page);
/// UI 空闲且后台已交还时，最多补画八行或四毫秒；只续画已准备的目标。
/// With the UI idle and worker returned, draw up to eight rows or four milliseconds of the prepared target.
void reader_prefetch_work_idle(size_t page);
/// 后台已交还时清空目标和代次；调用方仍须保护布局与字体变更。
/// Clear target and generation after worker handoff; callers still protect layout and font changes.
void reader_prefetch_invalidate(void);
/// 同样前提下释放帧缓存，保留任务和锁。/ Under the same precondition, free the frame but retain worker and locks.
void reader_prefetch_discard(void);
/// 离开阅读应用且后台已交还后回收全部资源；可重复调用。
/// Reclaim everything after leaving the reader and worker handoff; repeated calls are safe.
void reader_prefetch_release(void);
