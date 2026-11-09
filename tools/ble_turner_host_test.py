#!/usr/bin/env python3
"""Exercise production HID input, learning feedback and BLE/network lifecycle under faults.

SPDX-FileCopyrightText: 2026 mindreset
SPDX-License-Identifier: Apache-2.0
中文：抽取真实函数，模拟报告、内存与任务失败。/ English: extract real functions and inject report, memory and task faults.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / 'components/ble_page_turner/src/ble_page_turner.c'
SETTINGS = ROOT / 'main/apps/app_device_settings.c'

def function(path, name):
    source = path.read_text()
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    start, pos, depth = match.start(), match.end(), 1
    # Remove comments while keeping byte positions, then count braces outside strings.
    cleaned = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ' ' * len(m[0]), source, flags=re.S)
    quote, escaped = None, False
    while depth:
        c = cleaned[pos]
        if quote:
            if escaped: escaped = False
            elif c == '\\': escaped = True
            elif c == quote: quote = None
        elif c in '\"\'': quote = c
        elif c == '{': depth += 1
        elif c == '}': depth -= 1
        pos += 1
    return source[start:pos]

unit = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "ble_page_turner.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define configMAX_PRIORITIES 25
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0
#define BLE_SM_IO_CAP_NO_IO 0
#define BLE_SM_IO_CAP_DISP_YES_NO 1
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_HS_CONN_HANDLE_NONE 65535
#define KEY_RING_LEN 16
#define RAW_RING_LEN 17
#define FRAME_MAX 16
#define SAME_KEY_DEBOUNCE_MS 500
#define RAW_RELEASE_GAP_MS 150
#define YXT_CODE_PREV 0x01u
#define YXT_CODE_NEXT 0x02u
#define HID_SVC_UUID 0x1812u
#define BLE_GAP_EVENT_DISC 1
#define BLE_GAP_EVENT_EXT_DISC 2
#define BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE 0
#define BLE_GAP_EXT_ADV_DATA_STATUS_INCOMPLETE 1
#define CONFIG_BT_NIMBLE_EXT_ADV 1
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define BLE_PT_MIN_FREE_INTERNAL (56*1024)
#define BLE_PT_MIN_LARGEST_INTERNAL (20*1024)
#define BLE_PT_MIN_FREE_PSRAM (256*1024)
static ble_pt_device_t s_devices[BLE_PT_MAX_DEVICES];
static uint8_t s_device_count;
struct fake_uuid {uint16_t u;};
struct ble_hs_adv_fields {const uint8_t *name;uint8_t name_len;int num_uuids16;struct fake_uuid *uuids16;};
struct ble_gap_disc_desc {struct {uint8_t val[6],type;}addr;int rssi;const uint8_t *data;uint8_t length_data;};
struct ble_gap_ext_disc_desc {uint8_t props,data_status,legacy_event_type;struct {uint8_t val[6],type;}addr;int8_t rssi,tx_power;uint8_t sid,prim_phy,sec_phy;uint16_t periodic_adv_itvl;uint8_t length_data;const uint8_t *data;};
struct ble_gap_event {int type;struct ble_gap_disc_desc disc;struct ble_gap_ext_disc_desc ext_disc;};
static struct ble_hs_adv_fields advertised;
static int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *out,const uint8_t *data,uint8_t len){(void)data;if(!len){memset(out,0,sizeof(*out));return 0;}*out=advertised;return 0;}
static uint16_t ble_uuid_u16(const uint16_t *uuid){return *uuid;}
static void *bond_find(const char *addr){assert(addr);return NULL;}
static atomic_flag s_lifecycle=ATOMIC_FLAG_INIT;
static atomic_uint s_network_users;
static atomic_bool s_running;
static bool s_connected=true,s_connecting,s_scanning,s_link_up;
static char s_reconnect_addr[18];
static uint32_t s_reconnect_at;
static uint16_t s_conn_handle;
#define BLE_PT_MTU_DFLT 23
static uint16_t s_mtu = BLE_PT_MTU_DFLT;
static bool s_mtu_tried;
static uint32_t s_stable_since;
static void *s_host_task;
static char s_failure[128];
static bool s_has_keyboard_page=true,s_has_consumer_page,s_rest_known;
static uint8_t s_held_usage,s_held_mods,s_emit_usage;
static uint32_t s_held_since,s_last_repeat,s_input_serial,s_emit_ms,s_last_rx_ms;
static uint8_t s_repeat_count;
static ble_pt_frame_t s_rest,s_raw_held,s_raw_prev,s_raw_base;
static bool s_raw_down;
static uint32_t s_raw_last_ms;
static int s_ring_lock;
static struct {ble_pt_event_t items[KEY_RING_LEN];uint8_t head,tail;}s_keys;
static struct {ble_pt_raw_t items[RAW_RING_LEN];uint8_t head,tail;}s_raws;
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;}
static size_t free_internal=150*1024,largest_internal=60*1024,free_psram=2*1024*1024;
static size_t heap_caps_get_free_size(int c){return c==MALLOC_CAP_SPIRAM?free_psram:free_internal;}
static size_t heap_caps_get_largest_free_block(int c){(void)c;return largest_internal;}
static int init_count,deinit_count,stop_count,task_count,init_error,task_error,stop_error;
static esp_err_t nimble_port_init(void){++init_count;return init_error;}
static esp_err_t nimble_port_deinit(void){++deinit_count;return ESP_OK;}
static int nimble_port_stop(void){++stop_count;return stop_error;}
static void host_task(void*p){(void)p;}
static void on_sync(void){}
static void bonds_load(void){}
static struct {void(*sync_cb)(void);int sm_io_cap,sm_bonding,sm_mitm,sm_sc,sm_our_key_dist,sm_their_key_dist;}ble_hs_cfg;
static int xTaskCreatePinnedToCore(void(*fn)(void*),const char*name,int stack,void*param,int priority,void**handle,int core){
 assert(fn==host_task && !strcmp(name,"nimble_host") && stack==4096 && !param && priority==21 && core==0);
 ++task_count;if(task_error)return 0;*handle=(void*)1;return pdPASS;
}
void ble_pt_scan_stop(void){s_scanning=false;}
void ble_pt_disconnect(void){s_connected=false;}
static int s_ble_scroll,s_ble_learning;
static char s_ble_feedback[64];
void ble_pt_crumb(uint8_t id){(void)id;}
bool ble_pt_connected(void){return s_connected;}
static int binding_error;
static struct {char addr[18];ble_pt_frame_t frames[BLE_PT_MAX_BINDINGS];} s_bindings;
static ble_pt_frame_t persisted[BLE_PT_MAX_BINDINGS];
static char persisted_key[16];
static int binding_handles;
typedef int nvs_handle_t;
#define NVS_NS_BINDINGS "bindings"
#define NVS_NS_BONDS "bonds"
#define NVS_READONLY 0
#define NVS_READWRITE 1
// 句柄 1 = 手动映射，句柄 2 = bond 命名空间里的解码提示缓存。
// Handle 1 is manual bindings; handle 2 is the decode-hint cache in the bond namespace.
static esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){
 if(!strcmp(ns,NVS_NS_BONDS)){assert(mode==NVS_READONLY||mode==NVS_READWRITE);*h=2;return ESP_OK;}
 assert(!strcmp(ns,NVS_NS_BINDINGS)&&(mode==NVS_READONLY||mode==NVS_READWRITE));if(binding_error==1)return ESP_FAIL;*h=1;binding_handles++;return ESP_OK;}
static esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*in,size_t len){assert(h==1&&len==sizeof(persisted));if(binding_error==2)return ESP_FAIL;snprintf(persisted_key,sizeof(persisted_key),"%s",k);memcpy(persisted,in,len);return ESP_OK;}
static esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*len){assert(h==1&&*len==sizeof(persisted));if(strcmp(k,persisted_key))return ESP_FAIL;memcpy(out,persisted,*len);return ESP_OK;}
static char hint_key_stored[16];
static uint8_t hint_bits;
static bool hint_stored;
static esp_err_t nvs_get_u8(nvs_handle_t h,const char*k,uint8_t*v){assert(h==2);if(!hint_stored||strcmp(k,hint_key_stored))return ESP_FAIL;*v=hint_bits;return ESP_OK;}
static esp_err_t nvs_set_u8(nvs_handle_t h,const char*k,uint8_t v){assert(h==2&&strlen(k)<sizeof(hint_key_stored));snprintf(hint_key_stored,sizeof(hint_key_stored),"%s",k);hint_bits=v;hint_stored=true;return ESP_OK;}
static esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){assert(h==2);if(!strcmp(k,hint_key_stored))hint_stored=false;return ESP_OK;}
static esp_err_t nvs_commit(nvs_handle_t h){assert(h==1||h==2);return binding_error==3?ESP_FAIL:ESP_OK;}
static void nvs_close(nvs_handle_t h){if(h==1)binding_handles--;}

'''
for name in ('addr_hash','bond_key','bindings_load','lifecycle_take','lifecycle_give','ble_pt_input_serial','key_push','raw_push','frame_of','frame_same',
             'frame_idle','raw_release','raw_reset','raw_frame','extract_primary_code',
             'usage_to_special','emit_usage','ingest_report','ble_pt_pop_key','ble_pt_pop_raw',
             'ble_pt_learn_begin','ble_pt_bind','ble_pt_binding','start_locked','stop_locked','ble_pt_start','ble_pt_stop',
             'ble_pt_stop_for_sleep','ble_pt_network_acquire','ble_pt_network_release','note_report','scan_cb','ble_pt_device_count',
             'ble_pt_device','ble_pt_action_for_frame','ble_pt_action_for_usage',
             'hint_key','hints_load','hints_save','hints_clear'):
    unit += '\n' + function(BLE, name)
unit += '\n' + function(SETTINGS, 'ble_receive_feedback')
unit += '\n' + function(SETTINGS, 'format_bound_key')
for name in ('ble_scan_visible','ble_scan_visible_count','ble_scan_visible_at'):
    unit += '\n' + function(SETTINGS,name)
unit += r'''
static void reset_input(void){
 memset(&s_keys,0,sizeof(s_keys));memset(&s_raws,0,sizeof(s_raws));
 memset(&s_rest,0,sizeof(s_rest));memset(&s_raw_held,0,sizeof(s_raw_held));
 memset(&s_raw_prev,0,sizeof(s_raw_prev));memset(&s_raw_base,0,sizeof(s_raw_base));
 memset(&s_bindings,0,sizeof(s_bindings));memset(persisted,0,sizeof(persisted));
 persisted_key[0]=0;
 s_rest_known=false;s_raw_down=false;s_raw_last_ms=0;s_has_consumer_page=false;
 s_held_usage=s_held_mods=s_emit_usage=0;s_emit_ms=0;s_connected=true;
 s_ble_feedback[0]=0;s_ble_learning=0;binding_error=0;s_ble_scroll=200;
 snprintf(s_bindings.addr,sizeof(s_bindings.addr),"AA:BB:CC:DD:EE:FF");
}
// 8 字节帧的键码在第 2 字节；9 字节帧首字节是报告号，键码在第 3 字节。
// The code sits in byte 2 of an 8-byte frame; byte 0 of a 9-byte frame is the report id and the
// code sits in byte 3.
static void report(int len,int id,int key){uint8_t data[9]={0};if(len==9)data[0]=(uint8_t)id;data[len==9?3:2]=(uint8_t)key;ingest_report(data,(size_t)len);}
static void wait_ms(int ms){clock_us+=(int64_t)ms*1000;}
int main(void){
 // 同地址的扫描响应补齐名字；过滤后的行仍选择正确设备。
 // A later scan response supplies a name; filtered rows still select the correct peer.
 struct ble_gap_event event={.type=BLE_GAP_EVENT_DISC,.disc={.rssi=-50,.length_data=1}};
 assert(!scan_cb(&event,NULL)&&s_device_count==1&&!ble_scan_visible_count());
 advertised.name=(const uint8_t*)"YueXingTong";advertised.name_len=11;
 assert(!scan_cb(&event,NULL)&&s_device_count==1&&ble_scan_visible_count()==1);
 assert(!strcmp(ble_scan_visible_at(0)->name,"YueXingTong"));
 advertised.name=(const uint8_t*)"YX";advertised.name_len=2;scan_cb(&event,NULL);
 assert(!strcmp(ble_scan_visible_at(0)->name,"YX"));
 advertised.name=NULL;advertised.name_len=0;scan_cb(&event,NULL);
 assert(ble_scan_visible_count()==1);
 event.disc.addr.val[0]=1;scan_cb(&event,NULL);
 assert(s_device_count==2&&ble_scan_visible_count()==1&&!ble_scan_visible_at(1)&&!ble_scan_visible_at(-1));
 event.disc.addr.val[0]=2;advertised.name=(const uint8_t*)"Next";advertised.name_len=4;scan_cb(&event,NULL);
 assert(ble_scan_visible_count()==2&&!strcmp(ble_scan_visible_at(1)->name,"Next"));
 for(unsigned i=3;i<BLE_PT_MAX_DEVICES+2;i++){event.disc.addr.val[0]=i;scan_cb(&event,NULL);}
 assert(s_device_count==BLE_PT_MAX_DEVICES);
 event.disc.addr.val[0]=1;scan_cb(&event,NULL);assert(s_devices[1].has_name);
 // 扩展广播：Legacy 与扩展报告共用入库逻辑；数据不完整时只记地址，名字等完整报告。
 // Extended advertising: legacy and extended reports share the ingest path; incomplete data records
 // only the address, and the name waits for a complete report.
 s_device_count=0;
 struct ble_gap_event ext={.type=BLE_GAP_EVENT_EXT_DISC,
     .ext_disc={.data_status=BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE,.rssi=-40}};
 ext.ext_disc.addr.val[0]=0x33;ext.ext_disc.addr.type=1;ext.ext_disc.length_data=1;
 ext.ext_disc.data=(const uint8_t*)"";
 advertised.name=(const uint8_t*)"Ext";advertised.name_len=3;
 assert(!scan_cb(&ext,NULL)&&s_device_count==1&&ble_scan_visible_count()==1);
 assert(!strcmp(s_devices[0].name,"Ext")&&s_devices[0].addr_type==1&&s_devices[0].rssi==-40);
 ext.ext_disc.addr.val[0]=0x34;ext.ext_disc.data_status=BLE_GAP_EXT_ADV_DATA_STATUS_INCOMPLETE;
 assert(!scan_cb(&ext,NULL)&&s_device_count==2&&!s_devices[1].has_name);
 assert(!strcmp(s_devices[1].name,"00:00:00:00:00:34")&&ble_scan_visible_count()==1);
 // 阅星瞳无需学习；手动绑定覆盖默认映射；键盘修饰键与报告号不能误翻页。
 // YueXingTong needs no learning; manual bindings override defaults, and neither keyboard modifiers
 // nor report ids turn pages.
 s_has_keyboard_page=false;
 assert(ble_pt_action_for_usage(1,0)==BLE_PT_ACTION_PREV&&ble_pt_action_for_usage(2,0)==BLE_PT_ACTION_NEXT);
 const ble_pt_frame_t yxt_prev={.len=1,.bytes={0x01}},yxt_next={.len=1,.bytes={0x02}};
 const ble_pt_frame_t unknown_frame={.len=2,.bytes={0x03,0x00}},empty_frame={0};
 const ble_pt_frame_t report_id_frame={.len=9,.bytes={0x01,0,0,0,0,0,0,0,0}};
 assert(ble_pt_action_for_frame(&yxt_prev)==BLE_PT_ACTION_PREV&&ble_pt_action_for_frame(&yxt_next)==BLE_PT_ACTION_NEXT);
 assert(ble_pt_action_for_frame(&unknown_frame)==BLE_PT_ACTION_NONE&&ble_pt_action_for_frame(&empty_frame)==BLE_PT_ACTION_NONE);
 assert(ble_pt_action_for_frame(NULL)==BLE_PT_ACTION_NONE&&ble_pt_action_for_frame(&report_id_frame)==BLE_PT_ACTION_NONE);
 s_bindings.frames[1]=yxt_prev;assert(ble_pt_action_for_frame(&yxt_prev)==BLE_PT_ACTION_NEXT);
 memset(&s_bindings.frames[1],0,sizeof(s_bindings.frames[1]));
 s_has_keyboard_page=true;assert(ble_pt_action_for_frame(&yxt_prev)==BLE_PT_ACTION_NONE&&ble_pt_action_for_frame(&yxt_next)==BLE_PT_ACTION_NONE);
 s_bindings.frames[0]=yxt_prev;assert(ble_pt_action_for_frame(&yxt_prev)==BLE_PT_ACTION_PREV);
 memset(&s_bindings.frames[0],0,sizeof(s_bindings.frames[0]));
 // 一字节报告：首按就是边沿，连发不算，松开会收尾。
 // A one-byte report: the first press is already an edge, repeats do not count, and the release closes it.
 reset_input();s_has_keyboard_page=false;
 uint8_t native=1;ble_pt_event_t key;ble_pt_raw_t raw;
 ingest_report(&native,1);assert(ble_pt_pop_key(&key)&&ble_pt_action_for_usage(key.usage,key.mods)==BLE_PT_ACTION_PREV);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&raw.frame.len==1&&raw.frame.bytes[0]==1);
 ingest_report(&native,1);assert(!ble_pt_pop_key(&key)&&!ble_pt_pop_raw(&raw));
 native=0;ingest_report(&native,1);assert(ble_pt_pop_raw(&raw)&&!raw.pressed&&raw.frame.bytes[0]==1);
 native=2;ingest_report(&native,1);
 assert(ble_pt_pop_key(&key)&&ble_pt_action_for_usage(key.usage,key.mods)==BLE_PT_ACTION_NEXT);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&ble_pt_action_for_frame(&raw.frame)==BLE_PT_ACTION_NEXT);
 // 标准键盘：byte 0 是修饰键，那里的 0x01 不是翻页。
 // A standard keyboard: byte 0 is the modifier byte, where 0x01 is not a page turn.
 reset_input();s_has_keyboard_page=true;
 uint8_t keyboard[8]={0};ingest_report(keyboard,8);keyboard[0]=1;ingest_report(keyboard,8);
 assert(!ble_pt_pop_key(&key)&&ble_pt_pop_raw(&raw)&&ble_pt_action_for_frame(&raw.frame)==BLE_PT_ACTION_NONE);
 // 键盘页 + 消费者页：键盘字节为零时回退到消费者用法码，两个方向与带报告号的短帧都认。
 // Keyboard + consumer pages: when the keyboard byte is zero, fall back to the consumer usage;
 // both directions and a short report-id frame are recognised.
 reset_input();s_has_keyboard_page=true;s_has_consumer_page=true;
 uint8_t both_kb[8]={0},both_consumer[2]={0xB6,0},consumer_id[3]={0x02,0xB5,0};
 ingest_report(both_kb,8);assert(!ble_pt_pop_key(&key));
 ingest_report(both_consumer,2);
 assert(ble_pt_pop_key(&key)&&key.usage==0xB6&&ble_pt_action_for_usage(key.usage,key.mods)==BLE_PT_ACTION_PREV);
 both_consumer[0]=0xB5;ingest_report(both_consumer,2);
 assert(ble_pt_pop_key(&key)&&ble_pt_action_for_usage(key.usage,key.mods)==BLE_PT_ACTION_NEXT);
 wait_ms(600);ingest_report(consumer_id,3);
 assert(ble_pt_pop_key(&key)&&key.usage==0xB5&&ble_pt_action_for_usage(key.usage,key.mods)==BLE_PT_ACTION_NEXT);
 // 键盘帧只有修饰键时不把修饰键当键码。
 // A keyboard frame carrying only a modifier does not decode the modifier as a key.
 reset_input();s_has_keyboard_page=true;
 uint8_t mods_only[8]={0x02,0};ingest_report(mods_only,8);assert(!ble_pt_pop_key(&key));
 // 无报告映射提示的 9 字节帧：扫键码时跳过报告号。
 // A 9-byte frame without report-map hints: the scan skips the report id.
 reset_input();s_has_keyboard_page=false;
 uint8_t no_hint9[9]={0x07,0,0,0x4b,0,0,0,0,0};ingest_report(no_hint9,9);
 assert(ble_pt_pop_key(&key)&&key.usage==0x4b);
 // 非标遥控器：静止帧带非零常量，每次按下发的字节一模一样，整帧就是身份。
 // A non-standard remote keeps a non-zero constant in its resting frame and sends identical bytes on
 // every press, so the whole frame is the identity.
 const uint8_t hw_rest[8]={0x06,0x00,0,0,0,0,0,0},hw_prev[8]={0x06,0xb1,0,0,0,0,0,0},hw_next[8]={0x06,0xb2,0,0,0,0,0,0};
 reset_input();s_has_keyboard_page=false;
 ingest_report(hw_rest,8);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&raw.frame.len==8&&raw.frame.bytes[0]==0x06);
 // 点「学习」：此刻那一帧成为基准，静止帧不再算一次按下。
 // Tapping 学习 pins the frame present now as the baseline, so the resting frame stops being a press.
 ble_pt_learn_begin();
 wait_ms(200);ingest_report(hw_rest,8);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed&&raw.frame.bytes[0]==0x06);
 ingest_report(hw_rest,8);assert(!ble_pt_pop_raw(&raw));
 // 第一次按下就学得会，不需要先来一条松开报告。
 // The very first press learns, with no release report before it.
 s_ble_learning=1;ingest_report(hw_prev,8);
 assert(ble_receive_feedback()&&!s_ble_learning&&strstr(s_ble_feedback,"上一页"));
 ble_pt_frame_t bound={0};
 assert(ble_pt_binding(BLE_PT_ACTION_PREV,&bound)&&bound.len==8&&bound.bytes[1]==0xb1);
 assert(ble_pt_action_for_frame(&bound)==BLE_PT_ACTION_PREV);
 assert(persisted[0].len==8&&persisted[0].bytes[1]==0xb1);
 const ble_pt_frame_t hw_next_frame={.len=8,.bytes={0x06,0xb2}};
 assert(ble_pt_action_for_frame(&hw_next_frame)==BLE_PT_ACTION_NONE);
 char label[48];format_bound_key(&bound,label,sizeof(label));assert(!strcmp(label,"06 B1"));
 // 遥控器一直不发松开报告：静默之后同一个键再按一次，仍是新的一次按下。
 // With no release report ever: after silence, the same key pressed again is a new press.
 wait_ms(200);ingest_report(hw_prev,8);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&ble_pt_action_for_frame(&raw.frame)==BLE_PT_ACTION_PREV);
 ingest_report(hw_prev,8);assert(!ble_pt_pop_raw(&raw));
 // 两个不同的键紧接着按下，后一个不会丢。
 // Two different keys in quick succession: the second one is not lost.
 ingest_report(hw_next,8);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed&&raw.frame.len==8&&raw.frame.bytes[1]==0xb2);
 // 松开帧是另一个非零常量：回到按下之前那一帧只算松开，不再冒一次按下。
 // A non-zero release frame: returning to the frame before the press is only a release.
 reset_input();s_has_keyboard_page=false;
 ingest_report(hw_rest,8);assert(ble_pt_pop_raw(&raw)&&raw.pressed);
 ingest_report(hw_prev,8);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed);
 assert(ble_pt_pop_raw(&raw)&&raw.pressed);
 ingest_report(hw_rest,8);
 assert(ble_pt_pop_raw(&raw)&&!raw.pressed&&raw.frame.bytes[1]==0xb1);
 assert(!ble_pt_pop_raw(&raw));
 // 学到的标识：帧里非零的字节照原样列出，同一个键每次都一样。
 // The learned identifier lists the frame's non-zero bytes and is identical on every press.
 const ble_pt_frame_t empty_tag={0},one_byte={.len=1,.bytes={0x4b}},two_bytes={.len=2,.bytes={0xaa,0xbb}};
 const ble_pt_frame_t four_bytes={.len=4,.bytes={1,2,3,4}};
 format_bound_key(&empty_tag,label,sizeof(label));assert(!label[0]);
 format_bound_key(&one_byte,label,sizeof(label));assert(!strcmp(label,"4B"));
 format_bound_key(&two_bytes,label,sizeof(label));assert(!strcmp(label,"AA BB"));
 format_bound_key(&four_bytes,label,sizeof(label));assert(!strcmp(label,"01 02 03 +"));
 char tiny[3];format_bound_key(&two_bytes,tiny,sizeof(tiny));assert(tiny[2]==0);
 char sentinel='x';format_bound_key(&two_bytes,&sentinel,0);assert(sentinel=='x');
 // 首次按下、重复帧、松开与报告号都不需要先点屏幕，一次按下就够学。
 // First presses, duplicate frames, releases and report ids need no touch, and one press is enough.
 for(int len=8;len<=9;len++){
  reset_input();s_has_keyboard_page=false;s_ble_learning=1;
  report(len,len==9?7:0,0x4b);
  assert(ble_receive_feedback()&&!s_ble_learning&&s_ble_scroll==0);
  ble_pt_frame_t learned={0};
  assert(ble_pt_binding(BLE_PT_ACTION_PREV,&learned)&&learned.len==len);
  assert(learned.bytes[len==9?3:2]==0x4b&&learned.bytes[0]==(len==9?7:0));
  format_bound_key(&learned,label,sizeof(label));assert(!strcmp(label,len==9?"07 4B":"4B"));
  assert(strstr(s_ble_feedback,"识别成功")&&strstr(s_ble_feedback,"上一页"));
  report(len,len==9?7:0,0x4b);assert(!ble_receive_feedback());
  report(len,len==9?7:0,0);assert(!s_held_usage&&!ble_receive_feedback());
  s_ble_feedback[0]=0;s_ble_learning=2;report(len,len==9?7:0,0x4e);
  assert(ble_receive_feedback()&&!s_ble_learning&&strstr(s_ble_feedback,"下一页"));
  ble_pt_frame_t learned_next={0};
  assert(ble_pt_binding(BLE_PT_ACTION_NEXT,&learned_next)&&learned_next.bytes[len==9?3:2]==0x4e);
 }
 // 保存后停栈、清空内存映射，再按同地址载入：首按不能变成静止帧。
 // Stop after saving, erase RAM bindings, and reload the peer: a first press must not become rest.
 for(int keyboard_hint=0;keyboard_hint<2;++keyboard_hint)for(int next_first=0;next_first<2;++next_first){
  reset_input();s_has_keyboard_page=keyboard_hint;
  const ble_pt_frame_t prev={.len=8,.bytes={0x06,0xb1}},next={.len=8,.bytes={0x06,0xb2}};
  assert(ble_pt_bind(BLE_PT_ACTION_PREV,&prev)==ESP_OK);
  assert(ble_pt_bind(BLE_PT_ACTION_NEXT,&next)==ESP_OK);
  assert(ble_pt_start()==ESP_OK);
  snprintf(s_reconnect_addr,sizeof(s_reconnect_addr),"aa:bb:cc:dd:ee:ff");
  assert(ble_pt_stop_for_sleep(2000)==ESP_OK&&!s_running&&s_reconnect_addr[0]);
  raw_reset();s_rest_known=false;
  memset(&s_bindings,0,sizeof(s_bindings));
  bindings_load("11:22:33:44:55:66");assert(!ble_pt_binding(BLE_PT_ACTION_PREV,NULL));
  bindings_load("AA:BB:CC:DD:EE:FF");
  assert(ble_pt_binding(BLE_PT_ACTION_PREV,NULL)&&ble_pt_binding(BLE_PT_ACTION_NEXT,NULL)&&!binding_handles);
  assert(ble_pt_start()==ESP_OK);s_connected=true;
  const ble_pt_frame_t *first=next_first?&next:&prev,*second=next_first?&prev:&next;
  const ble_pt_action_t first_action=next_first?BLE_PT_ACTION_NEXT:BLE_PT_ACTION_PREV;
  const ble_pt_action_t second_action=next_first?BLE_PT_ACTION_PREV:BLE_PT_ACTION_NEXT;
  ingest_report(first->bytes,first->len);
  assert(ble_pt_pop_raw(&raw)&&raw.pressed&&ble_pt_action_for_frame(&raw.frame)==first_action);
  assert(!s_rest_known);
  ingest_report(first->bytes,first->len);assert(!ble_pt_pop_raw(&raw));
  wait_ms(200);ingest_report(first->bytes,first->len);
  assert(ble_pt_pop_raw(&raw)&&!raw.pressed);
  assert(ble_pt_pop_raw(&raw)&&raw.pressed&&ble_pt_action_for_frame(&raw.frame)==first_action);
  ingest_report(second->bytes,second->len);
  assert(ble_pt_pop_raw(&raw)&&!raw.pressed);
  assert(ble_pt_pop_raw(&raw)&&raw.pressed&&ble_pt_action_for_frame(&raw.frame)==second_action);
  assert(ble_pt_stop(2000)==ESP_OK);
 }
 init_count=deinit_count=stop_count=task_count=0;
 reset_input();s_has_keyboard_page=false;
 s_ble_learning=2;binding_error=3;report(8,0,0x4e);
 assert(ble_receive_feedback()&&strstr(s_ble_feedback,"保存失败")&&!s_ble_learning);
 reset_input();s_has_keyboard_page=false;
 report(8,0,0x4b);assert(ble_receive_feedback()&&strstr(s_ble_feedback,"已收到"));
 // 有界队列、断连和松开不造成误绑定。/ Bound queues and ignore releases or disconnected input.
 reset_input();s_has_keyboard_page=false;
 for(int n=0;n<100;n++){uint8_t burst[8]={0};burst[2]=(n%2)?0x4b:0;ingest_report(burst,8);}
 assert(ble_receive_feedback());
 reset_input();s_has_keyboard_page=false;
 report(8,0,0x4b);s_connected=false;s_ble_learning=1;
 assert(!ble_receive_feedback()&&s_ble_learning==1);
 // 内存不够时不进入 NimBLE；任务失败完整回收。/ Reject before NimBLE on low memory; unwind a failed host task.
 for(int kind=0;kind<3;kind++){
  free_internal=150*1024;largest_internal=60*1024;free_psram=2*1024*1024;
  if(kind==0)free_internal=55*1024;if(kind==1)largest_internal=19*1024;if(kind==2)free_psram=255*1024;
  int before=init_count;assert(ble_pt_start()==ESP_ERR_NO_MEM && !s_running && init_count==before);
 }
 free_internal=150*1024;largest_internal=60*1024;free_psram=2*1024*1024;
 task_error=1;assert(ble_pt_start()==ESP_ERR_NO_MEM && !s_running && task_count==1 && deinit_count==1);
 task_error=0;assert(ble_pt_start()==ESP_OK && s_running);
 stop_error=1;assert(ble_pt_network_acquire()==ESP_FAIL && atomic_load(&s_network_users)==0 && s_running);
 stop_error=0;assert(ble_pt_network_acquire()==ESP_OK && !s_running && atomic_load(&s_network_users)==1);
 int before=init_count;assert(ble_pt_start()==ESP_ERR_INVALID_STATE && init_count==before);
 assert(ble_pt_network_acquire()==ESP_OK && atomic_load(&s_network_users)==2);
 ble_pt_network_release();assert(ble_pt_start()==ESP_ERR_INVALID_STATE);
 ble_pt_network_release();ble_pt_network_release();assert(!atomic_load(&s_network_users));
 assert(ble_pt_start()==ESP_OK);assert(ble_pt_stop(2000)==ESP_OK && !s_running);
 atomic_flag_test_and_set(&s_lifecycle);assert(ble_pt_start()==ESP_ERR_TIMEOUT && !s_running);lifecycle_give();
 for(int n=0;n<100;n++){assert(ble_pt_start()==ESP_OK);assert(ble_pt_network_acquire()==ESP_OK);ble_pt_network_release();}
 // 解码提示按对端缓存：两个页各占一位，别的地址读不到，忘掉设备后也查不到。
 // Decode hints are cached per peer with one bit per page; another address cannot read them and
 // forgetting the peer clears them.
 bool kbd=false,cons=false;
 assert(!hints_load("AA:BB:CC:DD:EE:FF",&kbd,&cons));
 hints_save("AA:BB:CC:DD:EE:FF",true,false);
 assert(hints_load("AA:BB:CC:DD:EE:FF",&kbd,&cons)&&kbd&&!cons);
 hints_save("AA:BB:CC:DD:EE:FF",false,true);
 assert(hints_load("AA:BB:CC:DD:EE:FF",&kbd,&cons)&&!kbd&&cons);
 assert(!hints_load("11:22:33:44:55:66",&kbd,&cons));
 hints_save("AA:BB:CC:DD:EE:FF",false,false);
 assert(hints_load("AA:BB:CC:DD:EE:FF",&kbd,&cons)&&!kbd&&!cons);
 hints_clear("AA:BB:CC:DD:EE:FF");
 assert(!hints_load("AA:BB:CC:DD:EE:FF",&kbd,&cons));
 assert(!s_running && !atomic_load(&s_network_users));
 puts("PASS: named discovery, extended-report ingest, per-peer hint cache, YueXingTong/manual/modifier mappings, keyboard+consumer fallback, whole-frame raw identity, first-press learning, missing release reports, bounded queues, memory/task failures and network resource leases");
}
'''
# Active tick dispatch must consume reports before a state-only redraw can return.
assert 'ble_receive_feedback()' in function(SETTINGS, 'on_tick')
assert 'while (ble_pt_pop_raw(&stale))' in function(SETTINGS, 'on_gesture')
# 进扫描页先断开当前链路，链路落下后由 tick 补起扫描。
# Entering the scan page drops the current link; the tick starts the scan once it is down.
assert 'ble_pt_disconnect()' in function(SETTINGS, 'on_gesture')
assert 'ble_pt_scan_start(BLE_PT_SCAN_FOREVER)' in function(SETTINGS, 'on_tick')
# 对时入口经 sync_time_run 落到持有 BLE 租约的 sync_time_impl；扫描与传输入口直接取租约。
# The sync-time entry delegates through sync_time_run to the leased sync_time_impl; scan and
# transfer start hold the lease directly.
transfer = (ROOT / 'components/wifi_transfer/wifi_transfer.c').read_text()
for name in ('wifi_transfer_start', 'wifi_transfer_scan_wifi', 'sync_time_impl'):
    assert 'ble_pt_network_acquire()' in function(ROOT / 'components/wifi_transfer/wifi_transfer.c', name)
assert 'sync_time_run(utc_seconds, false, false)' in function(ROOT / 'components/wifi_transfer/wifi_transfer.c', 'wifi_transfer_sync_time')
assert 'sync_time_impl(utc_seconds)' in function(ROOT / 'components/wifi_transfer/wifi_transfer.c', 'sync_time_run')
assert transfer.count('ble_pt_network_acquire()') == 3
# 描述符遍历只能在自己的特征定义范围内，Report Map 必须分段读全。
# Descriptor discovery must stay inside its own characteristic definition, and the Report Map must
# be read in full chunks.
assert 's_hid_chr_defs' in function(BLE, 'dsc_next')
assert 'ble_gattc_read_long' in function(BLE, 'dsc_next')
assert 'ble_gattc_read(' not in function(BLE, 'dsc_next')
assert 'report_map_finish()' in function(BLE, 'report_map_cb')
assert 'BLE_GAP_EVENT_REPEAT_PAIRING' in function(BLE, 'gap_cb')
# 连接后交换 MTU，缓存命中时跳过 Report Map，扩展广播与 Legacy 共用入库路径。
# MTU is exchanged after connecting, a cached hint skips the Report Map, and extended advertising
# shares the ingest path with legacy reports.
assert 'mtu_exchange()' in function(BLE, 'gap_cb')
assert 'mtu_exchange()' in function(BLE, 'step_poll')
assert 's_hints_cached' in function(BLE, 'dsc_next')
# start_connect 的正文里有同名的 if 调用，按整段源码核对这一行。
# start_connect's body also contains an if-statement calling it, so check the exact line in the
# source rather than the extracted function.
assert 's_hints_cached = hints_load(addr, &s_has_keyboard_page, &s_has_consumer_page);' in BLE.read_text()
assert 'hints_save' in function(BLE, 'report_map_finish')
assert 'BLE_GAP_EVENT_EXT_DISC' in function(BLE, 'scan_cb')
assert 'BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE' in function(BLE, 'scan_cb')
# Secure Connections 只声明支持，不用 sm_sc_only：老遥控器仍走 Legacy。
# Secure Connections is advertised, not required (sm_sc_only): legacy remotes still pair.
assert 'ble_hs_cfg.sm_sc = 1;' in function(BLE, 'start_locked')
with tempfile.TemporaryDirectory(prefix='pico-ble-test-') as directory:
    work = Path(directory)
    (work/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\nenum {ESP_OK,ESP_FAIL,ESP_ERR_NO_MEM,ESP_ERR_TIMEOUT,ESP_ERR_INVALID_STATE,ESP_ERR_INVALID_ARG};\n')
    (work/'test.c').write_text(unit)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',
                    '-I'+str(work),'-I'+str(ROOT/'components/ble_page_turner/include'),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
