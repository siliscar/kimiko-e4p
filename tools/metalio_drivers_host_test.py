#!/usr/bin/env python3
"""中文：执行实际 Metalio 驱动，注入总线、寄存器和共享中断模型。
English: Execute Metalio drivers with injected bus, register and shared-IRQ models.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
ERRORS = '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_TIMEOUT -4
#define ESP_ERR_NOT_SUPPORTED -5
#define ESP_ERR_NO_MEM -6
#define ESP_ERR_NOT_FOUND -7
#define ESP_ERR_INVALID_RESPONSE -8
static inline const char *esp_err_to_name(int value) {(void)value; return "test";}
'''
I2C = '''#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct test_device *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef struct {int dev_addr_length; uint8_t device_address; unsigned scl_speed_hz, scl_wait_us;} i2c_device_config_t;
#define I2C_ADDR_BIT_LEN_7 0
int i2c_master_transmit(i2c_master_dev_handle_t,const uint8_t*,size_t,int);
int i2c_master_receive(i2c_master_dev_handle_t,uint8_t*,size_t,int);
int i2c_master_transmit_receive(i2c_master_dev_handle_t,const uint8_t*,size_t,uint8_t*,size_t,int);
int i2c_master_bus_add_device(void*,const i2c_device_config_t*,i2c_master_dev_handle_t*);
int i2c_master_bus_rm_device(i2c_master_dev_handle_t);
int i2c_master_probe(void*,uint8_t,int);
'''
GPIO = '''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_NUM_NC -1
#define GPIO_IS_VALID_GPIO(n) ((n)>=0 && (n)<64)
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_HIGH_LEVEL 3
#define GPIO_INTR_LOW_LEVEL 4
int gpio_config(const gpio_config_t*);
int gpio_set_level(int,int);
int gpio_get_level(int);
int gpio_set_intr_type(int,int);
int gpio_isr_handler_remove(int);
int gpio_install_isr_service(int);
int gpio_isr_handler_add(int,void(*)(void*),void*);
int gpio_wakeup_enable(int,int);
'''
FREERTOS = '''#pragma once
#include <pthread.h>
#include <stdint.h>
typedef unsigned TickType_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
#define portMAX_DELAY (~0u)
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
#define pdFALSE 0
'''
SEMAPHORE = '''#pragma once
#include "FreeRTOS.h"
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t*);
int xSemaphoreTakeRecursive(SemaphoreHandle_t,TickType_t);
int xSemaphoreGiveRecursive(SemaphoreHandle_t);
'''
MODEL = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "driver/gpio.h"
#include "board_i2c.h"
#include "freertos/semphr.h"
struct test_device { uint8_t address, pointer, regs[256]; bool latched[2]; };
static struct test_device devices[128];
static unsigned tx_count, rx_count, combined_count, delay_us, resets, removals;
static int fail_reg = -1, fail_read_reg = -1, fail_addr = -1;
static unsigned fail_combined;
static bool fail_after_write, lock_fail;
static unsigned fail_writes;
static int64_t now_us;
static gpio_config_t last_gpio;
static int irq_type, levels[64];
static bool shared_irq, stuck_irq;
static unsigned wake_calls, wake_fail_call;
static bool concurrent_case;
static _Thread_local bool contender;
static atomic_bool pointer_sent, contender_waiting;
static void test_log(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *lock) {
    pthread_mutexattr_t attr; assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(lock,&attr)); assert(!pthread_mutexattr_destroy(&attr)); return lock;
}
int xSemaphoreTakeRecursive(SemaphoreHandle_t lock,TickType_t ticks) {
    (void)ticks; if(lock_fail)return pdFALSE;
    if(concurrent_case && contender)atomic_store(&contender_waiting,true);
    assert(!pthread_mutex_lock(lock)); return pdTRUE;
}
int xSemaphoreGiveRecursive(SemaphoreHandle_t lock) {assert(!pthread_mutex_unlock(lock));return pdTRUE;}
int gpio_config(const gpio_config_t *cfg) {last_gpio=*cfg;return ESP_OK;}
int gpio_set_level(int pin,int level) {levels[pin]=level;return ESP_OK;}
int gpio_get_level(int pin) {
    if(shared_irq && pin==2)return !(stuck_irq || (devices[0x19].regs[0x31]&0x40) ||
                                  devices[0x20].latched[0] || devices[0x20].latched[1]);
    return levels[pin];
}
int gpio_set_intr_type(int pin,int type) {(void)pin;irq_type=type;return ESP_OK;}
int gpio_isr_handler_remove(int pin) {(void)pin;return ESP_OK;}
int gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}
int gpio_isr_handler_add(int pin,void(*handler)(void*),void *arg) {(void)pin;(void)handler;(void)arg;return ESP_OK;}
int gpio_wakeup_enable(int pin,int type) {assert(pin==2);irq_type=type;return ++wake_calls==wake_fail_call?ESP_FAIL:ESP_OK;}
void vTaskDelay(unsigned ms) {now_us+=(int64_t)ms*1000;}
void esp_rom_delay_us(unsigned us) {delay_us+=us;now_us+=us;}
int64_t esp_timer_get_time(void) {return now_us;}
int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,i2c_master_dev_handle_t *out) {
    assert(bus && cfg->scl_speed_hz==100000 && cfg->scl_wait_us==10000);
    devices[cfg->device_address].address=cfg->device_address;*out=&devices[cfg->device_address];return ESP_OK;
}
int i2c_master_bus_rm_device(i2c_master_dev_handle_t dev) {assert(dev);++removals;return ESP_OK;}
int i2c_master_probe(void *bus,uint8_t address,int timeout) {(void)timeout;assert(bus);return address==fail_addr?ESP_FAIL:ESP_OK;}
int i2c_master_transmit(i2c_master_dev_handle_t dev,const uint8_t *buf,size_t n,int timeout) {
    (void)timeout; assert(dev && n);++tx_count;
    if(dev->address==fail_addr)return ESP_FAIL;
    if(n==1){dev->pointer=buf[0];
        if(concurrent_case && !contender){atomic_store(&pointer_sent,true);while(!atomic_load(&contender_waiting)){} }
        return ESP_OK;
    }
    const bool fails=buf[0]==fail_reg && fail_writes;
    if(fails)--fail_writes;
    if(!fails || fail_after_write)for(size_t i=1;i<n;++i)dev->regs[(uint8_t)(buf[0]+i-1)]=buf[i];
    if(dev->address==0x6b && buf[0]==0x70 && buf[1]==0x44)dev->regs[0x70]=3;
    return fails?ESP_FAIL:ESP_OK;
}
int i2c_master_receive(i2c_master_dev_handle_t dev,uint8_t *buf,size_t n,int timeout) {
    (void)timeout;++rx_count;if(dev->address==fail_addr || dev->pointer==fail_read_reg)return ESP_FAIL;
    uint8_t reg=dev->pointer;
    if(dev->address==0x19)reg&=0x7f;
    for(size_t i=0;i<n;++i){
        const uint8_t addr=dev->address==0x20?((reg&~1u)|((reg+i)&1u)):(uint8_t)(reg+i);
        buf[i]=dev->regs[addr];if(dev->address==0x20 && addr<2)dev->latched[addr]=false;
        if(dev->address==0x19 && addr==0x31)dev->regs[addr]=0;
    }return ESP_OK;
}
int i2c_master_transmit_receive(i2c_master_dev_handle_t dev,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,int timeout) {
    ++combined_count;if(fail_combined){--fail_combined;return ESP_FAIL;}
    int err=i2c_master_transmit(dev,tx,tn,timeout);return err==ESP_OK?i2c_master_receive(dev,rx,rn,timeout):err;
}
static void model_reset(void) {
    memset(devices,0,sizeof(devices));for(unsigned i=0;i<128;++i)devices[i].address=i;
    tx_count=rx_count=combined_count=delay_us=resets=removals=0;
    fail_reg=fail_read_reg=fail_addr=-1;fail_combined=fail_writes=0;fail_after_write=lock_fail=false;
    now_us=0;irq_type=0;shared_irq=stuck_irq=false;wake_calls=wake_fail_call=0;memset(levels,1,sizeof(levels));
}
'''

BUS_TEST = r'''
static void *interloper(void *arg) {
    (void)arg;contender=true;while(!atomic_load(&pointer_sent)){}
    const uint8_t other[]={7};assert(board_i2c_transmit(&devices[0x55],other,1,50)==ESP_OK);return NULL;
}
int main(void) {
    model_reset();i2c_master_dev_handle_t dev;
    assert(board_i2c_add_device((void*)1,0x55,&dev)==ESP_OK);
    dev->regs[2]=0x42;uint8_t value=0;
    assert(board_i2c_read(dev,2,&value,1,50)==ESP_OK && value==0x42 && combined_count==1 && !delay_us);
    fail_combined=1;assert(board_i2c_read(dev,2,&value,1,50)==ESP_OK && value==0x42 && delay_us==200);
    fail_read_reg=2;assert(board_i2c_read(dev,2,&value,1,50)==ESP_FAIL);fail_read_reg=-1;
    lock_fail=true;unsigned before=tx_count;assert(board_i2c_read(dev,2,&value,1,50)==ESP_ERR_TIMEOUT && before==tx_count);lock_fail=false;
    assert(board_i2c_read(dev,2,NULL,1,50)==ESP_ERR_INVALID_ARG);
    concurrent_case=true;atomic_store(&pointer_sent,false);atomic_store(&contender_waiting,false);
    pthread_t worker;assert(!pthread_create(&worker,NULL,interloper,NULL));
    assert(board_i2c_read_stop(dev,2,&value,1,50)==ESP_OK && value==0x42);
    assert(!pthread_join(worker,NULL));assert(dev->pointer==7);concurrent_case=false;
    assert(board_i2c_lock(50)==ESP_OK);assert(board_i2c_read(dev,2,&value,1,50)==ESP_OK);board_i2c_unlock();
    puts("PASS: real bus lock, combined/STOP fallback, timeout, nested transfer, SCL stretching config and concurrent pointer isolation");
}
'''
IO_TEST = r'''
int main(void) {
    model_reset();fca9555_handle_t h=NULL;fca9555_config_t cfg=FCA9555_CONFIG_DEFAULT();cfg.i2c_addr=0x20;cfg.int_gpio=2;
    assert(fca9555_init((void*)1,&cfg,&h)==ESP_OK);
    const uint8_t values[]={0xa5,0x5a,0x20,0x14,0x02,0x01,0xc0,0xe3};
    memcpy(devices[0x20].regs,values,8);fca9555_map_t map={0};
    assert(fca9555_read_all(h,&map)==ESP_OK && map.input==0x5aa5 && map.output==0x1420 && map.invert==0x0102 && map.config==0xe3c0);
    assert(!combined_count && tx_count==8 && rx_count==8);
    uint8_t linear[8]={0};assert(fca9555_read_n(h,0,linear,8)==ESP_OK && !memcmp(linear,values,8));
    assert(fca9555_read_n(h,7,linear,2)==ESP_ERR_INVALID_ARG);
    fail_read_reg=4;memset(linear,0xaa,sizeof(linear));
    assert(fca9555_read_n(h,0,linear,8)==ESP_FAIL);for(int i=0;i<8;++i)assert(linear[i]==0xaa);
    fail_read_reg=-1;
    assert(fca9555_read_io(h,&map)==ESP_OK && map.output==0x1420 && map.config==0xe3c0);
    for(int reg=0;reg<8;++reg){fca9555_map_t before=map;fail_read_reg=reg;
        assert(fca9555_read_all(h,&map)==ESP_FAIL && !memcmp(&before,&map,sizeof(map)));}
    fail_read_reg=-1;devices[0x20].latched[0]=devices[0x20].latched[1]=true;
    assert(fca9555_clear_int(h)==ESP_OK && !devices[0x20].latched[0] && !devices[0x20].latched[1]);
    devices[0x20].latched[1]=true;fail_read_reg=0;
    assert(fca9555_clear_int(h)==ESP_FAIL && !devices[0x20].latched[1]);
    assert(fca9555_deinit(h)==ESP_OK && removals==1);
    puts("PASS: TCA register-pair hardware model, full/status maps, failed snapshot atomicity and both interrupt ports");
}
'''
TOUCH_TEST = r'''
static int reset_callback(void){++resets;vTaskDelay(310);return ESP_OK;}
static void point(unsigned slot,int x,int y,unsigned id,unsigned event){
    uint8_t *r=&devices[0x38].regs[3+slot*6];r[0]=(event<<6)|((x>>8)&15);r[1]=x;r[2]=(id<<4)|((y>>8)&15);r[3]=y;
}
int main(void){
    model_reset();devices[0x38].regs[0xa3]=0x64;
    cst836u_config_t cfg={.int_gpio=3,.rst_gpio=-1,.reset_fn=reset_callback};cst836u_handle_t h=NULL;
    assert(cst836u_init((void*)1,&cfg,&h)==ESP_OK && resets==1 && devices[0x38].regs[0xa4]==0);
    cst836u_touch_t t;devices[0x38].regs[2]=2;point(0,100,200,7,0);point(1,300,400,3,2);
    assert(cst836u_read(h,&t)==ESP_OK && t.touched && t.count==2 && t.points[0].id==7 && t.points[1].id==3 && t.points[1].active);
    devices[0x38].regs[2]=15;assert(cst836u_read(h,&t)==ESP_OK && !t.touched && t.count==0);
    devices[0x38].regs[2]=1;point(0,0,1400,0,0);assert(cst836u_read(h,&t)==ESP_OK && !t.touched);
    const int xs[]={100,200,300},keys[]={240,80,400};
    for(int i=0;i<3;++i){point(0,xs[i],1600,0,0);assert(cst836u_read(h,&t)==ESP_OK && t.touched && t.x==keys[i] && t.y==1500);}
    point(0,131,1600,0,0);assert(cst836u_read(h,&t)==ESP_OK && !t.touched);
    point(0,100,1569,0,0);assert(cst836u_read(h,&t)==ESP_OK && !t.touched);
    point(0,900,200,0,0);assert(cst836u_read(h,&t)==ESP_OK && t.touched && t.x==483 && t.y==900);
    assert(cst836u_set_mode(h,CST836U_MODE_DEEPSLEEP)==ESP_OK);unsigned before=combined_count;
    assert(cst836u_read(h,&t)==ESP_OK && !t.touched && combined_count==before);
    fail_reg=0xa4;fail_writes=1;assert(cst836u_wake(h)==ESP_FAIL && cst836u_get_mode(h)==CST836U_MODE_DEEPSLEEP);
    assert(cst836u_wake(h)==ESP_OK && cst836u_get_mode(h)==CST836U_MODE_NORMAL && resets==3);
    assert(cst836u_deinit(h)==ESP_OK);
    fail_reg=0xa4;fail_writes=1;h=NULL;assert(cst836u_init((void*)1,&cfg,&h)==ESP_FAIL && !h && removals==2);
    puts("PASS: FT hardware reset/mode init, dual tracking IDs, invalid frames, exact cover-key bounds, landscape and failed wake/init");
}
'''
TOUCH_THRESHOLD_MODEL = r'''
static unsigned threshold_reads, threshold_writes, tuning_reads;
static unsigned threshold_read_failures, threshold_write_failures;
static bool threshold_partial_write, threshold_ignore_write;
static uint8_t module_threshold;
static esp_err_t threshold_read(i2c_master_dev_handle_t dev,uint8_t reg,uint8_t *data,size_t n,int timeout){
    if(reg==0x00 || reg==0x9f || reg==0xa0 || reg==0x80)++tuning_reads;
    if(reg==0x80){
        assert(n==1);++threshold_reads;
        if(threshold_read_failures&(1u<<(threshold_reads-1)))return ESP_FAIL;
    }
    return board_i2c_read(dev,reg,data,n,timeout);
}
static esp_err_t threshold_transmit(i2c_master_dev_handle_t dev,const uint8_t *data,size_t n,int timeout){
    assert(n==2 && (data[0]==0xa4 || data[0]==0x80));
    if(data[0]==0x80){
        ++threshold_writes;
        if(threshold_write_failures&(1u<<(threshold_writes-1))){
            if(threshold_partial_write)assert(board_i2c_transmit(dev,data,n,timeout)==ESP_OK);
            return ESP_FAIL;
        }
        if(threshold_ignore_write && threshold_writes==1)return ESP_OK;
    }
    return board_i2c_transmit(dev,data,n,timeout);
}
#define board_i2c_read threshold_read
#define board_i2c_transmit threshold_transmit
'''
TOUCH_THRESHOLD_TEST = r'''
static int threshold_reset(void){
    ++resets;devices[0x38].regs[0x80]=module_threshold;devices[0x38].regs[0xa4]=1;
    vTaskDelay(310);return ESP_OK;
}
static void threshold_fixture(uint8_t base){
    model_reset();module_threshold=base;
    threshold_reads=threshold_writes=tuning_reads=0;
    threshold_read_failures=threshold_write_failures=0;
    threshold_partial_write=threshold_ignore_write=false;
    uint8_t *r=devices[0x38].regs;
    r[0xa3]=0x64;r[0x9f]=0x26;r[0xa0]=2;
    r[0xa9]=0x19;r[0x85]=0xa0;r[0x86]=1;r[0x88]=8;
}
static cst836u_handle_t threshold_open(void){
    cst836u_config_t cfg={.int_gpio=3,.rst_gpio=-1,.reset_fn=threshold_reset};
    cst836u_handle_t h=NULL;
    assert(cst836u_init((void*)1,&cfg,&h)==ESP_OK && h);
    const uint8_t *r=devices[0x38].regs;
    assert(r[0xa9]==0x19 && r[0x85]==0xa0 && r[0x86]==1 && r[0x88]==8);
    return h;
}
static unsigned scaled_threshold(unsigned base){
    return (base*CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT+99)/100;
}
int main(void){
    const uint8_t bases[]={0,1,2,3,4,5,22,40,187,255};
    for(unsigned i=0;i<sizeof(bases);++i){
        threshold_fixture(bases[i]);cst836u_handle_t h=threshold_open();
        assert(devices[0x38].regs[0x80]==scaled_threshold(bases[i]));
        assert(threshold_writes==(scaled_threshold(bases[i])!=bases[i]));
        if(CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT==100)assert(!tuning_reads);
        assert(cst836u_deinit(h)==ESP_OK);
    }
    if(CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT==100){
        puts("PASS: FT threshold 100% preserves reset values with no additional register access");return 0;
    }
    // 每次复位重新取模组值，唤醒不能把阈值越降越低。/ Resets re-read module values; wakes must not compound tuning.
    threshold_fixture(40);cst836u_handle_t h=threshold_open();
    for(int i=0;i<3;++i){
        assert(cst836u_set_mode(h,CST836U_MODE_DEEPSLEEP)==ESP_OK);
        assert(cst836u_wake(h)==ESP_OK && devices[0x38].regs[0x80]==scaled_threshold(40));
    }
    module_threshold=80;
    assert(cst836u_wake(h)==ESP_OK && devices[0x38].regs[0x80]==scaled_threshold(80));
    assert(cst836u_deinit(h)==ESP_OK);
    // 型号、工作模式和初始读取不可信时，保留原值。/ Preserve defaults for unknown chips/modes or a failed initial read.
    const uint8_t guards[]={0xa3,0x9f,0xa0,0x00};
    for(unsigned i=0;i<sizeof(guards);++i){
        threshold_fixture(40);devices[0x38].regs[guards[i]]=0x40;h=threshold_open();
        assert(!threshold_writes && devices[0x38].regs[0x80]==40);
        assert(cst836u_deinit(h)==ESP_OK);
    }
    for(int i=0;i<3;++i){
        threshold_fixture(40);fail_read_reg=i==0?0x00:(i==1?0x9f:0xa0);h=threshold_open();
        assert(!threshold_writes && devices[0x38].regs[0x80]==40);
        assert(cst836u_deinit(h)==ESP_OK);
    }
    threshold_fixture(40);threshold_read_failures=1;h=threshold_open();
    assert(!threshold_writes && devices[0x38].regs[0x80]==40);assert(cst836u_deinit(h)==ESP_OK);
    // 写失败可能已经生效；不论读回不符或读错，均恢复并验证原值。
    // Failed writes may take effect; restore and verify on errors or mismatched readback.
    for(int i=0;i<4;++i){
        threshold_fixture(40);
        if(i<2){threshold_write_failures=1;threshold_partial_write=i==1;}
        if(i==2)threshold_ignore_write=true;
        if(i==3)threshold_read_failures=2;
        h=threshold_open();assert(threshold_writes==2 && devices[0x38].regs[0x80]==40);
        assert(cst836u_deinit(h)==ESP_OK);
    }
    threshold_fixture(40);threshold_read_failures=2;threshold_write_failures=2;
    threshold_partial_write=true;h=threshold_open();
    assert(threshold_writes==2 && devices[0x38].regs[0x80]==40);assert(cst836u_deinit(h)==ESP_OK);
    // 恢复无法验证时不得宣称启动或唤醒成功。/ Unverified recovery must fail initialization or wake.
    threshold_fixture(40);threshold_read_failures=2;threshold_write_failures=2;
    cst836u_config_t cfg={.int_gpio=3,.rst_gpio=-1,.reset_fn=threshold_reset};h=NULL;
    assert(cst836u_init((void*)1,&cfg,&h)!=ESP_OK && !h && removals==1);
    threshold_fixture(40);threshold_read_failures=2|4;h=NULL;
    assert(cst836u_init((void*)1,&cfg,&h)!=ESP_OK && !h && removals==1);
    threshold_fixture(40);h=threshold_open();
    assert(cst836u_set_mode(h,CST836U_MODE_DEEPSLEEP)==ESP_OK);
    threshold_reads=threshold_writes=0;threshold_read_failures=2;threshold_write_failures=2;
    assert(cst836u_wake(h)!=ESP_OK && cst836u_get_mode(h)==CST836U_MODE_DEEPSLEEP);
    threshold_reads=threshold_writes=threshold_read_failures=threshold_write_failures=0;
    assert(cst836u_wake(h)==ESP_OK && devices[0x38].regs[0x80]==scaled_threshold(40));
    assert(cst836u_get_mode(h)==CST836U_MODE_NORMAL);
    threshold_reads=threshold_writes=0;threshold_read_failures=2;threshold_write_failures=2;
    assert(cst836u_wake(h)!=ESP_OK && cst836u_get_mode(h)==CST836U_MODE_DEEPSLEEP);
    cst836u_touch_t t;unsigned before=combined_count;
    assert(cst836u_read(h,&t)==ESP_OK && !t.touched && combined_count==before);
    assert(cst836u_deinit(h)==ESP_OK);
    puts("PASS: FT threshold scaling, chip/mode guards, reset/wake reapply, partial writes, readback and verified rollback");
}
'''
ACCEL_TEST = r'''
int main(void){
    model_reset();struct sc7a20h_dev_t storage={.dev=&devices[0x19],.int1_gpio=2,.cfg=SC7A20H_CONFIG_IDLE};
    sc7a20h_handle_t h=&storage;sc7a20h_sensor_config_t saved=h->cfg;devices[0x19].regs[0x1f]=0x5a;
    assert(sc7a20h_arm_pickup_wake(h,NULL)==ESP_OK);
    uint8_t *r=devices[0x19].regs;
    assert(r[0x1f]==0x5a && r[0x20]==0x57 && r[0x21]==1 && r[0x22]==0x40 && r[0x23]==0x88 && r[0x24]==8 && r[0x25]==3);
    assert(r[0x30]==0x2a && r[0x32]==0x10 && r[0x33]==0 && !memcmp(&h->cfg,&saved,sizeof(saved)));
    assert(sc7a20h_config_light_sleep_wakeup(h)==ESP_OK && irq_type==GPIO_INTR_LOW_LEVEL);
    assert(last_gpio.pull_up_en && !last_gpio.pull_down_en);
    assert(sc7a20h_int1_begin(h)==ESP_OK && last_gpio.intr_type==GPIO_INTR_NEGEDGE);
    r[0x31]=0x40;assert(sc7a20h_power_down(h)==ESP_OK && !h->powered);
    assert(r[0x20]==0x10 && !r[0x22] && !r[0x24] && !r[0x25] && !r[0x30] && !r[0x31]);
    fail_reg=0x25;fail_writes=1;assert(sc7a20h_arm_pickup_wake(h,NULL)==ESP_FAIL && !h->powered);
    sc7a20h_motion_cfg_t selected={.axis_mask=SC7A20H_AXIS_X,.ths_mg=320,.duration=3};
    assert(sc7a20h_arm_pickup_wake(h,&selected)==ESP_OK && r[0x30]==2 && r[0x32]==20 && r[0x33]==3);
    fail_reg=0x30;fail_writes=1;assert(sc7a20h_power_down(h)==ESP_FAIL && !r[0x22] && !r[0x25]);
    model_reset();sc7a20h_config_t init_cfg=SC7A20H_DEVICE_CONFIG_DEFAULT();sc7a20h_handle_t owned=NULL;
    devices[0x19].regs[0x0f]=0x11;init_cfg.int1_gpio=2;
    assert(sc7a20h_init((void*)1,&init_cfg,&owned)==ESP_OK && owned);
    assert(sc7a20h_arm_pickup_wake(owned,NULL)==ESP_OK);
    assert(sc7a20h_deinit(owned)==ESP_OK && devices[0x19].regs[0x20]==0x10 && !devices[0x19].regs[0x25] && removals==1);
    owned=NULL;fail_reg=0x23;fail_writes=1;
    assert(sc7a20h_init((void*)1,&init_cfg,&owned)==ESP_FAIL && !owned && removals==2 && devices[0x19].regs[0x20]==0x10);
    puts("PASS: SC vendor wake registers, shared active-low GPIO, pull-up, falling edge, silence without PD and route failures");
}
'''

def power_source():
    path = ROOT / 'components/device_power/device_power_metalio.c'
    text = path.read_text()
    defines = '\n'.join(line for line in text.splitlines() if re.match(r'#define (?:CX_|PCF_|I2C_TIMEOUT_MS|TAG)', line))
    code = '#include <time.h>\n#include <sys/time.h>\n#include "device_power.h"\n#include "esp_log.h"\n' + defines + '\nstatic i2c_master_dev_handle_t s_cx, s_rtc;\n'
    code += 'static bool s_charger_configured, s_charger_uv_shutdown;\nstatic int64_t s_charger_next_us, s_charger_resume_us;\n'
    code += r"""
static bool s_ready=true;
static pmu_snapshot_t s_snap;
static unsigned clock_sets, uv_log_events;
void device_power_batt_log_event(device_power_batt_log_event_t event) { assert(event == DEVICE_POWER_BATT_LOG_UNDERVOLTAGE); ++uv_log_events; }
static time_t clock_value;
static int test_settimeofday(const struct timeval *tv,const void *tz){assert(!tz);++clock_sets;clock_value=tv->tv_sec;return 0;}
#define settimeofday test_settimeofday
"""
    for name in ['bcd_to_dec','dec_to_bcd','dev_read','dev_write','cx_update_bits','cx_set_vreg_mv',
                 'cx_set_ichg_ma','cx_unlock_private','cx_charger_setup','cx_charger_park','cx_charger_poll','rtc_init','rtc_read','rtc_write','rtc_unix_utc','device_power_cmd']:
        code += function(name, path) + '\n'
    return code

POWER_TEST = r'''
static void reset_power(void){model_reset();s_cx=&devices[0x6b];s_rtc=&devices[0x51];s_charger_configured=true;s_charger_uv_shutdown=false;s_charger_resume_us=s_charger_next_us=0;}
static void charge(int64_t now,int mv,int ma,bool status_ok,int status){cx_charger_poll(now,true,mv,true,ma,status_ok,status);}
int main(void){
    reset_power();assert(cx_charger_setup()==ESP_OK);
    uint8_t *r=devices[0x6b].regs;
    assert(((((r[2]>>6)&3)|((r[3]&15)<<2))*80)==960);
    assert(((((r[6]>>4)&15)|((r[7]&15)<<4))*20)==960);
    charge(0,4400,10,true,1);assert((r[0x14]&4) && s_charger_next_us==10000000);
    charge(10000000,4200,10,true,1);assert(!(r[0x14]&4) && s_charger_next_us==12000000);
    charge(12000000,4200,0,true,1);assert(!(r[0x16]&0x20) && s_charger_resume_us==12200000);
    charge(12199999,4200,0,true,1);assert(!(r[0x16]&0x20));
    fail_reg=0x16;fail_writes=1;charge(12200000,4200,0,true,1);assert(s_charger_resume_us);
    charge(12400000,4200,0,true,1);assert((r[0x16]&0x20) && !s_charger_resume_us);
    reset_power();r=devices[0x6b].regs;charge(0,3100,-10,true,0);assert(r[0x18]==1 && s_charger_uv_shutdown && uv_log_events==1);
    charge(2000000,3100,-10,true,0);assert(!s_charger_resume_us);
    charge(4000000,3100,0,true,1);assert(s_charger_resume_us==4200000);
    charge(4200000,3100,0,true,1);assert(!s_charger_resume_us && (r[0x16]&0x20));
    reset_power();charge(0,3000,0,true,1);assert(!s_charger_uv_shutdown);
    reset_power();charge(0,3000,0,false,0);assert(!s_charger_uv_shutdown && !s_charger_resume_us);
    reset_power();cx_charger_poll(0,false,0,true,0,true,0);assert(!tx_count && !s_charger_uv_shutdown);
    reset_power();fail_reg=0x86;fail_writes=1;assert(cx_charger_setup()==ESP_FAIL && !(devices[0x6b].regs[0x16]&0x20));
    const uint8_t failed_regs[]={0x02,0x03,0x04,0x05,0x06,0x07,0x14,0x15,0x16,0x18,0x1a,0x23,0x24,0x70,0x86,0x3a,0x46};
    for(unsigned i=0;i<sizeof(failed_regs);++i){reset_power();r=devices[0x6b].regs;r[0x16]=0x20;
        fail_reg=failed_regs[i];fail_writes=1;fail_after_write=true;
        assert(cx_charger_setup()!=ESP_OK && !(r[0x16]&0x20) && (r[0x14]&4));}
    reset_power();r=devices[0x6b].regs;s_charger_resume_us=200000;
    assert(cx_charger_park()==ESP_OK && !s_charger_resume_us && (r[0x16]&0x20) && (r[0x14]&4));
    reset_power();s_charger_resume_us=200000;fail_reg=0x16;fail_writes=1;
    assert(cx_charger_park()==ESP_FAIL && s_charger_resume_us);assert(cx_charger_park()==ESP_OK && !s_charger_resume_us);
    reset_power();s_charger_configured=false;assert(cx_charger_park()==ESP_OK && !tx_count);
    reset_power();s_rtc->regs[0]=0x20;s_rtc->regs[1]=0x0c;assert(rtc_init()==ESP_OK && !s_rtc->regs[0] && !(s_rtc->regs[1]&0x0c));
    const time_t values[]={946684800LL,1709164800LL,1793232000LL,4102444799LL};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i){assert(rtc_write(values[i])==ESP_OK);struct tm tm;bool valid=false;assert(rtc_read(&tm,&valid)==ESP_OK && valid && rtc_unix_utc(&tm)==values[i]);}
    const char *zones[]={"UTC0","CST-8","EST5EDT","UTC-14"};
    for(unsigned z=0;z<4;++z){assert(!setenv("TZ",zones[z],1));tzset();
        for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i){assert(rtc_write(values[i])==ESP_OK);
            unsigned before=clock_sets;assert(device_power_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_OK &&
                clock_sets==before+1 && clock_value==values[i] && s_snap.unix_sec==(uint32_t)values[i]);}}
    s_rtc->regs[2]|=0x80;unsigned before=clock_sets;
    assert(device_power_cmd(PMU_CMD_TIME_GET,NULL,0)==ESP_ERR_INVALID_STATE && !s_snap.time_ok && clock_sets==before);
    s_rtc->regs[2]|=0x80;struct tm tm;bool valid=true;assert(rtc_read(&tm,&valid)==ESP_OK && !valid);
    s_rtc->regs[2]=0x6a;assert(rtc_read(&tm,&valid)==ESP_OK && !valid);
    s_rtc->regs[2]=0;s_rtc->regs[5]=0x31;s_rtc->regs[7]=0x02;s_rtc->regs[8]=0x26;assert(rtc_read(&tm,&valid)==ESP_OK && !valid);
    fail_reg=0;fail_writes=1;assert(rtc_init()==ESP_FAIL);
    puts("PASS: CX defaults/quantization, termination, nonblocking recharge/recovery, UV/input/error gates; RTC STOP, flags, UTC/leap/BCD/VL/date validity");
}
'''


def wake_poll_source():
    path = ROOT / 'components/device_power/device_power_metalio.c'
    text = path.read_text()
    code = power_source() + '#include "' + str(ROOT / 'components/device_power/battery_log.h') + '"\n#include "fca9555.h"\n'
    code += '\n'.join(line for line in text.splitlines()
                      if re.match(r'#define (?:BATT_|BQ_REG_|KEY_)', line)) + '\n'
    code += re.search(r'static const int32_t s_soc_curve\[.*?\};', text, re.S)[0] + '\n'
    code += re.search(r'typedef struct \{[^}]*\} batt_filter_t;', text, re.S)[0] + '\n'
    start = text.index('static batt_filter_t s_batt_filter;')
    code += text[start:text.index('static esp_err_t rtc_init', start)]
    tca = ROOT / 'components/fca9555/fca9555.c'
    raw = tca.read_text()
    start = raw.index('struct fca9555_dev_t {')
    code += raw[start:raw.index('};', start) + 2] + '\n'
    for name in ('port_ok', 'xfer_read', 'fca9555_read_input'):
        code += function(name, tca) + '\n'
    code += r'''
static i2c_master_dev_handle_t s_bq;
static struct fca9555_dev_t wake_ioe={.dev=&devices[0x20],.timeout_ms=100};
static fca9555_handle_t board_fca9555(void){return &wake_ioe;}
static bool s_key_down,s_key_long_fired,s_key_short_pending,s_key_long_pending;
static int64_t s_key_down_us;
static bool key_locked,logging;
static unsigned observations;
static void key_lock(void){assert(!key_locked);key_locked=true;}
static void key_unlock(void){assert(key_locked);key_locked=false;}
static batt_log_state_t s_batt_log;
static portMUX_TYPE s_batt_log_mux=portMUX_INITIALIZER_UNLOCKED;
static int8_t s_batt_ic_soc;
static int64_t s_batt_ic_us;
static bool batt_log_enabled(void){return logging;}
static int64_t batt_log_wall(void){return 0;}
static void batt_log_notify(void){}
static int32_t s_batt_persisted_permille=-1;
static void batt_save(int32_t value){(void)value;}
void batt_log_observe(batt_log_state_t *state,const batt_log_sample_t *sample){
    assert(state==&s_batt_log && sample);++observations;
}
'''
    for name in ('bq_read_voltage', 'bq_read_current', 'bq_read_soc',
                 'batt_compensated_mv', 'batt_curve_soc', 'batt_filter_step', 'batt_full_now',
                 'poll_key', 'poll_battery', 'wake_battery_snapshot_reusable',
                 'device_power_poll', 'device_power_take_key_wakeup'):
        code += function(name, path) + '\n'
    return code


WAKE_POLL_TEST = r'''
static void reset_wake_poll(void){
    model_reset();now_us=1000000;s_ready=true;
    s_bq=&devices[0x55];s_cx=&devices[0x6b];s_charger_configured=true;
    s_charger_resume_us=0;s_charger_next_us=10000000;s_charger_uv_shutdown=false;
    memset(&s_snap,0,sizeof(s_snap));memset(&s_batt_filter,0,sizeof(s_batt_filter));
    s_batt_poll_reusable=false;s_batt_poll_us=0;s_batt_saved_permille=-1;
    s_batt_full_since_us=-1;s_batt_last_save_us=now_us;s_batt_charging_last=false;
    s_key_down=s_key_long_fired=s_key_short_pending=s_key_long_pending=false;
    key_locked=logging=false;observations=0;
    devices[0x20].regs[1]=0xff;
    devices[0x55].regs[8]=0x3c;devices[0x55].regs[9]=0x0f;
}
int main(void){
    reset_wake_poll();assert(!device_power_take_key_wakeup() && s_batt_poll_reusable);
    unsigned before=combined_count,reads=rx_count;
    assert(!device_power_take_key_wakeup());
    assert(combined_count==before && rx_count==reads+1);
    now_us=s_batt_poll_us+KEY_WAKE_BATT_REUSE_US-1;
    assert(wake_battery_snapshot_reusable(now_us));
    ++now_us;assert(!wake_battery_snapshot_reusable(now_us));
    assert(!device_power_take_key_wakeup() && combined_count>before);
    now_us+=2000000;before=combined_count;
    assert(!device_power_take_key_wakeup() && combined_count>before);
    before=combined_count;assert(device_power_poll()==ESP_OK && combined_count>before);
    before=combined_count;assert(device_power_poll()==ESP_OK && combined_count>before);
    reset_wake_poll();poll_battery();before=combined_count;
    devices[0x20].regs[1]&=0x7f;assert(device_power_take_key_wakeup() && s_key_down);
    assert(combined_count>before);
    reset_wake_poll();poll_battery();s_key_short_pending=true;before=combined_count;
    assert(device_power_take_key_wakeup() && !s_key_short_pending && combined_count==before);
    reset_wake_poll();poll_battery();now_us=s_batt_poll_us-1;before=combined_count;
    assert(!device_power_take_key_wakeup() && combined_count>before);
    reset_wake_poll();poll_battery();s_charger_next_us=now_us;before=combined_count;
    assert(!device_power_take_key_wakeup() && combined_count>before);
    reset_wake_poll();poll_battery();s_charger_resume_us=now_us;before=combined_count;
    assert(!device_power_take_key_wakeup() && combined_count>before && !s_charger_resume_us);
    reset_wake_poll();poll_battery();s_charger_resume_us=now_us+200000;before=combined_count;
    assert(!device_power_take_key_wakeup() && combined_count==before);
    now_us=s_charger_resume_us;assert(!device_power_take_key_wakeup() && !s_charger_resume_us);
    for(unsigned field=0;field<4;++field){
        reset_wake_poll();
        if(field==0)fail_read_reg=BQ_REG_VOLTAGE;
        if(field==1)fail_read_reg=BQ_REG_CURRENT;
        if(field==2)fail_read_reg=CX_REG_STATUS1;
        if(field==3)devices[0x55].regs[9]=0;
        assert(!device_power_take_key_wakeup() && !s_batt_poll_reusable);
        fail_read_reg=-1;devices[0x55].regs[9]=0x0f;before=combined_count;
        assert(!device_power_take_key_wakeup() && combined_count>before && s_batt_poll_reusable);
    }
    reset_wake_poll();logging=true;poll_battery();before=observations;
    assert(!device_power_take_key_wakeup() && observations==before);
    now_us+=2000000;assert(!device_power_take_key_wakeup() && observations==before+1);
    reset_wake_poll();s_ready=false;before=rx_count;
    assert(!device_power_take_key_wakeup() && rx_count==before);
    puts("PASS: real wake key/battery/charger paths, adjacent sample reuse, exact expiry, key reads, active polls, error retries, clock rollback, charger deadlines and logging");
}
'''


def sleep_source(diagnostics=True):
    path = ROOT / 'main/power/sleep.c'
    text = path.read_text()
    defines = '\n'.join(line for line in text.splitlines() if line.startswith('#define APP_LOCK_'))
    code = '#define CONFIG_APP_SLEEP_DIAGNOSTICS ' + str(int(diagnostics)) + '\n'
    code += '#include "' + str(ROOT/'components/sc7a20h/sc7a20h.c') + '"\n' + defines + '\n'
    code += '#include "fca9555.h"\n'
    tca = ROOT/'components/fca9555/fca9555.c'
    tca_text = tca.read_text()
    start = tca_text.index('struct fca9555_dev_t {')
    code += tca_text[start:tca_text.index('};', start)+2] + '\n'
    for name in ('port_ok', 'xfer_read', 'fca9555_read_input'):
        code += function(name, tca) + '\n'
    start = text.index('typedef enum {\n    SLEEP_TRACE_KEY')
    code += text[start:text.index('#if CONFIG_APP_SLEEP_DIAGNOSTICS\n//', start)]
    code += r"""
#define BOARD_IOE_INT_GPIO 2
#define ESP_SLEEP_WAKEUP_TIMER 4
#define ESP_SLEEP_WAKEUP_GPIO 7
#define TAG "sleep-test"
typedef enum {APP_WAKE_NONE,APP_WAKE_KEY,APP_WAKE_PICKUP,APP_WAKE_TIMEOUT} app_wake_source_t;
typedef struct {bool jobs_ready,ble_stopped;} app_lifecycle_light_t;
static bool jobs_ready=true, ble_stopped=true, pickup_setting=true, key_pending;
static unsigned suspend_count,resume_count,sleep_count,key_polls,cache_restores,last_wake;
static int scenario;
static uint32_t wake_causes;
static bool parked, no_ioe;
static struct fca9555_dev_t ioe_storage={.dev=&devices[0x20],.int_gpio=2,.timeout_ms=100};
static fca9555_handle_t board_fca9555(void){return no_ioe?NULL:&ioe_storage;}
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static uint8_t saved_blob[512],pending_blob[512];
static size_t saved_size,pending_size;
static unsigned nvs_reads,nvs_writes,nvs_commits,nvs_closes,live_logs,saved_logs;
static int nvs_fault;
static char summary[2048];
static int nvs_open(const char *ns,int mode,nvs_handle_t *handle){
    assert(!parked && !strcmp(ns,"sleep_diag"));
    if(mode==NVS_READONLY)++nvs_reads;else {assert(mode==NVS_READWRITE);++nvs_writes;}
    if(nvs_fault==1)return ESP_FAIL;
    *handle=mode;return ESP_OK;
}
static int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
    assert(h==NVS_READONLY&&!strcmp(key,"last"));
    if(nvs_fault==4||!saved_size||saved_size>*size)return ESP_FAIL;
    memcpy(out,saved_blob,saved_size);*size=saved_size;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){
    assert(!parked&&h==NVS_READWRITE&&!strcmp(key,"last")&&size<=sizeof(pending_blob));
    if(nvs_fault==2)return ESP_FAIL;
    memcpy(pending_blob,data,size);pending_size=size;return ESP_OK;
}
static int nvs_commit(nvs_handle_t h){
    assert(!parked&&h==NVS_READWRITE);++nvs_commits;
    now_us+=70000;
    if(nvs_fault==3)return ESP_FAIL;
    memcpy(saved_blob,pending_blob,pending_size);saved_size=pending_size;return ESP_OK;
}
static void nvs_close(nvs_handle_t h){assert(h==NVS_READONLY||h==NVS_READWRITE);++nvs_closes;}
static int esp_app_get_elf_sha256(char *out,size_t size){assert(size==17);memcpy(out,"0123456789abcdef",17);return 17;}
static void sleep_test_log(const char *tag,const char *fmt,...) __attribute__((format(printf,2,3)));
static void sleep_test_log(const char *tag,const char *fmt,...){
    (void)tag;
    if(strncmp(fmt,"SLEEP_SUMMARY ",14))return;
    assert(!parked);va_list args;va_start(args,fmt);vsnprintf(summary,sizeof(summary),fmt,args);va_end(args);
    if(strstr(summary,"origin=live"))++live_logs;else if(strstr(summary,"origin=saved"))++saved_logs;
}
#undef ESP_LOGI
#define ESP_LOGI(...) sleep_test_log(__VA_ARGS__)
enum { DEVICE_POWER_BATT_LOG_LOCK=128, DEVICE_POWER_BATT_LOG_WAKE=256 };
static unsigned log_locks, log_wakes;
static void device_power_batt_log_event(int event){if(event==128)++log_locks;else {assert(event==256);++log_wakes;}}
static bool key_sampling=true;
static uint64_t timer_us;
static bool device_power_key_sampler_enable(bool enabled){bool previous=key_sampling;key_sampling=enabled;return previous;}
static app_lifecycle_light_t app_lifecycle_prepare_light(void){return (app_lifecycle_light_t){jobs_ready,ble_stopped};}
static void cache_wake_restore_snapshot(void){}
static void cache_wake_restore_apply(void){++cache_restores;}
static bool app_settings_pickup_wake(void){return pickup_setting;}
static void app_settings_set_last_wake(uint8_t value){last_wake=value;}
static void periph_suspend(bool off){assert(!off&&!key_sampling);++suspend_count;parked=true;}
static void periph_resume(void){assert(log_locks==1 && log_wakes==1);++resume_count;parked=false;now_us+=30000;}
static int gpio_wakeup_disable(int pin){assert(pin==2);return ESP_OK;}
static void board_clear_ioe_int(void){devices[0x20].latched[0]=devices[0x20].latched[1]=false;}
static bool device_power_take_key_wakeup(void){
    assert(!key_sampling);
    assert(++key_polls<10000);
    if(scenario==1 && key_polls==1)devices[0x19].regs[0x31]=0x40;
    if(scenario==2 && key_polls==1){devices[0x19].regs[0x31]=0x40;devices[0x20].latched[1]=true;}
    if(scenario==4 && key_polls==3)key_pending=true;
    if(scenario>=9 && scenario<=16 && key_polls>1)devices[0x20].regs[1]=0xff;
    bool key=key_pending;key_pending=false;return key;
}
static int esp_sleep_enable_timer_wakeup(uint64_t usec){assert(usec>0&&usec<=APP_LOCK_KEY_POLL_US);timer_us=usec;return scenario==7?ESP_FAIL:ESP_OK;}
static int esp_sleep_disable_wakeup_source(int source){assert(source==ESP_SLEEP_WAKEUP_TIMER);return ESP_OK;}
static int app_s31_light_sleep_start(void){
    assert(!key_sampling);
    ++sleep_count;assert(sleep_count<2000 && ble_stopped);
    assert(!nvs_reads&&!nvs_writes&&!nvs_commits&&!live_logs&&!saved_logs);
    now_us+=scenario>=20?(int64_t)timer_us:((scenario==3 && sleep_count==1)?10000:100000);
    wake_causes=1UL<<ESP_SLEEP_WAKEUP_TIMER;
    if(scenario>=9 && scenario<=16){
        if(sleep_count==1){
            wake_causes=1UL<<ESP_SLEEP_WAKEUP_GPIO;
            if(scenario==9||scenario==12||scenario==14||scenario==15)devices[0x20].regs[1]=0x7f;
            if(scenario==10)devices[0x19].regs[0x31]=0x40;
            if(scenario==13)fail_read_reg=1;
            if(scenario==14)wake_causes|=1UL<<ESP_SLEEP_WAKEUP_TIMER;
            if(scenario==15)wake_causes=1UL<<ESP_SLEEP_WAKEUP_TIMER;
            if(scenario==16)wake_causes=1UL<<3;
        }else {key_pending=true;fail_read_reg=-1;}
        return scenario==12?ESP_FAIL:ESP_OK;
    }
    if(scenario==0 || scenario==6)key_pending=true;
    else if(scenario==21)key_pending=true;
    else if(scenario==22)devices[0x19].regs[0x31]=0x40;
    else if(scenario<8)devices[0x19].regs[0x31]=0x40;
    return scenario==8?ESP_FAIL:ESP_OK;
}
static uint32_t esp_sleep_get_wakeup_causes(void){return wake_causes;}
"""
    code += '#if CONFIG_APP_SLEEP_DIAGNOSTICS\n' + function('sleep_trace_log', path) + '\n#endif\n'
    for name in ['sleep_trace_replay','sleep_trace_record','sleep_trace_finish','lock_capture_power_key',
                 'lock_arm_ioe_wakeup','pickup_ack','pickup_ia','pickup_wait_quiet','light_sleep_wait_until','app_light_sleep_wait']:
        code += function(name,path)+'\n'
    return code

SLEEP_TEST = r"""
static void reset_sleep(int next){
    model_reset();now_us=1000;shared_irq=true;scenario=next;
    jobs_ready=ble_stopped=pickup_setting=true;key_pending=false;
    key_sampling=true;
    wake_causes=0;parked=no_ioe=false;
    suspend_count=resume_count=sleep_count=key_polls=cache_restores=last_wake=0;
    log_locks=log_wakes=0;timer_us=0;
    nvs_fault=0;saved_size=pending_size=0;
    nvs_reads=nvs_writes=nvs_commits=nvs_closes=live_logs=saved_logs=0;
    summary[0]=0;devices[0x20].regs[1]=0xff;
}
static sleep_trace_t saved_trace(void){
    sleep_trace_t trace={0};assert(saved_size==sizeof(trace));memcpy(&trace,saved_blob,sizeof(trace));return trace;
}
static void check_trace(unsigned attempts,unsigned ok,unsigned failed,int exit){
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    sleep_trace_t trace=saved_trace();
    assert(trace.attempts==attempts&&trace.successes==ok&&trace.failures==failed&&trace.exit==(unsigned)exit);
    assert(trace.version==1&&trace.ok_call_us+trace.rejected_call_us<=trace.total_us);
    assert(nvs_writes==1&&nvs_commits==1&&nvs_closes==1&&live_logs==1&&!nvs_reads&&!saved_logs);
    assert(strstr(summary,"elf=0123456789abcdef")&&strstr(summary,"save_error=0"));
    assert(trace.start_us+trace.total_us+70000==now_us);
#else
    (void)attempts;(void)ok;(void)failed;(void)exit;
    assert(!nvs_reads&&!nvs_writes&&!nvs_commits&&!live_logs&&!saved_logs);
#endif
}
int main(void){
    struct sc7a20h_dev_t storage={.dev=&devices[0x19],.int1_gpio=2,.cfg=SC7A20H_CONFIG_IDLE};
    sc7a20h_handle_t acc=&storage;
    // 清中断/复查前读到按下，随后已松手也只需一次浅睡；旧函数必须在此失败。
    // Preserve the observed press before clearing/rechecking even after release; the old loop must fail here.
    reset_sleep(9);pickup_setting=false;
    assert(app_light_sleep_wait(acc)==APP_WAKE_KEY&&sleep_count==1);
    check_trace(1,1,0,SLEEP_TRACE_KEY);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    assert(saved_trace().key_latched==1&&saved_trace().gpio_wakes==1);
#endif
    reset_sleep(0);assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && sleep_count==1 && cache_restores==1);
    check_trace(1,1,0,SLEEP_TRACE_KEY);
    assert(suspend_count==1 && resume_count==1 && !acc->powered && irq_type==GPIO_INTR_LOW_LEVEL);
    assert(key_sampling);
    for(int n=1;n<=2;++n){reset_sleep(n);assert(app_light_sleep_wait(acc)==APP_WAKE_PICKUP && !sleep_count && last_wake==APP_WAKE_PICKUP);}
    reset_sleep(3);assert(app_light_sleep_wait(acc)==APP_WAKE_PICKUP && sleep_count==2);
    reset_sleep(4);ble_stopped=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && !sleep_count);
    check_trace(0,0,0,SLEEP_TRACE_KEY);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    assert(saved_trace().ble_blocked);
#endif
    reset_sleep(0);jobs_ready=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && !suspend_count && !resume_count && !wake_calls);
    check_trace(0,0,0,SLEEP_TRACE_JOBS_BUSY);
    reset_sleep(0);fail_reg=0x25;fail_writes=1;
    assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && sleep_count==1 && irq_type==GPIO_INTR_LOW_LEVEL && !acc->powered);
    reset_sleep(0);wake_fail_call=2;
    assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && sleep_count==1 && irq_type==GPIO_INTR_LOW_LEVEL && !devices[0x19].regs[0x30]);
    reset_sleep(5);stuck_irq=true;
    assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && !sleep_count && now_us<10000000 && resume_count==1);
    check_trace(0,0,0,SLEEP_TRACE_IRQ_STUCK);
    reset_sleep(6);pickup_setting=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && sleep_count==1);
    reset_sleep(7);pickup_setting=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && !sleep_count);
    check_trace(0,0,0,SLEEP_TRACE_TIMER_ERROR);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    assert(saved_trace().timer_errors==APP_LOCK_SLEEP_ERROR_LIMIT);
#endif
    reset_sleep(8);pickup_setting=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY && sleep_count==APP_LOCK_SLEEP_ERROR_LIMIT && !cache_restores);
    check_trace(APP_LOCK_SLEEP_ERROR_LIMIT,0,APP_LOCK_SLEEP_ERROR_LIMIT,SLEEP_TRACE_SLEEP_ERROR);
    assert(key_sampling);
    reset_sleep(0);key_sampling=false;assert(app_light_sleep_wait(acc)==APP_WAKE_KEY&&!key_sampling);
    reset_sleep(10);assert(app_light_sleep_wait(acc)==APP_WAKE_PICKUP&&sleep_count==1);
    check_trace(1,1,0,SLEEP_TRACE_PICKUP);
    for(int n=11;n<=16;++n){
        reset_sleep(n);pickup_setting=false;
        assert(app_light_sleep_wait(acc)==APP_WAKE_KEY&&sleep_count==(n==14?1u:2u));
        check_trace(n==14?1:2,n==12?0:(n==14?1:2),n==12?2:0,SLEEP_TRACE_KEY);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
        sleep_trace_t trace=saved_trace();
        assert(trace.key_latched==(n==14?1u:0u));
        assert(trace.unresolved_gpio==((n==11||n==13)?1u:0u));
        assert(trace.key_read_errors==(n==13?1u:0u));
        assert(trace.gpio_wakes==((n==11||n==13||n==14)?1u:0u));
        assert(trace.other_wakes==(n==16?1u:0u));
#endif
    }
    reset_sleep(11);pickup_setting=false;no_ioe=true;
    assert(app_light_sleep_wait(NULL)==APP_WAKE_KEY&&sleep_count==2);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    assert(saved_trace().key_read_errors==1&&!saved_trace().key_latched);
    reset_sleep(3);assert(app_light_sleep_wait(acc)==APP_WAKE_PICKUP);
    assert(saved_trace().short_calls==1&&saved_trace().min_call_us==10000&&saved_trace().max_call_us==100000);
    sleep_trace_t trace=saved_trace();assert(trace.ok_call_us==110000);
    sleep_trace_replay();assert(saved_logs==1&&nvs_reads==1&&nvs_writes==1&&nvs_commits==1&&nvs_closes==2);
    assert(strstr(summary,"origin=saved")&&strstr(summary,"ok_call_us=110000"));
    unsigned replays=saved_logs;
    nvs_fault=4;sleep_trace_replay();assert(saved_logs==replays);nvs_fault=0;
    for(int kind=0;kind<7;++kind){
        sleep_trace_t invalid=trace;
        if(kind==0)invalid.version=2;
        if(kind==1)invalid.elf[16]='x';
        if(kind==2)invalid.attempts++;
        if(kind==3)invalid.ok_call_us=invalid.total_us+1;
        if(kind==4)invalid.rejected_call_us=-1;
        if(kind==5)invalid.exit=99;
        memcpy(saved_blob,&invalid,sizeof(invalid));saved_size=sizeof(invalid)-(kind==6);
        sleep_trace_replay();assert(saved_logs==replays);
    }
    for(int fault=1;fault<=3;++fault){
        reset_sleep(0);nvs_fault=fault;
        assert(app_light_sleep_wait(acc)==APP_WAKE_KEY&&resume_count==1&&key_sampling);
        assert(live_logs==1&&!saved_size&&strstr(summary,"save_error=-1"));
        assert(nvs_closes==(fault==1?0u:1u));
    }
    reset_sleep(0);assert(app_light_sleep_wait(acc)==APP_WAKE_KEY);
    trace=saved_trace();trace.elf[0]='a';memcpy(saved_blob,&trace,sizeof(trace));
    parked=true;now_us+=50000;parked=false;
    sleep_trace_t next={.start_us=now_us};nvs_fault=3;
    sleep_trace_finish(&next,SLEEP_TRACE_JOBS_BUSY,APP_WAKE_KEY);
    nvs_fault=0;sleep_trace_replay();assert(strstr(summary,"elf=a123456789abcdef"));
    sleep_trace_t counters={0};
    sleep_trace_record(&counters,ESP_FAIL,1UL<<ESP_SLEEP_WAKEUP_GPIO,10000);
    assert(counters.failures==1&&!counters.gpio_wakes&&!counters.short_calls&&!counters.min_call_us);
    sleep_trace_record(&counters,ESP_OK,(1UL<<ESP_SLEEP_WAKEUP_TIMER)|(1UL<<ESP_SLEEP_WAKEUP_GPIO),2000000);
    sleep_trace_record(&counters,ESP_OK,0,0);
    assert(counters.attempts==3&&counters.successes==2&&counters.timer_wakes==1&&counters.gpio_wakes==1);
    assert(counters.other_wakes==1&&counters.short_calls==1&&!counters.min_call_us&&counters.max_call_us==2000000);
#else
    sleep_trace_replay();assert(!nvs_reads);
#endif
    // 分钟档用虚拟时钟跑完整时长，诊断开关均不得增加切片内落盘。
    // Run complete intervals with diagnostics on/off, never adding in-slice persistence.
    for(unsigned minutes=10;minutes<=60;minutes=minutes==10?30:minutes+30){
        reset_sleep(20);pickup_setting=false;
        int64_t deadline=now_us+(int64_t)minutes*60*1000000;
        assert(light_sleep_wait_until(acc,deadline)==APP_WAKE_TIMEOUT);
        assert(now_us==deadline+30000+(CONFIG_APP_SLEEP_DIAGNOSTICS?70000:0));
        assert(sleep_count==minutes*30&&cache_restores==sleep_count&&last_wake==APP_WAKE_NONE);
        assert(key_sampling&&resume_count==1);
        check_trace(sleep_count,sleep_count,0,SLEEP_TRACE_TIMEOUT);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
        sleep_trace_t timeout=saved_trace();
        assert(timeout.wake==APP_WAKE_TIMEOUT&&timeout.timer_wakes==sleep_count);
        sleep_trace_replay();assert(saved_logs==1&&strstr(summary,"origin=saved"));
#endif
    }
    reset_sleep(20);pickup_setting=false;
    assert(light_sleep_wait_until(acc,1501000)==APP_WAKE_TIMEOUT&&timer_us==1500000);
    assert(now_us==1501000+30000+(CONFIG_APP_SLEEP_DIAGNOSTICS?70000:0));
    check_trace(1,1,0,SLEEP_TRACE_TIMEOUT);
    reset_sleep(21);pickup_setting=false;
    assert(light_sleep_wait_until(acc,now_us+600000000)==APP_WAKE_KEY&&sleep_count==1);
    check_trace(1,1,0,SLEEP_TRACE_KEY);
    // 截止同一轮真实按键/拿起优先，保留按键锁存与诊断退出类型。
    // Key/pickup events at expiry win, preserving key capture and diagnostic exit types.
    reset_sleep(21);pickup_setting=false;
    assert(light_sleep_wait_until(acc,now_us+2000000)==APP_WAKE_KEY);
    check_trace(1,1,0,SLEEP_TRACE_KEY);
    reset_sleep(22);
    assert(light_sleep_wait_until(acc,now_us+2000000)==APP_WAKE_PICKUP);
    check_trace(1,1,0,SLEEP_TRACE_PICKUP);
    reset_sleep(14);pickup_setting=false;
    assert(light_sleep_wait_until(acc,now_us+100000)==APP_WAKE_KEY&&sleep_count==1);
    check_trace(1,1,0,SLEEP_TRACE_KEY);
#if CONFIG_APP_SLEEP_DIAGNOSTICS
    assert(saved_trace().key_latched==1);
#endif
    puts("PASS: actual sleep/SC shared IRQ key and pickup, prelatched motion before IOE checks, short-sleep filtering, arm/config failures, bounded stuck line, BLE/jobs fallback and sleep errors");
    puts("PASS: GPIO P17 capture before release, motion-only/unknown/read-error/stale wake causes, diagnostic on/off, summary accounting and NVS/replay failure boundaries");
}
"""

def main():
    with tempfile.TemporaryDirectory(prefix='metalio-drivers-') as folder:
        work = Path(folder)
        env, compiler = toolchain(work)
        headers = {
            'esp_err.h': ERRORS, 'driver/i2c_master.h': I2C, 'driver/gpio.h': GPIO,
            'freertos/FreeRTOS.h': FREERTOS, 'freertos/semphr.h': SEMAPHORE,
            'freertos/task.h': '#pragma once\nvoid vTaskDelay(unsigned);\n',
            'esp_log.h': '#pragma once\n#define ESP_LOGI(...) test_log(__VA_ARGS__)\n#define ESP_LOGW(...) test_log(__VA_ARGS__)\n#define ESP_LOGE(...) test_log(__VA_ARGS__)\n',
            'esp_rom_sys.h': '#pragma once\nvoid esp_rom_delay_us(unsigned);\n',
            'esp_timer.h': '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
            'esp_attr.h': '#pragma once\n#define IRAM_ATTR\n',
            'esp_sleep.h': '#pragma once\n#define ESP_EXT1_WAKEUP_ANY_LOW 0\n#define ESP_EXT1_WAKEUP_ANY_HIGH 1\nstatic inline int esp_sleep_enable_gpio_wakeup(void){return 0;}\nstatic inline int esp_sleep_enable_ext1_wakeup_io(unsigned long long pins,int mode){(void)pins;(void)mode;return 0;}\n',
            'driver/rtc_io.h': '#pragma once\nstatic inline int rtc_gpio_is_valid_gpio(int n){return n>=0;}\nstatic inline int rtc_gpio_pullup_en(int n){(void)n;return 0;}\nstatic inline int rtc_gpio_pulldown_en(int n){(void)n;return 0;}\nstatic inline int rtc_gpio_pullup_dis(int n){(void)n;return 0;}\nstatic inline int rtc_gpio_pulldown_dis(int n){(void)n;return 0;}\n',
            'sdkconfig.h': '#pragma once\n#define CONFIG_IDF_TARGET_ESP32S31 1\n',
        }
        for name, content in headers.items():
            path = work / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        includes = ['-I'+str(work)] + ['-I'+str(ROOT/'components'/component/'include') for component in ['board_i2c','fca9555','cst836u','sc7a20h','device_power']]
        bus = '#include "' + str(ROOT/'components/board_i2c/board_i2c.c') + '"\n'
        cases = [('bus', '', BUS_TEST),
                 ('tca', '#include "'+str(ROOT/'components/fca9555/fca9555.c')+'"\n', IO_TEST),
                 ('touch', '#include "'+str(ROOT/'components/cst836u/cst836u.c')+'"\n', TOUCH_TEST),
                 ('accel', '#include "'+str(ROOT/'components/sc7a20h/sc7a20h.c')+'"\n', ACCEL_TEST),
                 ('power', power_source(), POWER_TEST),
                 ('wake_poll', wake_poll_source(), WAKE_POLL_TEST),
                 ('sleep', sleep_source(), SLEEP_TEST),
                 ('sleep_off', sleep_source(False), SLEEP_TEST)]
        for percent in (100,75,50):
            source = (f'#define CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT {percent}\n' +
                      TOUCH_THRESHOLD_MODEL + '#include "' +
                      str(ROOT/'components/cst836u/cst836u.c') + '"\n')
            cases.append((f'touch_threshold_{percent}',source,TOUCH_THRESHOLD_TEST))
        for name, source, test in cases:
            c, binary = work/(name+'.c'), work/name
            c.write_text(MODEL + bus + source + test)
            subprocess.run(compiler + ['-std=gnu11','-Wall','-Wextra','-Wno-unused-function',
                                       '-Wno-unused-variable','-fsanitize=address,undefined','-pthread'] + includes + [str(c),'-o',str(binary)], env=env, check=True)
            subprocess.run([str(binary)], env=env, check=True)

if __name__ == '__main__':
    main()
