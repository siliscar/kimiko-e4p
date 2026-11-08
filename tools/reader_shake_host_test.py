"""中文：使用真实阅读轮询及手势机验证持触屏蔽、抬手恢复与休眠重试。
English: Test held-touch suppression, release recovery and sleep retries with the real tick and gesture engine.
"""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function

ROOT = Path(__file__).resolve().parents[1]
book = ROOT / 'main/apps/app_book.c'
unit = r'''
#include <assert.h>
#include <stdio.h>
#include "ui_gesture.h"
#include "book_policy.h"
#include "reader_orientation.h"
static reader_orientation_t s_orientation;
static int64_t s_orientation_retry_ms;
static display_orientation_t direction;
static unsigned rotations;
display_orientation_t display_orientation_get(void){return direction;}
static bool reader_orientation_apply(display_orientation_t next){direction=next;++rotations;return true;}
typedef enum {APP_REDRAW_NONE,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
typedef struct {int x_mg,y_mg,z_mg;} sc7a20h_sample_t;
typedef struct {int unused;} sc7a20h_sensor_config_t;
#define ESP_OK 0
#define READER_SHAKE_POLL_MS 50
#define READER_SHAKE_TOUCH_QUIET_MS 600
#define READER_SHAKE_CFG_BACKOFF_MS 2000
static const sc7a20h_sensor_config_t READER_SHAKE_SENSOR_CONFIG={0};
static book_shake_gate_t s_shake_gate;
static int64_t s_shake_touch_ms,s_stats_activity_ms;
static bool s_shake_sensor_on=true,s_shake_cfg_pending;
static bool sensor_powered=true,sleep_fail,body_visible=true;
static unsigned sleeps,turns;
static sc7a20h_sample_t next_sample;
static bool shake_enabled=true;
static bool app_settings_reader_shake_turn(void){return shake_enabled;}
static bool app_book_reader_body_visible(void){return body_visible;}
static void board_sensor_sleep(void *acc){(void)acc;++sleeps;if(!sleep_fail)sensor_powered=false;}
static bool sc7a20h_powered(void *acc){return acc&&sensor_powered;}
static int sc7a20h_apply_config(void *acc,const sc7a20h_sensor_config_t *cfg){(void)acc;(void)cfg;sensor_powered=true;return ESP_OK;}
static int board_accel_read(void *acc,sc7a20h_sample_t *sample){(void)acc;*sample=next_sample;return ESP_OK;}
static app_redraw_t turn_page(app_ctx_t *ctx,int dir){(void)ctx;assert(dir==1||dir==-1);++turns;return APP_REDRAW_PAGE;}
'''
for name in ('reader_shake_screen_axes', 'reader_shake_tick'):
    unit += function(name, book) + '\n'
unit += r'''
int main(void){
 ui_gesture_t gesture={0};ui_gesture_event_t event;
 app_touch_t touch={.touched=true,.count=1,.x=200,.y=120};
 app_ctx_t ctx={.touch=&touch,.acc=&gesture};
 for(int pass=0;pass<2;++pass){
  int start=3000+pass*4000;ui_gesture_reset(&gesture);s_shake_gate=(book_shake_gate_t){0};
  touch.touched=true;touch.count=1;turns=0;
  for(int t=start;t<=start+2000;t+=50){
   ctx.now_ms=t;ctx.pressed=t==start;
   if(pass&&t>=start+100)touch.count=2;
   if(ui_gesture_feed(&gesture,&ctx,&event))s_shake_touch_ms=t;
   next_sample=(sc7a20h_sample_t){.z_mg=-1000,.x_mg=t==start+1500?-600:0};
   reader_shake_tick(&ctx);
  }
  assert(!turns&&touch.touched);
  touch.touched=false;touch.count=0;
  for(int t=start+2050;t<=start+3000;t+=50){
   ctx.now_ms=t;
   next_sample=(sc7a20h_sample_t){.z_mg=-1000,.x_mg=t==start+2250||t==start+2950?-600:0};
   reader_shake_tick(&ctx);
   if(t<start+2600)assert(!turns);
  }
  assert(turns==1);
 }
 body_visible=false;sleep_fail=true;s_shake_sensor_on=true;sleeps=0;
 for(int t=12000;t<=17000;t+=50){ctx.now_ms=t;reader_shake_tick(&ctx);}
 assert(sleeps>=2&&sleeps<=4&&s_shake_sensor_on&&sensor_powered);
 sleep_fail=false;ctx.now_ms=20000;reader_shake_tick(&ctx);assert(!sensor_powered&&!s_shake_sensor_on);
 puts("PASS: held single/multitouch suppression, release quiet window and recovery, bounded sleep retries");
 // 自动旋转单独开启也负责采样；触摸/词典冻结候选，锁定且晃动关闭后交还传感器。
 // Auto-rotation alone owns sampling; touch/dictionary freeze candidates and disabling both controls returns the sensor.
 shake_enabled=false;body_visible=true;s_orientation.automatic=true;turns=0;touch.touched=false;
 next_sample=(sc7a20h_sample_t){.x_mg=-1000};
 for(int t=27000;t<=27500;t+=50){ctx.now_ms=t;app_redraw_t out=reader_shake_tick(&ctx);assert(out==(t==27500?APP_REDRAW_FULL:APP_REDRAW_NONE));}
 assert(rotations==1&&direction==DISPLAY_ORIENTATION_RIGHT&&s_orientation.locked==direction&&!turns&&sensor_powered);
 next_sample=(sc7a20h_sample_t){.x_mg=1000};touch.touched=true;
 for(int t=28000;t<=29000;t+=50){ctx.now_ms=t;assert(reader_shake_tick(&ctx)==APP_REDRAW_NONE);}
 touch.touched=false;s_dictionary_view.open=true;
 for(int t=30000;t<=31000;t+=50){ctx.now_ms=t;assert(reader_shake_tick(&ctx)==APP_REDRAW_NONE);}
 assert(rotations==1&&!s_orientation.candidate);
 s_dictionary_view.open=false;s_orientation.automatic=false;ctx.now_ms=32000;reader_shake_tick(&ctx);assert(!sensor_powered);
 const int expected[][2]={{-100,200},{200,100},{100,-200},{-200,-100}};
 sc7a20h_sample_t axes={100,200,300};
 for(int d=0;d<4;++d){direction=d;int x,y,z;reader_shake_screen_axes(&axes,&x,&y,&z);assert(x==expected[d][0]&&y==expected[d][1]&&z==300);}
 puts("PASS: auto-only sensor ownership, stable reflow request, touch/dictionary suppression, lock sleep and shake axes in all orientations");
}
'''
with tempfile.TemporaryDirectory(dir=ROOT/'build') as folder:
    path = Path(folder)
    stub = (ROOT/'tools/ui_gesture_stubs/app.h').read_text().replace('bool pressed, released, consumed;', 'bool pressed, released, consumed; void *acc;')
    (path/'app.h').write_text(stub)
    (path/'test.c').write_text(unit)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I'+str(path), '-I'+str(ROOT/'tools/ui_gesture_stubs'), '-I'+str(ROOT/'main/ui'), '-I'+str(ROOT/'main/book'),
                    str(path/'test.c'), str(ROOT/'main/ui/ui_gesture.c'), str(ROOT/'main/book/reader_orientation.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
