/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "time_sync_policy.h"

int main(void) {
    const uint32_t now = 1787961600u;
    assert(app_time_sync_clock_valid(now));
    assert(app_time_sync_clock_valid(APP_TIME_SYNC_CLOCK_FLOOR));
    assert(!app_time_sync_clock_valid(APP_TIME_SYNC_CLOCK_FLOOR - 1));
    assert(!app_time_sync_clock_valid(0));

    // Never synced and never tried: due as soon as the clock is plausible.
    assert(app_time_sync_due(now, 0, 0));

    // Success boundary: exactly the interval is due, one second earlier is not.
    assert(!app_time_sync_due(now, now - APP_TIME_SYNC_INTERVAL_SEC + 1, 0));
    assert(app_time_sync_due(now, now - APP_TIME_SYNC_INTERVAL_SEC, 0));
    assert(app_time_sync_due(now, now - APP_TIME_SYNC_INTERVAL_SEC - 1, 0));

    // Failure cooldown holds the interval back even when the last success is old.
    assert(!app_time_sync_due(now, 0, now - APP_TIME_SYNC_RETRY_SEC + 1));
    assert(app_time_sync_due(now, 0, now - APP_TIME_SYNC_RETRY_SEC));
    assert(!app_time_sync_due(now, now - APP_TIME_SYNC_INTERVAL_SEC * 2, now - 60));

    // A fresh success blocks a sync even after a recent failed attempt.
    assert(!app_time_sync_due(now, now - 60, now - 60));

    // An invalid clock never reports due; callers fall back to one attempt per power cycle.
    assert(!app_time_sync_due(0, 0, 0));
    assert(!app_time_sync_due(APP_TIME_SYNC_CLOCK_FLOOR - 1, 0, 0));

    // A clock moved far back ignores stale records instead of stalling forever.
    const uint32_t back = now - APP_TIME_SYNC_BACKWARD_TOLERANCE_SEC - 1;
    assert(app_time_sync_due(back, now, now));
    // A small backwards step keeps them: no retry storm for clock jitter.
    assert(!app_time_sync_due(now - APP_TIME_SYNC_BACKWARD_TOLERANCE_SEC + 1, now, now));

    // Repeated attempts inside one power cycle end with the cap, so the radio cannot cycle forever.
    assert(APP_TIME_SYNC_ATTEMPTS_PER_BOOT >= 1 && APP_TIME_SYNC_ATTEMPTS_PER_BOOT <= 10);
    assert(APP_TIME_SYNC_INTERVAL_SEC >= APP_TIME_SYNC_RETRY_SEC);

    const uint32_t floor = APP_TIME_SYNC_CLOCK_FLOOR;
    struct tm tm = {0};
    time_t stamp = (time_t)floor;
    assert(gmtime_r(&stamp, &tm) != NULL && tm.tm_year + 1900 == 2024 && tm.tm_mon == 0 && tm.tm_mday == 1);
    puts("time sync policy: floor, interval, cooldown, backwards clock and attempt cap passed");
    return 0;
}
