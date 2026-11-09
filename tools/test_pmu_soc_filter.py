#!/usr/bin/env python3
"""中文：Metalio 电量滤波状态机对拍：抗抖、单向、限速、判满、开机恢复与负载补偿。
English: Metalio battery filter replay: noise immunity, one-direction stepping, rate
limit, charge-done snap, boot restore and load compensation."""
from pathlib import Path
import subprocess, tempfile, re
from run_reader_baseline import toolchain
root=Path(__file__).resolve().parents[1]
source=(root/'components/device_power/device_power_metalio.c').read_text()
def extract(name):
    m=re.search(r'^(?:static )?[^\n]+\b'+name+r'\([^;{}]*?\)\s*\{',source,re.M)
    assert m, name
    n=m.end();level=1
    while level:
        if source[n]=='{':level+=1
        elif source[n]=='}':level-=1
        n+=1
    return source[m.start():n]
defines='\n'.join(re.findall(r'^#define BATT_.*$',source,re.M))
curve=re.search(r'^static const int32_t s_soc_curve\[.*?\};',source,re.M|re.S).group(0)
typedef=re.search(r'typedef struct \{[^}]*\} batt_filter_t;',source,re.S).group(0)
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    code='#include <assert.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n'
    code+=defines+'\n'+curve+'\n'+typedef+'\n'
    code+='\n'.join(extract(f) for f in ['batt_compensated_mv','batt_curve_soc','batt_filter_step','batt_full_now'])
    code+='''
static batt_filter_t f;
static int32_t s_mv;
static int32_t s_pm;
static void reset(void) { memset(&f, 0, sizeof(f)); }
static void step(int64_t t, int mv, int ma, bool have_i, bool chg, bool done, int saved) {
 batt_filter_step(&f, t, (uint16_t)mv, have_i, (int16_t)ma, chg, done, saved, &s_mv, &s_pm);
}
static int64_t s_since;
static bool full(int64_t t, int mv, int ma, bool chg, bool done) {
 return batt_full_now(done, chg, true, mv, true, (int16_t)ma, t, &s_since);
}
int main(void) {
 // 插拔时 CX 与 BQ 可不同步；补偿必须跟随有符号电流。
 // CX and BQ can disagree during cable transitions; compensation follows signed current.
 reset(); step(0, 3866, -87, true, true, false, -1); assert(s_mv==3881);
 reset(); step(0, 4174, 868, true, false, false, -1); assert(s_mv==4024);
 // 1) 瞬时 ±8mV 抖动整段不动显示 / instantaneous +-8mV noise never moves the display
 reset();
 int first=-1;
 for (int i=0;i<600;i++) {
  step((int64_t)i*200000, 3900+((i&1)?8:-8), -30, true, false, false, -1);
  if (first<0) first=s_pm; else assert(s_pm==first);
 }
 // 2) 放电单调不升 / discharge never rises
 reset();
 int prev=1<<30;
 for (int i=0;i<1400;i++) {
  step((int64_t)i*1000000, 4000-i/2, -40, true, false, false, -1);
  assert(s_pm<=prev);
  prev=s_pm;
 }
 assert(s_pm<560);
 // 3) 充电单调不降 / charge never falls
 reset();
 prev=0;
 for (int i=0;i<1400;i++) {
  step((int64_t)i*200000, 3700+i/2, 500, true, true, false, -1);
  assert(s_pm>=prev);
  prev=s_pm;
 }
 // 4) 正常电量限速：至少隔 60s、单步最多 1% / normal range: >=60s apart, <=1% per step
 reset();
 step(0, 4000, 0, false, false, false, -1);
 assert(s_pm==695);
 int64_t last_t=0;
 int changes=0;
 prev=695;
 for (int i=1;i<=6300;i++) {
  step((int64_t)i*200000, 3700, 0, false, false, false, -1);
  if (s_pm!=prev) {
   assert((int64_t)i*200000-last_t>=60000000);
   assert(prev-s_pm>0 && prev-s_pm<=10);
   changes++;
   last_t=(int64_t)i*200000;
   prev=s_pm;
  }
 }
 assert(changes>=10);
 // 5) 判满对齐 100%，电压门槛挡误判 / done snaps to 100%, voltage gate guards misreads
 reset();
 step(0, 4380, 0, false, true, true, -1);
 assert(s_pm==1000);
 reset();
 step(0, 4000, 0, false, true, true, -1);
 assert(s_pm==695);
 // 6) 开机恢复容差 / boot restore tolerance
 reset(); step(0, 3900, 0, false, false, false, 540); assert(s_pm==540);
 reset(); step(0, 3900, 0, false, false, false, 200); assert(s_pm==batt_curve_soc(3900));
 reset(); step(0, 3900, 0, false, false, false, -1);  assert(s_pm==batt_curve_soc(3900));
 // 7) EMA 收敛到补偿后电压 / EMA converges on the compensated voltage
 reset();
 step(0, 3950, 0, false, false, false, -1);
 for (int i=1;i<900;i++) step((int64_t)i*200000, 3900, 0, false, false, false, -1);
 assert(s_mv>=3899 && s_mv<=3901);
 // 8) 负载补偿方向与封顶 / load compensation direction and cap
 reset(); step(0, 3800, -3000, true, false, false, -1); assert(s_mv==3950);
 reset(); step(0, 3800, -800, true, false, false, -1);  assert(s_mv==3944);
 reset(); step(0, 3800, 800, true, true, false, -1);   assert(s_mv==3656);
 // 9) 涓流判满 / taper-full
 s_since=-1;
 assert(full(0,4380,0,true,false)==false);              // 立即不算满 / not full instantly
 assert(full(299999999,4380,10,true,false)==false);    // 差 1µs 不算 / 1µs short stays unfilled
 assert(full(300000000,4380,10,true,false)==true);     // 满 5 分钟判满 / held 5 min: full
 assert(full(300000001,4380,-80,true,false)==false && s_since==-1); // 明显放电撤销确认 / discharge restarts confirmation
 s_since=-1;
 assert(full(100000000,4380,-80,true,false)==false && s_since==-1);
 assert(full(400000000,4380,5,true,false)==false);     // 放电不算尾充 / discharge cannot count as taper
 assert(full(700000000,4380,5,true,false)==true);
 s_since=-1;
 assert(full(0,4380,117,true,false)==false&&s_since==-1);  // 大电流重置 / real charge current resets
 assert(full(0,4340,0,true,false)==false&&s_since==-1);    // 电压不够重置 / low voltage resets
 assert(full(0,4380,0,false,false)==false&&s_since==-1);   // 未充电重置 / not charging resets
 assert(full(0,4380,0,true,true)==true);                   // CX done 位优先 / a real done bit wins
 // 中途真充电清窗后重新累计 / a mid-window real charge restarts the accrual
 s_since=-1;
 assert(full(0,4380,5,true,false)==false);
 assert(full(60000000,4380,80,true,false)==false&&s_since==-1);
 assert(full(60000001,4380,5,true,false)==false);
 assert(full(360000001,4380,5,true,false)==true);
 // 判满对齐穿过整只滤波器：涓流窗口内 done 仍为假，窗口满后轮询侧喂 done=true，
 // 显示对齐 100% / taper-full through the whole filter: done stays false inside the
 // window, the poll side feeds done=true once it completes, and the display snaps
 reset();
 step(0, 4370, 10, true, true, false, -1);
 step(299999999, 4370, 10, true, true, false, -1);
 assert(s_pm<1000);
 step(300000000, 4370, 10, true, true, true, -1);
 assert(s_pm==1000);
 // 10) 满电平台与拔线瞬间（2026-10-25 卡上日志）
 // Full plateau and the unplug instant (2026-10-25 card log)
 reset(); step(0, 4374, 0, false, false, false, -1); assert(s_pm>=990);   // 平台下沿 / plateau low edge
 reset(); step(0, 4380, 5, true, false, false, -1);  assert(s_pm>=990);   // 平台中位 / plateau median
 reset(); step(0, 4380, 10, true, true, false, -1);  assert(s_pm>=990);   // 涓流窗口内 / inside the taper window
 reset(); step(0, 4382, 8, true, true, true, -1);    assert(s_pm==1000);  // 判满 / full
 step(60000000, 4351, -186, true, false, false, -1); assert(s_pm==1000); // 拔线第一分钟不掉 / stays full off the charger
 step(120000000, 4345, -96, true, false, false, -1); assert(s_pm>=970);  // 之后随负载电压缓降 / then eases down under load
 // 11) 中段锚点回充对账（2026-10-27 卡上日志）
 // Mid-anchor refill accounting (2026-10-27 card log)
 // 静置 4305 -> 曲线 957‰，回充实测 ~955‰ / resting 4305 -> curve 957‰, refill measured ~955‰
 reset(); step(0, 4305, 0, false, false, false, -1); assert(s_pm>=950 && s_pm<=965);
 // 静置 4241(-100mA 补偿后 4259) -> 曲线 922‰，回充腿（~93mAh≈955）容差带
 // resting 4241 (4259 compensated) -> curve 922‰, refill leg (~93 mAh ≈ 955) tolerance band
 reset(); step(0, 4259, 0, false, false, false, -1); assert(s_pm>=910 && s_pm<=940);
 // 深放底点 ocv 4091 -> 曲线 769‰，回充法 828、ic_soc 85 的容差带
 // deep-discharge floor ocv 4091 -> curve 769‰, refill 828 / ic_soc 85 tolerance band
 reset(); step(0, 4091, -91, true, false, false, -1); assert(s_pm>=755 && s_pm<=845);
 // 4100/4200/4300 网格值抽查（回充腿与积分对账的中位点）
 // grid spot checks for 4100/4200/4300 (midpoints reconciled by refill legs and integrals)
 reset(); step(0, 4300, 0, false, false, false, -1); assert(s_pm==955);
 reset(); step(0, 4200, 0, false, false, false, -1); assert(s_pm==875);
 reset(); step(0, 4100, 0, false, false, false, -1); assert(s_pm==790);
 // 12) 2026-11-05 连续区间的相对容量，不把芯片 SOC 当真值。
 // Relative sampled capacity from the 2026-11-05 continuous segment, not gauge SOC truth.
 const int mv[] = {4100,4050,4000,3950};
 const int q[] = {326016,442395,521732,656160};
 for (unsigned i=0;i<4;i++) {
  int relative=790-(q[i]-q[0])*1000LL/2050000;
  int error=batt_curve_soc(mv[i])-relative;
  assert(error>=-1 && error<=1);
 }
 assert(batt_curve_soc(3600)==140 && batt_curve_soc(3500)==80);
 assert(batt_curve_soc(3400)==40 && batt_curve_soc(3300)==0);
 for(int v=2501;v<=5000;v++) {
  int delta=batt_curve_soc(v)-batt_curve_soc(v-1);
  assert(delta>=0 && delta<=2);
 }
 // 13) 插拔重新计时，正常区间不在几秒内连跳；低电量不受一分钟限制。
 // Rearm on transitions; no immediate normal-range jumps, but low levels retain fast tracking.
 reset();step(0,4000,0,true,true,false,-1);
 int before=s_pm;
 step(60000000,3960,-100,true,false,false,-1);assert(s_pm==before);
 step(119999999,3960,-100,true,false,false,-1);assert(s_pm==before);
 step(120000000,3960,-100,true,false,false,-1);assert(s_pm==before-10);
 reset();step(0,3450,0,true,false,false,-1);
 before=s_pm;step(10000000,3300,0,true,false,false,-1);assert(s_pm==before-10);
 reset();step(0,3300,0,true,false,false,5);
 step(10000000,3300,0,true,false,false,-1);assert(s_pm==0);
 // 有符号极值不会溢出，无电流时不补偿。/ Signed extremes cannot overflow; missing current disables compensation.
 assert(batt_compensated_mv(4000,true,INT16_MIN)==4150);
 assert(batt_compensated_mv(4000,true,INT16_MAX)==3850);
 assert(batt_compensated_mv(4000,false,INT16_MIN)==4000);
 puts("PASS: battery filter noise immunity, monotonic one-direction stepping, rate limit, charge-done snap, boot restore tolerance, EMA convergence, load compensation, taper-full and the full-charge plateau");
}
'''
    (p/'test.c').write_text(code)
    (p/'toolchain').mkdir()
    env,_=toolchain(p/'toolchain')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    str(p/'test.c'),'-o',str(p/'test')],env=env,check=True)
    subprocess.run([str(p/'test')],env=env,check=True)
