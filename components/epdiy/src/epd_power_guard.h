/*
 * 独立空闲关电；扫描与关电互斥，不依赖产品循环或 OTA 锁。
 * Independent idle shutdown; serialize scans and power-off without the app loop or OTA lock.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/// 板级初始化后启动关电任务。/ Start shutdown supervision after board initialization.
void epd_power_guard_init(void);
/// 确认关电并停止任务；调用方须先停止提交扫描。/ Confirm power-off and stop supervision after callers stop submitting scans.
void epd_power_guard_deinit(void);
/// 独立上电也只保留100ms；已有期限不延期。/ Standalone power-on lasts 100 ms without extending an existing deadline.
void epd_power_guard_on(void);
/// 显式关电失败时由任务继续重试。/ Continue retrying a failed explicit shutdown in the worker.
void epd_power_guard_off(void);
/// 扫描前持锁并确保上电，必须配对end。/ Lock and ensure power before scanning; pair with end.
void epd_power_guard_scan_begin(void);
/// 扫描完成立即释放驱动，从此刻起计时空闲：单次刷新 100 ms，间隔 <600 ms 的连刷保持 600 ms。/ Release drive after scanning and arm the idle deadline at completion: 100 ms for a lone refresh, 600 ms held across rapid sequences.
void epd_power_guard_scan_end(void);
/// 应用层提示：随后的扫描把供电保持到调用时刻加 hold_ms（阅读翻页用秒级窗）。只延长空闲期、不上电，确认关电即失效。/ App hint: later scans hold power until now+hold_ms (reading turns use a seconds-long window). Extends only the idle window, never powers on, and expires on a confirmed off.
void epd_power_guard_hint(uint32_t hold_ms);
/// 检查扫描准入，不提前上电。/ Check scan admission without powering on early.
bool epd_power_guard_scan_allowed(void);
/// 嵌套显示组只记录范围，不上电或延后空闲期限。/ Nested display groups neither power on nor extend idle deadlines.
void epd_power_guard_session_begin(void);
/// 最外层显示组结束不立即关电：按已排空闲期（含应用提示）到期关电，失败仍由任务重试。/ The outermost group end does not power off immediately: shutdown lands at the armed idle deadline (app hints included); the worker retries failures.
void epd_power_guard_session_end(void);
/// 确认关电并持供电锁；失败不得写Flash。/ Confirm off and hold the power lock; never write flash on failure.
bool epd_power_guard_flash_begin(void);
/// 同任务释放成功取得的Flash保护。/ Release successful flash protection from the same task.
void epd_power_guard_flash_end(void);
