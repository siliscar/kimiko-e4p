/* 实际关电任务使用pthread替代RTOS边界，不替换供电策略。/ Replace RTOS boundaries with pthreads while retaining the actual power policy. */
#include "epd_power_guard.h"
#include "epd_board.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct HostSem { pthread_mutex_t lock; pthread_cond_t cond; unsigned value; };
struct HostTask {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned notifications;
    void (*entry)(void*);
    void* arg;
};
struct HostTimer {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool armed, executing, closing;
    int64_t deadline;
    esp_timer_create_args_t args;
};
static _Thread_local TaskHandle_t current_task;
static TaskHandle_t latest_task;
static atomic_bool physical_on, scanning;
static atomic_uint on_calls, off_calls, attempts, failures, releases;
static atomic_int_fast64_t last_off_us, first_attempt_us;
static epd_ctrl_state_t state;

int64_t esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* 宿主 esp_timer 桩：以 0.2ms 轮询检测绝对期限，彻底绕开 macOS 上
 * pthread_cond_timedwait 使用 CLOCK_REALTIME、而 esp_timer_get_time() 使用
 * CLOCK_MONOTONIC 的时钟域差异，以及 stop/start 广播丢失唤醒的竞态。
 * 所有断言都在 100ms 量级，0.2ms 轮询抖动可忽略。
 * Host esp_timer stub: poll the absolute deadline every 0.2 ms. This sidesteps the macOS
 * clock-domain mismatch (pthread_cond_timedwait uses CLOCK_REALTIME while esp_timer_get_time()
 * uses CLOCK_MONOTONIC) and the lost-wakeup races of stop/start broadcasts. All asserts are on
 * a 100 ms scale, so 0.2 ms of polling jitter is negligible. */
static void* timer_task(void* arg) {
    esp_timer_handle_t timer = arg;
    for (;;) {
        pthread_mutex_lock(&timer->lock);
        if (timer->closing) { pthread_mutex_unlock(&timer->lock); break; }
        const bool due = timer->armed && esp_timer_get_time() >= timer->deadline;
        void (*callback)(void*) = due ? timer->args.callback : NULL;
        void* callback_arg = timer->args.arg;
        if (due) { timer->armed = false; timer->executing = true; }
        pthread_mutex_unlock(&timer->lock);
        if (callback) {
            // 回调不持定时器锁：可安全调用 stop/start_once_at 重新排期。
            // Do not hold the timer lock across the callback: it may stop/re-arm the alarm.
            callback(callback_arg);
            pthread_mutex_lock(&timer->lock);
            timer->executing = false;
            pthread_cond_broadcast(&timer->cond);   // 唤醒 esp_timer_stop_blocking 的等待者。
            pthread_mutex_unlock(&timer->lock);
        } else {
            usleep(200);
        }
    }
    return NULL;
}
int esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* out) {
    esp_timer_handle_t timer = calloc(1, sizeof(*timer)); assert(timer);
    pthread_mutex_init(&timer->lock, NULL); pthread_cond_init(&timer->cond, NULL);
    timer->args = *args; *out = timer;
    assert(!pthread_create(&timer->thread, NULL, timer_task, timer));
    return ESP_OK;
}
int esp_timer_start_once_at(esp_timer_handle_t timer, uint64_t deadline) {
    // deadline 与 esp_timer_get_time() 同为 CLOCK_MONOTONIC 微秒绝对时刻；轮询线程每 0.2ms
    // 自行读取，无需广播唤醒，也不存在 stop/start 丢信号的竞态。
    // deadline shares the CLOCK_MONOTONIC microsecond domain of esp_timer_get_time(); the
    // polling thread reads it every 0.2 ms, so no broadcast or lost-wakeup race is possible.
    pthread_mutex_lock(&timer->lock);
    int result = timer->armed ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (!result && deadline <= (uint64_t)esp_timer_get_time()) result = ESP_ERR_INVALID_ARG;
    if (!result) {
        timer->deadline = (int64_t)deadline;
        timer->armed = true;
    }
    pthread_mutex_unlock(&timer->lock);
    return result;
}
int esp_timer_stop(esp_timer_handle_t timer) {
    pthread_mutex_lock(&timer->lock);
    int result = timer->armed ? ESP_OK : ESP_ERR_INVALID_STATE;
    timer->armed = false; pthread_cond_broadcast(&timer->cond);
    pthread_mutex_unlock(&timer->lock);
    return result;
}
int esp_timer_stop_blocking(esp_timer_handle_t timer, uint32_t ticks) {
    assert(ticks == portMAX_DELAY);
    pthread_mutex_lock(&timer->lock);
    timer->armed = false; pthread_cond_broadcast(&timer->cond);
    while (timer->executing) pthread_cond_wait(&timer->cond, &timer->lock);
    pthread_mutex_unlock(&timer->lock);
    return ESP_OK;
}
int esp_timer_delete(esp_timer_handle_t timer) {
    pthread_mutex_lock(&timer->lock);
    assert(!timer->armed && !timer->executing);
    timer->closing = true; pthread_cond_broadcast(&timer->cond);
    pthread_mutex_unlock(&timer->lock);
    assert(!pthread_join(timer->thread, NULL));
    pthread_mutex_destroy(&timer->lock); pthread_cond_destroy(&timer->cond); free(timer);
    return ESP_OK;
}
void host_log(const char* tag, const char* fmt, ...) { (void)tag; (void)fmt; }

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
    SemaphoreHandle_t sem = calloc(1, sizeof(*sem)); assert(sem);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&sem->lock, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&sem->cond, NULL);
    return sem;
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return xSemaphoreCreateRecursiveMutex(); }
int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t wait) {
    assert(wait == portMAX_DELAY); return pthread_mutex_lock(&sem->lock) == 0;
}
int xSemaphoreGiveRecursive(SemaphoreHandle_t sem) { return pthread_mutex_unlock(&sem->lock) == 0; }
int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t wait) {
    assert(wait == portMAX_DELAY);
    pthread_mutex_lock(&sem->lock);
    while (!sem->value) pthread_cond_wait(&sem->cond, &sem->lock);
    sem->value = 0;
    pthread_mutex_unlock(&sem->lock);
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t sem) {
    pthread_mutex_lock(&sem->lock); sem->value = 1;
    pthread_cond_signal(&sem->cond); pthread_mutex_unlock(&sem->lock);
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t sem) {
    pthread_mutex_destroy(&sem->lock); pthread_cond_destroy(&sem->cond); free(sem);
}
static void* start_task(void* arg) {
    current_task = arg;
    current_task->entry(current_task->arg);
    return NULL;
}
BaseType_t xTaskCreate(void (*entry)(void*), const char* name, unsigned stack, void* arg,
                       unsigned priority, TaskHandle_t* result) {
    assert(!strcmp(name, "epd_power") && stack >= 2048 && priority >= 20);
    TaskHandle_t task = calloc(1, sizeof(*task)); assert(task);
    pthread_mutex_init(&task->lock, NULL); pthread_cond_init(&task->cond, NULL);
    task->entry = entry; task->arg = arg;
    *result = latest_task = task;
    assert(pthread_create(&task->thread, NULL, start_task, task) == 0);
    return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {
    pthread_mutex_lock(&task->lock); ++task->notifications;
    pthread_cond_signal(&task->cond); pthread_mutex_unlock(&task->lock);
}
unsigned ulTaskNotifyTake(int clear, TickType_t wait) {
    assert(clear && wait == portMAX_DELAY);
    TaskHandle_t task = current_task;
    pthread_mutex_lock(&task->lock);
    if (wait == portMAX_DELAY) {
        while (!task->notifications) pthread_cond_wait(&task->cond, &task->lock);
    } else {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += wait / 1000;
        until.tv_nsec += (long)(wait % 1000) * 1000000;
        if (until.tv_nsec >= 1000000000) { ++until.tv_sec; until.tv_nsec -= 1000000000; }
        while (!task->notifications)
            if (pthread_cond_timedwait(&task->cond, &task->lock, &until)) break;
    }
    unsigned result = task->notifications;
    task->notifications = 0;
    pthread_mutex_unlock(&task->lock);
    return result;
}
void vTaskDelete(TaskHandle_t task) { assert(!task); pthread_exit(NULL); }
void vTaskDelay(TickType_t ticks) { usleep(ticks * 1000); }

static void raw_on(epd_ctrl_state_t* ctrl) {
    assert(ctrl == &state && !scanning && !physical_on); physical_on = true; ++on_calls;
}
static bool raw_off(epd_ctrl_state_t* ctrl) {
    assert(ctrl == &state && !scanning);
    if (atomic_fetch_add(&attempts, 1) == 0) first_attempt_us = esp_timer_get_time();
    if (failures) { --failures; return false; }
    last_off_us = esp_timer_get_time(); physical_on = false; ++off_calls;
    return true;
}
static void raw_release(void) { assert(physical_on && !scanning); ++releases; }
static const EpdBoardDefinition board = {.poweron=raw_on, .poweroff_checked=raw_off,
                                         .release_drive=raw_release};
const EpdBoardDefinition* epd_current_board(void) { return &board; }
epd_ctrl_state_t* epd_ctrl_state(void) { return &state; }
void epd_set_mode(bool mode) { (void)mode; assert(false); }

static void wait_off(unsigned count) {
    int64_t limit = esp_timer_get_time() + 4000000;
    while (off_calls < count && esp_timer_get_time() < limit) usleep(1000);
    assert(off_calls == count && !physical_on);
}
static void join_worker(void) {
    assert(pthread_join(latest_task->thread, NULL) == 0);
    pthread_mutex_destroy(&latest_task->lock); pthread_cond_destroy(&latest_task->cond);
    free(latest_task); latest_task = NULL;
}

static atomic_bool competing_scan;
static void *scan_during_flash(void *arg) {
    (void)arg;
    epd_power_guard_scan_begin();
    competing_scan = true;
    epd_power_guard_scan_end();
    return NULL;
}

int main(void) {
    epd_power_guard_init();
    // 主线程阻塞，任务仍独立关电；重复上电和旧通知不能延长期限。
    // The worker shuts down while the main thread blocks; repeated on calls and stale notices cannot extend the deadline.
    int64_t start = esp_timer_get_time();
    epd_power_guard_on(); usleep(40000); epd_power_guard_on();
    xTaskNotifyGive(latest_task); usleep(300000);
    wait_off(1);
    // 上界从宽：pthread 定时线程唤醒/调度抖动不应使测试偶发失败（断言重点是 ~100ms 量级而非精确毫秒）。
    // Loose upper bound: pthread timer wake/scheduling jitter must not flake the test (the point is the ~100 ms order, not exact milliseconds).
    assert(last_off_us - start >= 100000 && last_off_us - start < 250000);
    assert(on_calls == 1 && releases == 1);
    printf("blocked caller: shutdown after %lld us\n", (long long)(last_off_us-start));

    // 扫描跨过旧期限不准断电；最后一轮结束后重新计时，包含嵌套与过期通知。
    // A scan crossing the old deadline cannot lose power; restart timing after the last scan, including nesting and stale notices.
    epd_power_guard_on(); usleep(70000);
    epd_power_guard_scan_begin(); scanning = true;
    xTaskNotifyGive(latest_task); usleep(180000);
    assert(physical_on && off_calls == 1);
    scanning = false; epd_power_guard_scan_end(); usleep(40000);
    epd_power_guard_scan_begin(); epd_power_guard_scan_begin();
    scanning = true; usleep(140000); assert(off_calls == 1 && physical_on);
    scanning = false; epd_power_guard_scan_end();
    scanning = true; usleep(130000); assert(off_calls == 1 && physical_on);
    scanning = false; start = esp_timer_get_time(); epd_power_guard_scan_end();
    wait_off(2);
    // 定时线程唤醒、调度与关闭 I2C 的抖动只约束下界，上界从宽防偶发慢 CI。
    // Timer wake-up, scheduling and I2C-off jitter only constrain the lower bound; the upper bound is loose for slow CI.
    assert(last_off_us - start >= 100000);

    // 关断命令失败保留带电状态，独立重试而不依赖下一次扫描。
    // Failed shutdown commands retain powered state and retry independently of another scan.
    failures = 2; attempts = 0;
    epd_power_guard_scan_begin(); start = esp_timer_get_time(); epd_power_guard_scan_end();
    wait_off(3);
    assert(attempts == 3 && first_attempt_us - start >= 100000);
    assert(last_off_us - first_attempt_us >= 10000);
    epd_power_guard_on(); failures = 1; attempts = 0;
    epd_power_guard_off(); assert(physical_on);
    wait_off(4); assert(attempts == 2);

    // 失败后立即刷新必须确认关断并重新上电，不能复用“可能带电”的状态。
    // An immediate scan after failure must confirm off and repower instead of reusing uncertain power.
    epd_power_guard_on(); failures = 2; attempts = 0;
    epd_power_guard_off(); assert(physical_on);
    epd_power_guard_scan_begin();
    assert(physical_on && !failures && off_calls == 5 && attempts == 3);
    scanning = true; usleep(1000); scanning = false;
    epd_power_guard_scan_end();
    epd_power_guard_off(); wait_off(6);

    // 显式关电取消旧期限；反复销毁重建，不允许访问已释放的板级资源。
    // Explicit off cancels old deadlines; repeated teardown/reinit must not access released board resources.
    epd_power_guard_on(); epd_power_guard_off(); wait_off(7);
    usleep(130000); assert(off_calls == 7);
    epd_power_guard_deinit(); join_worker();
    for (unsigned i=0; i<10; ++i) {
        epd_power_guard_init(); epd_power_guard_on();
        epd_power_guard_deinit(); join_worker();
    }
    assert(!physical_on && off_calls == 17);
    // 显示组不提前上电；嵌套结束不关电，组内阻塞受100ms保护，最外层结束按空闲期到期关电（尊重提示，不提前切断）。
    // Display groups do not power early; nested ends keep the rails on, blocking within a group retains
    // the 100 ms fallback, and the outermost end shuts down at the idle deadline (honoring hints, never cutting early).
    epd_power_guard_init();
    unsigned before_on = on_calls, before_off = off_calls;
    epd_power_guard_session_begin(); epd_power_guard_session_begin();
    usleep(130000); assert(on_calls == before_on && !physical_on);
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    assert(on_calls == before_on + 1 && physical_on);
    epd_power_guard_session_end(); assert(physical_on && off_calls == before_off);
    start = esp_timer_get_time(); epd_power_guard_session_end();
    assert(physical_on && off_calls == before_off);
    // 组内扫描的100ms空闲期在最外层结束后仍然到期关电。
    // The in-group scan's 100 ms idle deadline still expires and powers off after the outermost end.
    wait_off(before_off + 1);
    assert(last_off_us - start >= 50000);
    // 应用提示（阅读翻页）：真实翻页先给提示、再刷屏。提示窗（900ms）必须把连刷600ms/
    // 单刷100ms 的空闲期顶到提示期限，最外层显示组结束不得提前关电作废提示；提示随确认关电失效。
    // An app hint (reading turn): real turns hint first, then scan. The 900 ms hint must stretch
    // the 600 ms burst / 100 ms lone window to its deadline; the outermost group end must not cut
    // power early and invalidate the hint. The hint expires with the confirmed off.
    epd_power_guard_session_begin();
    epd_power_guard_hint(900);
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    start = esp_timer_get_time(); epd_power_guard_session_end();
    // 熬过 600ms 连刷窗仍带电：证明保持来自提示而非连刷窗，组结束没有提前关电。
    // Still powered past the 600 ms burst window: the hold comes from the hint, not the burst
    // window, and the group end did not cut power early.
    usleep(700000); assert(physical_on && off_calls == before_off + 1);
    wait_off(before_off + 2);
    assert(last_off_us - start >= 800000 && last_off_us - start < 1300000);
    epd_power_guard_session_begin();
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    wait_off(before_off + 3);
    epd_power_guard_session_end(); assert(off_calls == before_off + 3);

    // 写Flash先确认关电；关断失败拒绝写入，写入期间其他扫描必须等待。
    // Flash access confirms off first, rejects failed shutdown and excludes concurrent scans throughout the write.
    epd_power_guard_scan_begin();
    assert(!epd_power_guard_flash_begin() && physical_on);
    epd_power_guard_scan_end(); failures = 1;
    assert(!epd_power_guard_flash_begin() && physical_on);
    assert(!epd_power_guard_scan_allowed());
    // 上面三个显示组各关一次电（before_off+1..+3），此处等的是关断失败后的独立重试（+4）。
    // The three display groups above each powered off (before_off+1..+3); here we wait for the
    // independent retry after the failed shutdown (+4).
    wait_off(before_off + 4); assert(epd_power_guard_scan_allowed());
    epd_power_guard_scan_begin(); epd_power_guard_scan_end();
    assert(epd_power_guard_flash_begin() && !physical_on);
    assert(epd_power_guard_flash_begin()); epd_power_guard_flash_end();
    pthread_t competitor;
    assert(!pthread_create(&competitor, NULL, scan_during_flash, NULL));
    usleep(130000); assert(!competing_scan && !physical_on);
    epd_power_guard_flash_end();
    assert(!pthread_join(competitor, NULL) && competing_scan && physical_on);
    epd_power_guard_off();
    epd_power_guard_deinit(); join_worker();
    assert(epd_power_guard_flash_begin()); epd_power_guard_flash_end();
    puts("PASS: actual worker deadlines, blocked caller, active/nested scans, stale notices, retry, explicit off and teardown");
}
