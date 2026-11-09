/*
 * 独立高优先级任务负责空闲关电。仅真实扫描持有供电锁，耗时绘制不续期。
 * A dedicated high-priority task owns the idle shutdown. Only physical scans hold the power lock;
 * slow framebuffer preparation does not renew the deadline.
 *
 * 用户修订：空闲窗自适应两档——单次刷新后仍 100ms 关电；间隔 <600ms 的连刷（滑杆拖动、
 * 键盘反馈、连续滚动）期间保持供电，不再每次扫描重付上电序列。另有应用层提示窗
 * （阅读翻页）可把保持期延长到秒级，提示随确认关电一起失效。
 * User revision: two-tier idle window — a lone refresh still powers off after 100 ms, while
 * scans arriving less than 600 ms apart (slider drags, keyboard feedback, continuous scrolls)
 * keep the rails on instead of repaying the power-up sequence on every scan. An app-side hint
 * window (reading turns) can stretch the hold to seconds; hints expire with a confirmed off.
 */
#include "epd_power_guard.h"
#include "epd_board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define IDLE_US INT64_C(100000)
// 连刷判定与保持窗：滑杆 120ms 节流、键盘 DU 反馈、主页面连续滚动都落在这个间隔内。
// Burst detection and hold window: the 120 ms slider throttle, keyboard DU feedback and
// main-page continuous scrolls all fall inside this gap.
#define BURST_GAP_US INT64_C(600000)
#define BURST_IDLE_US INT64_C(600000)
#define RETRY_US INT64_C(5000)
static SemaphoreHandle_t power_lock, stopped;
static TaskHandle_t worker;
static esp_timer_handle_t alarm_timer;
static bool powered, uncertain, stopping;
static unsigned scan_depth, session_depth, flash_depth;
static int64_t idle_deadline_us, wake_deadline_us;
// 顶层扫描的起点与上一次完成时刻，用来判定连刷；应用提示的绝对期限在 arm 时并入空闲期。
// Top-level scan start and the previous completion time, used to detect bursts; the app hint
// deadline folds into the idle window at arm time.
static int64_t scan_start_us, last_end_us, hint_deadline_us;

static void lock(void) {
    if (xSemaphoreTakeRecursive(power_lock, portMAX_DELAY) != pdTRUE) abort();
}
static void unlock(void) { xSemaphoreGiveRecursive(power_lock); }

static void release_drive(void) {
    const EpdBoardDefinition* board = epd_current_board();
    if (board->release_drive) board->release_drive();
    else epd_set_mode(false);
}

static void alarm_callback(void* arg) {
    (void)arg;
    xTaskNotifyGive(worker);
}

static void schedule_alarm(int64_t deadline_us) {
    esp_err_t result = esp_timer_stop(alarm_timer);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) abort();
    if (!deadline_us) return;
    if (deadline_us > esp_timer_get_time()) {
        result = esp_timer_start_once_at(alarm_timer, deadline_us);
        if (result == ESP_OK) return;
        if (result != ESP_ERR_INVALID_ARG || deadline_us > esp_timer_get_time()) abort();
    }
    // 绝对期限已过就直接唤醒，不再等待系统节拍累计。
    // Wake immediately when the absolute deadline has passed instead of accumulating RTOS ticks.
    xTaskNotifyGive(worker);
}

static void arm(int64_t completed_us, int64_t window_us) {
    idle_deadline_us = completed_us + window_us;
    // 未到期的应用提示把空闲期拉长到提示期限；扫描期间提示自然续期（每次翻页都会重设）。
    // An unexpired app hint stretches the idle window to the hinted deadline; scans keep it
    // alive naturally because every turn re-arms it.
    if (hint_deadline_us > idle_deadline_us) idle_deadline_us = hint_deadline_us;
    wake_deadline_us = idle_deadline_us;
    schedule_alarm(wake_deadline_us);
}

static bool power_off(void);

static void power_on(void) {
    if (powered && !uncertain) return;
    // 未确认关断不能当作有效供电；先反复确认关断，再执行完整上电，禁止无效供电下扫描。
    // Unconfirmed shutdown is not ready power; confirm off before a full power-on rather than scanning with uncertain rails.
    while (uncertain) {
        if (power_off()) uncertain = powered = false;
        else vTaskDelay(pdMS_TO_TICKS(5));
    }
    // 失败也视作可能带电；板级失败策略不得继续扫描。
    // Treat failure as possibly powered; the board failure policy must prevent scanning.
    powered = true;
    epd_current_board()->poweron(epd_ctrl_state());
}

static bool power_off(void) {
    const EpdBoardDefinition* board = epd_current_board();
    if (board->poweroff_checked) return board->poweroff_checked(epd_ctrl_state());
    board->poweroff(epd_ctrl_state());
    return true;
}

static void shut_down(void) {
    const int64_t started_us = esp_timer_get_time();
    const int64_t deadline_us = idle_deadline_us;
#if CONFIG_DISPLAY_POWER_DIAG
    // 连翻期间电轨提前关断的触发源（hint 应把空闲期顶到提示期限）。
    // Who cuts the rails mid-hold during continuous turns (a hint should push the idle deadline out).
    ESP_LOGI("epd_power", "HV_OFF now=%lld idle=%lld hint=%lld scans=%u sess=%u",
             (long long)started_us, (long long)idle_deadline_us,
             (long long)hint_deadline_us, scan_depth, session_depth);
#endif
    const bool ok = power_off();
    const int64_t completed_us = esp_timer_get_time();
    powered = !ok;
    uncertain = !ok;
    wake_deadline_us = ok ? 0 : completed_us + RETRY_US;
    if (ok) {
        idle_deadline_us = 0;
        // 确认关电后提示一并失效：提示只服务随后的扫描，不能把已关的电当成仍被需要。
        // A confirmed off invalidates the hint too: hints only serve scans that follow.
        hint_deadline_us = 0;
    }
    schedule_alarm(wake_deadline_us);
    // 诊断不参与期限计算；失败必须保留可能带电状态并重试。
    // Diagnostics do not determine deadlines; failures retain possible power and schedule a retry.
#if CONFIG_DISPLAY_POWER_DIAG
    ESP_LOGI("epd_power", "HV_IDLE deadline_us=%lld start_us=%lld done_us=%lld late_us=%lld ok=%d",
             (long long)deadline_us, (long long)started_us, (long long)completed_us,
             (long long)(deadline_us ? started_us - deadline_us : 0), ok);
#else
    (void)started_us;
    (void)deadline_us;
#endif
    if (!ok) ESP_LOGE("epd_power", "HV shutdown unconfirmed; retrying");
}

static void power_task(void* arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        lock();
        const int64_t now_us = esp_timer_get_time();
        if (wake_deadline_us && now_us >= wake_deadline_us) shut_down();
        else schedule_alarm(wake_deadline_us);
        if (stopping && !powered) {
            // 回调只通知任务，不持供电锁；先等回调退出再释放任务句柄。
            // Callbacks only notify and never take the power lock; join callbacks before releasing the task handle.
            if (esp_timer_stop_blocking(alarm_timer, portMAX_DELAY) != ESP_OK) abort();
            unlock();
            xSemaphoreGive(stopped);
            vTaskDelete(NULL);
            return;
        }
        unlock();
    }
}

void epd_power_guard_init(void) {
    assert(!power_lock && !worker);
    power_lock = xSemaphoreCreateRecursiveMutex();
    stopped = xSemaphoreCreateBinary();
    if (!power_lock || !stopped) abort();
    powered = uncertain = stopping = false;
    scan_depth = session_depth = flash_depth = 0;
    idle_deadline_us = wake_deadline_us = 0;
    scan_start_us = last_end_us = hint_deadline_us = 0;
    const esp_timer_create_args_t args = {
        .callback = alarm_callback,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "epd_idle",
    };
    if (esp_timer_create(&args, &alarm_timer) != ESP_OK) abort();
    // 定时回调只发通知，I2C由独立任务执行；不持产品/OTA锁。分配失败禁止启用面板。
    // Timer callbacks only notify; a dedicated task performs I2C without the product/OTA lock. Allocation failure prevents panel use.
    BaseType_t result = xTaskCreate(power_task, "epd_power", 3072, NULL,
                                    configMAX_PRIORITIES - 2, &worker);
    if (result != pdPASS) abort();
}

void epd_power_guard_on(void) {
    lock();
    assert(!stopping && !flash_depth);
    if (!powered || uncertain) {
        power_on();
        release_drive();
        arm(esp_timer_get_time(), IDLE_US);
    }
    unlock();
}

void epd_power_guard_off(void) {
    lock();
    assert(scan_depth == 0);
    if (powered) shut_down();
    xTaskNotifyGive(worker);
    unlock();
}

void epd_power_guard_scan_begin(void) {
    lock();
    assert(!stopping && !flash_depth);
    if (++scan_depth == 1) scan_start_us = esp_timer_get_time();
    idle_deadline_us = wake_deadline_us = 0;
    schedule_alarm(0);
    power_on();
}

void epd_power_guard_scan_end(void) {
    const int64_t completed_us = esp_timer_get_time();
    assert(scan_depth > 0);
    if (--scan_depth == 0) {
        release_drive();
        // 两次顶层扫描间隔小于连刷判定窗：拖动/连滚期间保持供电，下一次扫描免上电序列。
        // Two top-level scans closer than the burst gap: keep the rails on through the drag
        // or scroll so the next scan skips the power-up sequence.
        const bool burst = last_end_us && scan_start_us - last_end_us < BURST_GAP_US;
        arm(completed_us, burst ? BURST_IDLE_US : IDLE_US);
        last_end_us = completed_us;
    }
    unlock();
}

void epd_power_guard_hint(uint32_t hold_ms) {
    if (!power_lock) return;
    lock();
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)hold_ms * 1000;
#if CONFIG_DISPLAY_POWER_DIAG
    ESP_LOGI("epd_power", "HINT hold=%ums deadline=%lld powered=%d scans=%u idle=%lld",
             (unsigned)hold_ms, (long long)deadline_us, (int)powered, scan_depth,
             (long long)idle_deadline_us);
#endif
    if (deadline_us > hint_deadline_us) hint_deadline_us = deadline_us;
    // 电轨还在且空闲期限已排定：提示更晚就立刻延后唤醒，否则工作线程会按原期限关电。
    // Rails on with a pending idle deadline: push the wake later immediately, or the worker
    // powers off at the original deadline and ignores the hint.
    if (powered && !scan_depth && idle_deadline_us && hint_deadline_us > idle_deadline_us) {
        idle_deadline_us = wake_deadline_us = hint_deadline_us;
        schedule_alarm(wake_deadline_us);
    }
    unlock();
}

bool epd_power_guard_scan_allowed(void) {
    lock();
    const bool allowed = !stopping && !uncertain && !flash_depth;
    unlock();
    return allowed;
}

void epd_power_guard_session_begin(void) {
    lock();
    assert(!stopping && !flash_depth);
    ++session_depth;
    unlock();
}

void epd_power_guard_session_end(void) {
    lock();
    assert(session_depth && !scan_depth);
    if (!--session_depth && powered) {
        // 显示组结束不再立即关电：翻页等提示把空闲期顶到秒级，立即关会作废提示并让下一页重付上电序列。
        // 组内扫描已排定的空闲期（单刷100ms/连刷600ms）也由到期关电兜底，此处不提前打断。
        // Display-group end no longer powers off immediately: reading-turn hints stretch the idle
        // window to seconds, and cutting the rails here invalidates the hint so the next turn repays
        // the power-up sequence. Deadlines armed by in-group scans (100 ms lone / 600 ms burst) still
        // shut down on expiry; this path never cuts them short.
        const int64_t now_us = esp_timer_get_time();
        if (!idle_deadline_us) {
            // 无已排期限：无提示按单刷窗兜底；有未过期提示保持到提示期限。
            // No armed deadline: fall back to the lone-refresh window without a hint, or hold to an unexpired hint.
            const int64_t window_us = hint_deadline_us > now_us ? hint_deadline_us - now_us : IDLE_US;
            arm(now_us, window_us);
        } else if (hint_deadline_us > idle_deadline_us) {
            // 提示更晚：立即延后唤醒，否则工作线程按原期限关电并忽略提示。
            // A later hint pushes the wake out now, or the worker powers off at the original deadline and ignores it.
            idle_deadline_us = wake_deadline_us = hint_deadline_us;
            schedule_alarm(wake_deadline_us);
        }
    }
    unlock();
}

bool epd_power_guard_flash_begin(void) {
    // 启动早期尚未初始化屏幕时没有高压资源；调用方须保证初始化/销毁不与写入并发。
    // Before display initialization there are no HV resources; callers serialize initialization/teardown with writes.
    if (!power_lock) return true;
    lock();
    if (stopping || scan_depth) { unlock(); return false; }
    if (powered) shut_down();
    if (powered) { unlock(); return false; }
    ++flash_depth;
    // 实际Flash操作期间持锁，防止其他任务在确认关电后重新开始扫描。
    // Hold the lock through the actual flash operation so another task cannot restart scanning after confirmed off.
    return true;
}

void epd_power_guard_flash_end(void) {
    if (!power_lock) return;
    assert(flash_depth);
    --flash_depth;
    unlock();
}

void epd_power_guard_deinit(void) {
    lock();
    assert(!scan_depth && !session_depth && !flash_depth);
    stopping = true;
    if (powered) shut_down();
    xTaskNotifyGive(worker);
    unlock();
    // 关电未确认时保持板级资源可用，独立任务继续重试。
    // Keep board resources alive while the independent worker retries an unconfirmed shutdown.
    xSemaphoreTake(stopped, portMAX_DELAY);
    if (esp_timer_delete(alarm_timer) != ESP_OK) abort();
    alarm_timer = NULL;
    vSemaphoreDelete(stopped);
    vSemaphoreDelete(power_lock);
    stopped = power_lock = NULL;
    worker = NULL;
}
