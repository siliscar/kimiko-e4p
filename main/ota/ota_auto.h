/* 中文：主循环独占的自动 OTA 检查调度，不下载、不安装、不主动唤醒。
 * English: Main-loop-owned automatic OTA checks, without downloading, installing or waking. */
#pragma once
#include <stdbool.h>

/// 仅在安全空闲窗口启动检查；完成结果可延后提示。/ Start in safe idle windows; retain results for later prompting.
void pico_ota_auto_poll(bool idle);
/// 是否有待提示的新版本；消费后本轮不再弹出。/ A pending new-version offer; acknowledging suppresses this cycle's prompt.
bool pico_ota_auto_pending(void);
void pico_ota_auto_acknowledge(void);
/// 切页、锁屏与关机前仅取消自动任务，不干预手动升级。/ Cancel only automatic work before navigation, lock or shutdown.
void pico_ota_auto_cancel(void);
