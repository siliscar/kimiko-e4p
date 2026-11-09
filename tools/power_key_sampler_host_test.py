#!/usr/bin/env python3
"""中文：用实际电源键识别和采样任务验证漏按、并发交接与轮询回退。
English: Exercise actual key recognition and sampling for missed presses, handoff and fallback.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "components/device_power/device_power_metalio.c"

UNIT = r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY (~0u)
#define pdTRUE 1
#define pdPASS 1
#define TAG "key-test"
#define ESP_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef void *fca9555_handle_t;
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned notifications;
    void (*entry)(void *);
    void *arg;
} test_task_t;
typedef test_task_t *TaskHandle_t;
static _Thread_local TaskHandle_t current_task;
static atomic_bool fail_create, down, read_error;
static atomic_uint reads, battery_polls, glitch_reads;
static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bus_cond = PTHREAD_COND_INITIALIZER;
static bool block_read, read_blocked;
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {
    assert(pthread_mutex_init(storage, NULL) == 0); return storage;
}
static int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks) {
    (void)ticks; assert(pthread_mutex_lock(lock) == 0); return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t lock) {
    assert(pthread_mutex_unlock(lock) == 0); return pdTRUE;
}
static void vTaskDelay(unsigned ms) { usleep(ms * 1000); }
static int64_t esp_timer_get_time(void) {
    struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static void *task_entry(void *arg) {
    current_task = arg; current_task->entry(current_task->arg); return NULL;
}
static int xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
    unsigned stack, void *arg, unsigned priority, TaskHandle_t *out, int core) {
    assert(stack >= 2048 && priority == 4 && core == 1); (void)name;
    if (atomic_load(&fail_create)) return 0;
    test_task_t *t = calloc(1, sizeof(*t)); assert(t);
    assert(pthread_mutex_init(&t->mutex, NULL) == 0);
    assert(pthread_cond_init(&t->cond, NULL) == 0);
    t->entry = entry; t->arg = arg; *out = t;
    assert(pthread_create(&t->thread, NULL, task_entry, t) == 0); return pdPASS;
}
static void xTaskNotifyGive(TaskHandle_t t) {
    pthread_mutex_lock(&t->mutex); ++t->notifications;
    pthread_cond_signal(&t->cond); pthread_mutex_unlock(&t->mutex);
}
static unsigned ulTaskNotifyTake(int clear, unsigned ticks) {
    assert(clear == pdTRUE && ticks == portMAX_DELAY && current_task);
    pthread_mutex_lock(&current_task->mutex);
    while (!current_task->notifications) pthread_cond_wait(&current_task->cond, &current_task->mutex);
    unsigned count = current_task->notifications; current_task->notifications = 0;
    pthread_mutex_unlock(&current_task->mutex); return count;
}
static fca9555_handle_t board_fca9555(void) { return (void *)1; }
static int fca9555_read_input(fca9555_handle_t h, int port, uint8_t *out) {
    assert(h && port == 1);
    pthread_mutex_lock(&bus_mutex);
    if (block_read) { read_blocked = true; pthread_cond_signal(&bus_cond); }
    while (block_read) pthread_cond_wait(&bus_cond, &bus_mutex);
    pthread_mutex_unlock(&bus_mutex);
    atomic_fetch_add(&reads, 1);
    if (atomic_load(&read_error)) return ESP_FAIL;
    bool level = atomic_load(&down);
    if (atomic_load(&glitch_reads)) { atomic_fetch_sub(&glitch_reads, 1); level = true; }
    *out = level ? 0 : 0x80; return ESP_OK;
}
typedef enum { DEVICE_POWER_KEY_NONE, DEVICE_POWER_KEY_SHORT, DEVICE_POWER_KEY_LONG } device_power_key_action_t;
static bool s_ready = true;
static struct { uint8_t pending_events; } s_snap;
static bool s_batt_poll_reusable, s_charger_configured;
static int64_t s_batt_poll_us, s_charger_resume_us, s_charger_next_us;
static void poll_battery(void) {
    atomic_fetch_add(&battery_polls, 1);
    s_batt_poll_us = esp_timer_get_time(); s_batt_poll_reusable = true;
}
'''


def source():
    raw = DRIVER.read_text()
    code = UNIT + "\n".join(re.findall(r"^#define KEY_.*$", raw, re.M)) + "\n"
    code += raw[raw.index("static bool s_key_down;"):raw.index("static uint8_t bcd_to_dec")]
    for name in ("poll_key", "key_sampler_task", "device_power_key_sampler_start",
                 "device_power_key_sampler_enable", "device_power_poll", "device_power_drain_events",
                 "device_power_take_key_action", "device_power_take_key_short",
                 "wake_battery_snapshot_reusable", "device_power_take_key_wakeup",
                 "device_power_consume_key_press"):
        code += function(name, DRIVER) + "\n"
    return code


TEST = r'''
static void wait_reads(unsigned target) {
    int64_t deadline = esp_timer_get_time() + 2000000;
    while (atomic_load(&reads) < target && esp_timer_get_time() < deadline) usleep(1000);
    assert(atomic_load(&reads) >= target);
}
static void clean_key(void) {
    atomic_store(&down, false); atomic_store(&read_error, false);
    unsigned before = atomic_load(&reads); wait_reads(before + 2);
    device_power_consume_key_press();
}
static atomic_bool pause_done;
static void *pause_sampler(void *arg) {
    (void)arg; assert(device_power_key_sampler_enable(false));
    atomic_store(&pause_done, true); return NULL;
}
int main(void) {
    // 任务创建失败时，原始直读短按和长按仍可用。/ Failed creation retains direct short/long polling.
    atomic_store(&fail_create, true); assert(!device_power_key_sampler_start());
    assert(!device_power_key_sampler_enable(true));
    atomic_store(&down, true); assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    usleep(40000); atomic_store(&down, false);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_SHORT);
    // 对照：主循环200ms空窗内完整发生的短按确实丢失。/ Control: a complete press between 200ms polls is lost.
    atomic_store(&down, true); usleep(100000); atomic_store(&down, false); usleep(100000);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    puts("REPRO: direct polling misses a complete 100ms press between polls");

    atomic_store(&fail_create, false); assert(device_power_key_sampler_start());
    assert(!device_power_key_sampler_enable(true));
    clean_key();
    // 消费者阻塞时，按键状态仍被采集；相位和时长变化不依赖200ms读取节拍。
    // Capture while the consumer is blocked, varying phase and hold length independently of its 200ms cadence.
    const unsigned widths[] = {50, 80, 100, 150, 500};
    for (unsigned i = 0; i < sizeof(widths)/sizeof(widths[0]); ++i) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            clean_key(); unsigned battery_before = atomic_load(&battery_polls);
            usleep(phase * 3000); atomic_store(&down, true); usleep(widths[i] * 1000);
            atomic_store(&down, false); usleep(230000);
            assert(atomic_load(&battery_polls) == battery_before);
            assert(device_power_take_key_action() == DEVICE_POWER_KEY_SHORT);
            assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
        }
    }
    puts("PASS: 15 phase/width cases retain short presses across blocked consumers without battery polling");
    clean_key(); atomic_store(&glitch_reads, 1);
    wait_reads(atomic_load(&reads) + 4);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    atomic_store(&read_error, true); atomic_store(&down, true); usleep(100000);
    atomic_store(&down, false); atomic_store(&read_error, false); wait_reads(atomic_load(&reads) + 2);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);

    atomic_store(&down, true); usleep(1150000);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_LONG);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    atomic_store(&down, false); usleep(50000);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    clean_key(); atomic_store(&down, true); usleep(60000);
    assert(device_power_take_key_wakeup()); device_power_consume_key_press();
    usleep(1050000); assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    atomic_store(&down, false); usleep(50000);
    assert(!device_power_take_key_short());
    atomic_store(&down, true); usleep(80000); atomic_store(&down, false); usleep(50000);
    assert(device_power_take_key_short()); assert(!device_power_take_key_short());
    puts("PASS: glitches/read failures stay silent; long presses fire once; consumed wake holds/releases stay silent");

    // 独占或切页边界不遗留短按，也不把原有按住状态的释放变成新按键。
    // Exclusive/page boundaries discard pending shorts and never turn an existing hold's release into a new press.
    clean_key(); atomic_store(&down, true); usleep(70000);
    assert(device_power_key_sampler_enable(false));
    atomic_store(&down, false); assert(!device_power_key_sampler_enable(true));
    wait_reads(atomic_load(&reads) + 2);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    atomic_store(&down, true); usleep(80000); atomic_store(&down, false); usleep(50000);
    assert(device_power_key_sampler_enable(false)); assert(!device_power_key_sampler_enable(true));
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);

    // 暂停必须等在途硬件读取退场，返回后不能再清中断。/ Pause must join an in-flight read and prevent later IRQ-clearing reads.
    clean_key(); pthread_mutex_lock(&bus_mutex); block_read = true;
    while (!read_blocked) pthread_cond_wait(&bus_cond, &bus_mutex);
    pthread_mutex_unlock(&bus_mutex);
    pthread_t pauser; assert(pthread_create(&pauser, NULL, pause_sampler, NULL) == 0);
    usleep(30000); assert(!atomic_load(&pause_done));
    pthread_mutex_lock(&bus_mutex); block_read = false; pthread_cond_broadcast(&bus_cond);
    pthread_mutex_unlock(&bus_mutex); assert(pthread_join(pauser, NULL) == 0);
    unsigned paused_reads = atomic_load(&reads); usleep(50000);
    assert(atomic_load(&pause_done) && atomic_load(&reads) == paused_reads);
    assert(!device_power_key_sampler_enable(false));
    // 暂停时仍能由锁屏入口直读唤醒，再交还快采。/ The paused lock path retains direct wake polling before resuming sampling.
    atomic_store(&down, true); assert(device_power_take_key_wakeup());
    device_power_consume_key_press(); atomic_store(&down, false);
    assert(device_power_take_key_action() == DEVICE_POWER_KEY_NONE);
    assert(!device_power_key_sampler_enable(true)); wait_reads(atomic_load(&reads) + 2);
    assert(device_power_key_sampler_enable(false));
    // 同步停用可嵌套；保持之前的停用状态。/ Nested pauses restore the previous disabled state.
    bool previous = device_power_key_sampler_enable(false); assert(!previous);
    assert(!device_power_key_sampler_enable(previous));
    assert(device_power_key_sampler_start()); assert(!device_power_key_sampler_enable(true));
    wait_reads(atomic_load(&reads) + 2); assert(device_power_key_sampler_enable(false));
    puts("PASS: pause joins hardware reads, nested disable stays disabled, direct wake and task reuse work");
    return 0;
}
'''

with tempfile.TemporaryDirectory(dir=ROOT / "build") as folder:
    work = Path(folder)
    env, compiler = toolchain(work)
    c, exe = work / "power-key.c", work / "power-key"
    c.write_text(source() + TEST)
    subprocess.run(compiler + ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
                              "-fsanitize=address,undefined", "-pthread", str(c), "-o", str(exe)],
                   env=env, check=True, timeout=30)
    subprocess.run([str(exe)], env=env, check=True, timeout=30)
