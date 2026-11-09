#!/usr/bin/env python3
"""中文：真实 RTC 恢复、后台校准与 SNTP 所有权回归。/ English: Actual RTC restore, scheduler and SNTP ownership regressions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess, tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
common=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_INVALID_RESPONSE 5
#define ESP_ERR_NOT_FOUND 6
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
typedef int esp_err_t;
static int64_t mono;
static time_t wall=1800000000;
static int64_t esp_timer_get_time(void){return mono;}
#define time(...) wall
static void vTaskDelay(int ms){mono+=(int64_t)ms*1000;}
'''
restore=common+r'''
#define PMU_CMD_TIME_GET 7
typedef struct {uint32_t unix_sec;uint16_t unix_millis;bool time_ok,time_synced;} pmu_snapshot_t;
static pmu_snapshot_t pmu;
static struct timeval current,assigned;
static int reads,writes,pmu_error;
static esp_err_t device_power_cmd(int cmd,void *p,size_t n){(void)cmd;(void)p;(void)n;++reads;return pmu_error;}
static const pmu_snapshot_t *device_power_get(void){return &pmu;}
static int fake_gettimeofday(struct timeval *tv,void *zone){(void)zone;*tv=current;return 0;}
static int fake_settimeofday(const struct timeval *tv,void *zone){(void)zone;assigned=*tv;++writes;return 0;}
#define gettimeofday fake_gettimeofday
#define settimeofday fake_settimeofday
'''+function('restore_time_from_pmu',root/'main/app_main.c')+r'''
int main(void){
 pmu=(pmu_snapshot_t){1800000000,987,true,true};restore_time_from_pmu();
 assert(reads==1&&writes==1&&assigned.tv_sec==1800000000&&assigned.tv_usec==987000);
 current=(struct timeval){1800000001,123456};restore_time_from_pmu();assert(reads==1&&writes==1);
 current.tv_sec=0;pmu.unix_millis=1000;restore_time_from_pmu();assert(writes==1);
 pmu.unix_millis=0;pmu.time_ok=false;restore_time_from_pmu();assert(writes==1);
 pmu.time_ok=true;pmu.time_synced=false;restore_time_from_pmu();assert(writes==1);
 pmu.time_synced=true;pmu.unix_sec=1;restore_time_from_pmu();assert(writes==1);
 pmu.unix_sec=1800000000;pmu_error=ESP_FAIL;restore_time_from_pmu();assert(writes==1);
 puts("PASS: retained S3 time survives reset; cold boot uses fresh PMU milliseconds; invalid/unsynced PMU cannot overwrite it");
}
'''
scheduler=common+r'''
typedef int nvs_handle_t;
enum {WIFI_TRANSFER_STOPPED,WIFI_TRANSFER_STARTING,WIFI_TRANSFER_READY,WIFI_TRANSFER_UPLOADING,WIFI_TRANSFER_ERROR};
enum {WIFI_TRANSFER_MODE_AP,WIFI_TRANSFER_MODE_STA};
typedef struct {int state,mode;bool network_ready;} wifi_transfer_status_t;
static wifi_transfer_status_t network;
static uint64_t stored;
static uint32_t pending;
static int jobs,task_ok=1,pmu_syncs,saves,worker_result,credential_reads;
static bool creds=true;
static void (*job)(void *);
static bool manual_trace;
static char manual_events[32];
static unsigned manual_event_count;
static void event(char c){if(manual_trace){assert(manual_event_count+1<sizeof(manual_events));manual_events[manual_event_count++]=c;manual_events[manual_event_count]=0;}}
static int nvs_open(const char *ns,int mode,int *h){event('O');(void)mode;assert(!strcmp(ns,"kiiko_clock"));*h=1;return ESP_OK;}
static int nvs_get_u64(int h,const char *k,uint64_t *v){(void)h;(void)k;*v=stored;return ESP_OK;}
static int nvs_set_u64(int h,const char *k,uint64_t v){(void)h;(void)k;event('V');stored=v;++saves;return ESP_OK;}
static int nvs_commit(int h){event('C');(void)h;return ESP_OK;}
static void nvs_close(int h){event('X');(void)h;}
static bool wifi_transfer_cancel_time_sync(void){return true;}
static bool wifi_transfer_take_time_sync(uint32_t *v){*v=pending;pending=0;return *v!=0;}
static void wifi_transfer_get_status(wifi_transfer_status_t *s){event('S');*s=network;}
static int wifi_transfer_get_saved_wifi(char *s,bool *b){*s=0;++credential_reads;*b=creds;return ESP_OK;}
static int wifi_transfer_sync_time_auto(uint32_t *v){*v=wall;if(!worker_result)pending=wall;return worker_result;}
static int manual_error,write_error,read_error,pmu_offset;
static bool pmu_absent;
static uint32_t written_utc;
static int wifi_transfer_sync_time(uint32_t *v){event('N');*v=(uint32_t)wall-13;wall+=3;return manual_error;}
#define PMU_CMD_TIME_SYNC 8
#define PMU_CMD_TIME_GET 7
static int device_power_cmd(int cmd,const uint8_t *data,size_t n){
 if(cmd==PMU_CMD_TIME_SYNC){event('W');assert(data&&n==4);written_utc=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;assert(written_utc==(uint32_t)wall);return write_error;}
 assert(cmd==PMU_CMD_TIME_GET&&!data&&!n);event('R');return read_error;
}
static int app_sync_time_to_pmu(void){++pmu_syncs;return ESP_OK;}
static int xTaskCreate(void (*fn)(void*),const char *n,int stack,void *arg,int pri,void *handle){
 (void)n;(void)arg;(void)handle;assert(stack==6144&&pri==1);job=fn;if(task_ok)++jobs;return task_ok;
}
static void vTaskDelete(void *t){(void)t;}
static bool s_auto=true;static uint32_t s_try;
static uint32_t s_last_record;static int record_saves;
static bool app_settings_auto_time_sync(void){return s_auto;}
static uint32_t app_settings_time_sync_attempt(void){return s_try;}
static void app_settings_set_time_sync_attempt(uint32_t t){s_try=t;}
static void app_settings_set_time_sync_last(uint32_t t){event('L');s_last_record=t;++record_saves;}
typedef struct {uint32_t unix_sec;bool time_synced;} pmu_snapshot_t;
static pmu_snapshot_t the_pmu;
static const pmu_snapshot_t *device_power_get(void){event('P');the_pmu.unix_sec=(uint32_t)(wall+pmu_offset);return pmu_absent?NULL:&the_pmu;}
static int pmu_battery_percent(const pmu_snapshot_t *p){(void)p;return 80;}
static bool pmu_battery_charging(const pmu_snapshot_t *p){(void)p;return false;}
'''
scheduler+='\n'.join(x for x in (root/'main/time/time_service.h').read_text().splitlines() if not x.startswith(('#include','#pragma once')))+'\n'
scheduler+=__import__('pathlib').Path(root/'main/time/time_sync_policy.h').read_text().replace('#pragma once','')+'\n'+'\n'.join(x for x in (root/'main/time/time_service.c').read_text().splitlines() if not x.startswith('#include'))
scheduler+=r'''
static void reset(void){
 manual_trace=false;manual_event_count=0;manual_events[0]=0;
 manual_error=write_error=read_error=pmu_offset=0;pmu_absent=false;the_pmu.time_synced=true;written_utc=0;
 s_loaded=false;s_last_sync=0;s_next_attempt_us=15000000;atomic_store(&s_running,false);
 stored=pending=jobs=pmu_syncs=saves=credential_reads=worker_result=record_saves=0;s_last_record=0;task_ok=1;creds=true;mono=15000000;
 network=(wifi_transfer_status_t){WIFI_TRANSFER_STOPPED,WIFI_TRANSFER_MODE_STA,false};
}
int main(void){
 reset();app_time_service_poll(false);assert(!jobs&&!s_loaded);app_time_service_poll(true);assert(jobs==1);
 app_time_service_poll(true);assert(jobs==1);job(NULL);assert(!atomic_load(&s_running));
 app_time_service_poll(true);assert(pmu_syncs==1&&saves==1&&stored==(uint64_t)wall&&record_saves==1&&s_last_record==wall&&jobs==1);
 mono+=3600000000LL;wall+=3600;app_time_service_poll(true);assert(jobs==1); // one hour is not enough
 network=(wifi_transfer_status_t){WIFI_TRANSFER_READY,WIFI_TRANSFER_MODE_STA,true};
 app_time_service_poll(true);assert(jobs==1); // online does not shorten the cadence either
 mono+=18000LL*1000000;wall+=18000;app_time_service_poll(true);assert(jobs==2); // six hours, reusing the online link
 job(NULL);app_time_service_poll(true);assert(pmu_syncs==2&&record_saves==2&&s_last_record==wall);
 reset();stored=wall-21600;app_time_service_poll(true);assert(jobs==1); // overdue offline
 reset();stored=wall-21599;app_time_service_poll(true);assert(!jobs);
 for(int state=1;state<=4;++state){reset();network.state=state;network.mode=WIFI_TRANSFER_MODE_AP;network.network_ready=true;app_time_service_poll(true);assert(!jobs);}
 reset();creds=false;app_time_service_poll(true);assert(!jobs&&credential_reads==1);app_time_service_poll(true);assert(credential_reads==1);
 reset();task_ok=0;app_time_service_poll(true);assert(!jobs&&!atomic_load(&s_running));
 task_ok=1;app_time_service_poll(true);assert(!jobs);mono+=CLOCK_RETRY_US;app_time_service_poll(true);assert(jobs==1);
 reset();worker_result=ESP_FAIL;app_time_service_poll(true);job(NULL);app_time_service_poll(true);assert(!pmu_syncs&&!saves&&jobs==1);
 pending=wall;app_time_service_poll(false);assert(pending);app_time_service_poll(true);assert(!pending&&pmu_syncs==1&&saves==1&&record_saves==1);
 reset();app_time_service_note_success(0);assert(!record_saves); // an invalid UTC cannot enter the record
 app_time_service_note_success(wall);assert(record_saves==1&&s_last_record==wall);
 // 保留部分成功、写入/回读顺序、十秒容差和当前主机秒数，不回写网络旧秒数。
 // Retain partial success, write/read ordering, ten-second tolerance and current host time rather than stale network seconds.
 for(int error=ESP_FAIL;error<=ESP_ERR_NOT_FOUND;++error){
  reset();manual_trace=true;manual_error=error;app_time_sync_result_t r=app_time_service_sync_manual();
  assert(r.error==error&&!r.network_time_received&&!record_saves&&!saves&&!written_utc&&!strcmp(manual_events,"SN"));
 }
 reset();manual_trace=true;write_error=ESP_FAIL;app_time_sync_result_t r=app_time_service_sync_manual();
 assert(r.error==ESP_FAIL&&r.network_time_received&&!record_saves&&!saves&&!strcmp(manual_events,"SNW"));
 reset();manual_trace=true;read_error=ESP_ERR_TIMEOUT;r=app_time_service_sync_manual();
 assert(r.error==ESP_ERR_TIMEOUT&&r.network_time_received&&!record_saves&&!saves&&!strcmp(manual_events,"SNWR"));
 for(int fault=0;fault<4;++fault){
  reset();manual_trace=true;pmu_absent=fault==0;the_pmu.time_synced=fault!=1;pmu_offset=fault==2?-1:fault==3?11:0;
  r=app_time_service_sync_manual();assert(r.error==ESP_ERR_INVALID_RESPONSE&&r.network_time_received&&!record_saves&&!saves&&!strcmp(manual_events,"SNWRP"));
 }
 for(int offset=0;offset<=10;offset+=10){
  reset();manual_trace=true;pmu_offset=offset;s_boot_attempts=3;r=app_time_service_sync_manual();
  assert(r.error==ESP_OK&&r.network_time_received&&record_saves==1&&saves==1&&s_last_record==(uint32_t)wall&&stored==(uint32_t)wall);
  assert(!s_boot_attempts&&s_next_attempt_us==mono+CLOCK_RETRY_US&&!strcmp(manual_events,"SNWRPLOVCX"));
 }
 puts("PASS: manual sync preserves partial network success, fresh host seconds, PMU write/read validation and success-only NVS bookkeeping");
 puts("PASS: actual scheduler on a flat six-hour cadence (online link reused, offline dialed), the settings last-sync record refreshed on every success, idle gating, single temporary worker, AP/upload exclusion, no-credential/OOM/error backoff and real-sync-only PMU/NVS handoff");
}
'''
lease=common+r'''
static bool s_config_busy,s_time_active,s_time_automatic,s_time_cancel,s_stopping;
static uint32_t s_time_pending_utc;
enum {WIFI_TRANSFER_STOPPED,WIFI_TRANSFER_READY};
static struct {int state;} s_status;
static int impl_result,impl_calls,cancel_reads;
static bool cancel_time_sync(bool automatic_only){++cancel_reads;return !s_time_active||(!automatic_only||s_time_automatic);}
static esp_err_t sync_time_impl(uint32_t *utc){assert(s_time_active&&s_config_busy);++impl_calls;*utc=wall;return impl_result;}
static esp_err_t sync_time_online_impl(uint32_t *utc){return sync_time_impl(utc);}
'''+function('sync_time_run',root/'components/wifi_transfer/wifi_transfer.c')+r'''
int main(void){uint32_t utc;
 assert(sync_time_run(NULL,false,true)==ESP_ERR_INVALID_ARG);
 assert(sync_time_run(&utc,false,true)==ESP_OK&&impl_calls==1&&s_time_pending_utc==wall);
 assert(!s_time_active&&!s_config_busy);
 s_config_busy=true;assert(sync_time_run(&utc,false,true)==ESP_ERR_INVALID_STATE&&impl_calls==1);s_config_busy=false;
 s_time_active=true;assert(sync_time_run(&utc,true,true)==ESP_ERR_INVALID_STATE);assert(sync_time_run(&utc,false,false)==ESP_ERR_INVALID_STATE);s_time_active=false;
 s_time_pending_utc=0;impl_result=ESP_FAIL;assert(sync_time_run(&utc,true,false)==ESP_FAIL&&!s_time_pending_utc&&!s_time_active&&!s_config_busy);
 s_stopping=true;s_status.state=WIFI_TRANSFER_READY;assert(sync_time_run(&utc,false,true)==ESP_ERR_INVALID_STATE);
 s_status.state=WIFI_TRANSFER_STOPPED;assert(sync_time_run(&utc,false,true)==ESP_FAIL);
 puts("PASS: SNTP/config share one admission lock; active operations cannot be reentered, failed work publishes no success, stopped network can calibrate");
}
'''
ntp=common+r'''
static bool s_time_cancel;
static int inits,deinits,waits,cancel_at,success_at,init_error;
typedef struct {int count;const char *servers[3];} esp_sntp_config_t;
#define ESP_SNTP_SERVER_LIST(...) {__VA_ARGS__}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(n,list) {.count=n,.servers=list}
static bool time_sync_cancelled(void){return s_time_cancel;}
static int esp_netif_sntp_init(esp_sntp_config_t *c){assert(c->count==3&&!strcmp(c->servers[0],"203.107.6.88"));++inits;return init_error;}
static int esp_netif_sntp_sync_wait(int ms){mono+=ms*1000;++waits;if(waits==cancel_at)s_time_cancel=true;return waits==success_at?ESP_OK:ESP_ERR_TIMEOUT;}
static void esp_netif_sntp_deinit(void){++deinits;}
'''+function('time_sync_ntp',root/'components/wifi_transfer/wifi_transfer.c')+r'''
static void reset(void){mono=0;s_time_cancel=false;inits=deinits=waits=cancel_at=success_at=init_error=0;}
int main(void){uint32_t utc=0;
 reset();success_at=2;assert(time_sync_ntp(&utc)==ESP_OK&&utc==wall&&inits==1&&deinits==1&&waits==2);
 reset();s_time_cancel=true;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_STATE&&!inits&&!deinits);
 reset();cancel_at=2;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_STATE&&deinits==1&&mono==200000);
 reset();assert(time_sync_ntp(&utc)==ESP_ERR_TIMEOUT&&deinits==1&&mono==20000000);
 reset();init_error=ESP_FAIL;assert(time_sync_ntp(&utc)==ESP_FAIL&&!deinits&&!waits);
 reset();success_at=1;wall=1;assert(time_sync_ntp(&utc)==ESP_ERR_INVALID_RESPONSE&&deinits==1);
 puts("PASS: actual SNTP helper has one init/deinit, valid UTC only,100ms cancellation checkpoints and20s bounded timeout; failed init never tears down an unowned singleton");
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-clock-test-',dir='/private/tmp') as folder:
 for name,unit in [('restore',restore),('scheduler',scheduler),('lease',lease),('ntp',ntp)]:
  c=Path(folder)/(name+'.c');binary=Path(folder)/name;c.write_text(unit)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
