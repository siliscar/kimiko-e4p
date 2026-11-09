// 宿主测试桩：默认采样任务不可用，主循环走降级直读分支（与 F4 之前行为一致），
// 用于回归验证 app_loop 的 ingest/dispatch 管线抽取。测试可显式启用下方队列，
// 模拟刷屏阻塞期间的积压帧与回放。真机构建链接真实 touch_sampler.c，本文件不参与。
// / Host test stub: by default the sampler task is unavailable so the loop takes the
// direct-read fallback (identical to pre-F4 behavior), regression-testing the
// ingest/dispatch extraction in app_loop. A test may enable the queue below to
// simulate frames buffered while a refresh blocks the loop, then replayed. Device
// builds link the real touch_sampler.c; this file is not part of them.
#include "touch_sampler.h"

#define TEST_QUEUE_LEN 32
static bool s_test_active;
static touch_frame_t s_test_queue[TEST_QUEUE_LEN];
static unsigned s_test_rd, s_test_wr;

bool touch_sampler_start(cst836u_handle_t tp) { (void)tp; return false; }
bool touch_sampler_active(void) { return s_test_active; }
unsigned touch_sampler_pending(void) { return s_test_wr - s_test_rd; }
bool touch_sampler_pop(touch_frame_t* out) {
    if (s_test_rd == s_test_wr) return false;
    *out = s_test_queue[s_test_rd++ % TEST_QUEUE_LEN];
    return true;
}

// 与真实实现同语义：前视不移动读指针，丢弃只推进读指针。
// Same semantics as the real sampler: peek does not move the cursor, discard only advances it.
bool touch_sampler_peek(unsigned offset, touch_frame_t* out) {
    if (offset >= s_test_wr - s_test_rd) return false;
    *out = s_test_queue[(s_test_rd + offset) % TEST_QUEUE_LEN];
    return true;
}

void touch_sampler_discard(unsigned count) {
    unsigned pending = s_test_wr - s_test_rd;
    if (count > pending) count = pending;
    s_test_rd += count;
}

// 测试钩子：启用后主循环改走队列；push 按时间递增写入一帧。
// / Test hooks: enabling switches the loop to the queue; push appends one frame.
void touch_sampler_test_enable(bool active) {
    s_test_active = active;
    s_test_rd = s_test_wr = 0;
}

void touch_sampler_test_push(int64_t t_ms, uint16_t x, uint16_t y, uint8_t count) {
    s_test_queue[s_test_wr++ % TEST_QUEUE_LEN] =
        (touch_frame_t){.t_ms = t_ms, .x = x, .y = y, .count = count, .touched = count != 0};
}
