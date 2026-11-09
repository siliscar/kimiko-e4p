/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 图书进度与连续甩动门槛测试。/ Book progress and continuous-shake gate tests.
 */
#include <assert.h>
#include <stdio.h>
#include "book_policy.h"

/* 50ms 采样节奏与真机一致。/ The 50ms cadence matches the on-device sampler. */
static int feed(book_shake_gate_t *g, int x, int y, int z, bool suppressed, int t) {
    return book_shake_feed(g, x, y, z, suppressed, t);
}

int main(void) {
    assert(book_position_bytes(100, 200, 50, 100) == 150);
    assert(book_position_bytes(100, 200, 150, 100) == 200);
    assert(book_position_bytes(100, 200, 0, 0) == 100);
    assert(book_position_bytes(0, UINT32_MAX, UINT32_MAX, UINT32_MAX) == UINT32_MAX);
    book_shake_gate_t g = {0};
    // 任意持机角度先锚定基线；一次向右甩动在 +250mg 上穿沿翻下一页，同向后续帧不重复触发。
    // Anchor at any holding angle first; one right swing turns forward on the +250mg crossing
    // edge and later frames of the same swing never retrigger.
    for (int t = 0; t <= 400; t += 50) assert(feed(&g, 180, -600, -760, false, t) == 0);
    assert(feed(&g, 300, -600, -760, false, 450) == 0);
    assert(feed(&g, 500, -600, -760, false, 500) == 1);
    assert(feed(&g, 700, -600, -760, false, 550) == 0);
    assert(feed(&g, 850, -600, -760, false, 600) == 0);
    // 回摆半程落在不应期内。/ The rebound half-swing lands inside the refractory window.
    assert(feed(&g, 200, -600, -760, false, 850) == 0);
    assert(feed(&g, -450, -600, -760, false, 1000) == 0);
    assert(feed(&g, -180, -600, -760, false, 1150) == 0);
    // 不应期结束后独立的一下向左甩翻上一页。/ After the refractory, an independent left swing turns back.
    for (int t = 1200; t <= 1850; t += 50) assert(feed(&g, -180, -600, -760, false, t) == 0);
    assert(feed(&g, -700, -600, -760, false, 1900) == -1);
    assert(feed(&g, 180, -600, -760, false, 2100) == 0);
    // 连续甩动：左右交替的每一摆各触发一次，不要求中间回到静止。
    // Continuous shaking: each alternating swing fires on its own without needing stillness between.
    g = (book_shake_gate_t){0};
    for (int t = 0; t <= 400; t += 50) assert(feed(&g, 0, 0, -1000, false, t) == 0);
    int forward = 0, back = 0;
    static const int wave[] = {0, 500, 900, 500, 0, -500, -900, -500};
    for (int i = 0; i < 21; ++i) {
        // 半周期 300ms 的对称摆动，峰值 ±900。/ A symmetric 300ms half-period swing, ±900 peaks.
        int dir = feed(&g, wave[i % 8], 0, -1000, false, 500 + (int64_t)i * 75);
        if (dir == 1) ++forward;
        if (dir == -1) ++back;
    }
    assert(forward >= 1 && back >= 1 && forward + back <= 4);
    // 慢倾斜：基线跟随，永远不触发。/ Slow tilt: the baseline follows and nothing ever fires.
    g = (book_shake_gate_t){0};
    for (int t = 0; t <= 10000; t += 50) assert(!feed(&g, (t - 5000) / 8, (t - 5000) / 16, -1000, false, t));
    // 恒定 1g 的纯旋转，50ms 转 1.5 度；两秒转过 60 度不应翻页。
    // Pure constant-1g rotation at 1.5 degrees per 50ms must not turn over a 60-degree tilt.
    for (int sign=-1;sign<=1;sign+=2) {
        g=(book_shake_gate_t){0};
        for(int t=0;t<=500;t+=50)assert(!feed(&g,0,0,-1000,false,t));
        int64_t x=0,z=-1000000;
        for(int t=550;t<=2500;t+=50){
            int64_t next_x=(x*999657-z*26177)/1000000;
            z=(x*26177+z*999657)/1000000;x=next_x;
            assert(!feed(&g,sign*(int)(x/1000),0,(int)(z/1000),false,t));
        }
    }
    // 平滑甩动保留灵敏度，变化速率按真实间隔判定。/ Rounded swings retain sensitivity with time-normalized slew.
    const int rounded[]={0,80,160,230,300,360,390,360,300,230,160,80,0};
    for(int sign=-1;sign<=1;sign+=2){
        g=(book_shake_gate_t){0};
        for(int t=0;t<=400;t+=50)assert(!feed(&g,0,-900,-400,false,t));
        int turns=0;
        for(unsigned i=0;i<sizeof(rounded)/sizeof(*rounded);++i){
            int dir=feed(&g,sign*rounded[i],-900,-400,false,450+(int)i*50);
            if(dir){assert(dir==sign);++turns;}
        }
        assert(turns==1);
    }
    // 纵向甩动不触发。/ Vertical swings never fire.
    g = (book_shake_gate_t){0};
    for (int i = 0; i < 6; ++i) {
        int64_t t = 2000 + (int64_t)i * 200;
        assert(!feed(&g, 0, i % 2 ? 800 : -800, -1000, false, t));
    }
    // x 越限但 y 占优不触发。/ An x crossing with y dominant is rejected.
    assert(feed(&g, 0, 0, -1000, false, 3300) == 0); /* 断档重新锚定 / gap re-anchor */
    assert(feed(&g, 0, 0, -1000, false, 3350) == 0);
    assert(!feed(&g, 500, 700, -1000, false, 3400));
    assert(!feed(&g, 0, 0, -1000, false, 3450));
    // z 轴大幅度摆动与 x/y 无关。/ Large z swings are irrelevant to x/y.
    for (int i = 0; i < 6; ++i)
        assert(!feed(&g, 30, 12, i % 2 ? 200 : -2200, false, 4000 + (int64_t)i * 200));
    // 触摸压制帧重新锚定；静置期内的大步进与回落穿越都不得触发，不继承半个动作。
    // Suppressed frames re-anchor; within the settle window neither a large step nor the
    // return crossing may fire, so no half gesture carries over.
    assert(!feed(&g, 700, 0, -1000, true, 5000));
    assert(!feed(&g, 400, 0, -1000, false, 5050));
    assert(!feed(&g, 180, 0, -1000, false, 5100));
    assert(!feed(&g, 180, 0, -1000, false, 5150));
    assert(!feed(&g, 500, 0, -1000, false, 5200));
    assert(!feed(&g, 180, 0, -1000, false, 5250));
    assert(!feed(&g, 180, 0, -1000, false, 5300));
    // 断档与时钟回退都重新锚定，不让旧方向误触发。/ Gaps and clock resets re-anchor without firing.
    assert(!feed(&g, 700, 0, -1000, false, 6000));
    assert(!feed(&g, -700, 0, -1000, false, 20));
    assert(!book_shake_feed(NULL, 0, 0, 0, false, 0));
    puts("PASS book position and continuous shake: edges, refractory, no-stillness chaining, tilt, other axes, suppression");
}
