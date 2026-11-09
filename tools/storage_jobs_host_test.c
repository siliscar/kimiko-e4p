/*
 * 中文：编译真实交还服务，验证短路顺序与每个任务独立接收完整超时值。
 * English: Compile the real handoff service and verify short-circuit order and full per-job timeouts.
 */
#include "storage_jobs.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static bool cover_ok, avatar_ok;
static unsigned expected_timeout, calls;

bool book_cover_jobs_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 0);
    return cover_ok;
}

bool ui_avatar_quiesce(unsigned timeout_ms) {
    assert(timeout_ms == expected_timeout && calls++ == 1);
    return avatar_ok;
}

int main(void) {
    const unsigned timeouts[] = {0, 1, 2000, UINT_MAX};
    for (unsigned i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        expected_timeout = timeouts[i];
        for (unsigned cover = 0; cover < 2; ++cover) {
            for (unsigned avatar = 0; avatar < 2; ++avatar) {
                cover_ok = cover;
                avatar_ok = avatar;
                calls = 0;
                assert(storage_jobs_quiesce(expected_timeout) == (cover_ok && avatar_ok));
                assert(calls == (cover_ok ? 2u : 1u));
            }
        }
    }
    puts("PASS: cover before avatar, short-circuit failure, repeated handoffs and per-job timeout propagation");
}
