#!/usr/bin/env python3
"""并发马达写入不能恢复已关断的高压位。/ A queued motor write must not restore cleared HV bits."""
from pathlib import Path
import os
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "components/board_support/board_metalio_e4p.c"
unit = r'''
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -1
#define TAG "test"
#define ESP_LOGW(...) ((void)0)
#define IO_MOTOR 3
#define IO_PWRUP 1
#define IO_VCOM_CTRL 0
#define IO_WAKEUP 2
#define IOE_BIT(n) (1u << (n))
static atomic_bool s_pwrup, s_vcom_ctrl, s_wakeup;
static atomic_uchar s_p0_extra;
static bool s_ioe_ok = true;
static pthread_mutex_t bus;
static _Thread_local bool motor_thread;
static atomic_bool waiting;
static atomic_uchar actual_out;
static int board_i2c_lock(int timeout) {
    assert(timeout == 50);
    if (motor_thread) waiting = true;
    return pthread_mutex_lock(&bus);
}
static void board_i2c_unlock(void) { assert(!pthread_mutex_unlock(&bus)); }
static int ioe_write(uint8_t reg, uint8_t value) {
    assert(reg==2); actual_out=value; return ESP_OK;
}
'''
for name in ("p0_value", "p0_write", "board_motor"):
    unit += function(name, BOARD) + "\n"
unit += r'''
static void* motor(void* unused) {
    (void)unused; motor_thread=true;
    assert(board_motor(true)==ESP_OK); return NULL;
}
int main(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&bus,&attr); pthread_mutexattr_destroy(&attr);
    for(unsigned i=0;i<200;++i) {
        s_pwrup=s_vcom_ctrl=s_wakeup=true;
        s_p0_extra=0; actual_out=7; waiting=false;
        assert(!pthread_mutex_lock(&bus));
        pthread_t thread; assert(!pthread_create(&thread,NULL,motor,NULL));
        while(!waiting) usleep(10);
        // 马达已排队，此时完成关断，再让马达取得总线。
        // Finish shutdown while the motor is queued, then let the motor acquire the bus.
        s_pwrup=s_vcom_ctrl=s_wakeup=false;
        assert(p0_write()==ESP_OK && !(actual_out&7));
        assert(!pthread_mutex_unlock(&bus)); assert(!pthread_join(thread,NULL));
        assert(actual_out==IOE_BIT(IO_MOTOR));
    }
    pthread_mutex_destroy(&bus);
    puts("PASS: 200 queued motor writes cannot restore cleared HV bits");
}
'''
with tempfile.TemporaryDirectory(prefix="board-hv-") as folder:
    source, binary = Path(folder) / "test.c", Path(folder) / "test"
    source.write_text(unit)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                    "-fsanitize=" + os.environ.get("HOST_TEST_SANITIZER", "address,undefined"),
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
