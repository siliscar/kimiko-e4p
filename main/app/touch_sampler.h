#pragma once
// F4 触摸采样任务：主循环被刷屏等阻塞期间持续采样 CST836U，帧入 SPSC 环形队列；
// 主循环恢复后排空回放，触摸边沿不丢。设计与验证规程见
// docs/local/2026-10-14-f4-touch-sampler-plan.md。
// / F4 touch sampler task: keeps sampling the CST836U while the main loop is
// blocked (refresh etc.), pushing frames into an SPSC ring; the loop drains and
// replays them on wake so press/release edges survive. See the F4 plan doc.

#include <stdbool.h>
#include <stdint.h>

#include "cst836u.h"

// 单帧 16 字节：手势机与去抖全部以 ms 粒度工作（长按 500ms 等窗口均用
// ctx.now_ms），µs 精度无消费者。t_ms 保留 esp_timer 毫秒全宽：回放会把帧时间戳
// 写回 last_input_ms，32 位回绕会让连续开机 49.7 天后的自动休眠计时直接判成超时。
// / 16-byte frame: every consumer works at ms granularity. t_ms keeps the full-width
// esp_timer ms value: replay feeds frame timestamps back into last_input_ms, where a
// 32-bit wrap would read as an expired idle timeout after 49.7 days of uptime.
typedef struct {
    int64_t t_ms;
    uint16_t x;
    uint16_t y;
    uint8_t count;
    uint8_t touched;
} touch_frame_t;

// 启动采样任务（core 1 / prio 4 / 5ms 节拍）。false = 创建失败，主循环回退直读。
// / Start the sampler task (core 1, prio 4, 5ms tick). false = creation failed;
// the main loop falls back to direct cst836u_read.
bool touch_sampler_start(cst836u_handle_t tp);

// 任务是否在跑（决定主循环走队列还是直读）。/ Whether the sampler task is live.
bool touch_sampler_active(void);

// 当前积压帧数（读者视角，含覆盖钳制）。/ Frames buffered, reader's view.
unsigned touch_sampler_pending(void);

// 非阻塞弹出一帧；false = 队列空。仅主循环（唯一读者）调用。
// / Non-blocking pop; false when empty. Main loop is the only reader.
bool touch_sampler_pop(touch_frame_t* out);

// 前视：读取距队首 offset 帧（0 = 下一帧将弹出的帧），不移动读指针；越界返回 false。
// 仅主循环（唯一读者）调用。连翻打断用它扫描积压里是否已出现别的按键按下沿。
// / Peek: read the frame `offset` ahead of the read cursor (0 = the next frame a pop
// would return) without consuming it; false when out of range. The main loop is the
// only reader. Backlog interruption uses it to look for a newer key press.
bool touch_sampler_peek(unsigned offset, touch_frame_t* out);

// 丢弃队首 count 帧（不超过当前积压），供连翻打断跳过已作废的同向点击。
// / Drop up to `count` queued frames from the head, so a backlog interruption can skip
// taps that the newest key press has made obsolete.
void touch_sampler_discard(unsigned count);
