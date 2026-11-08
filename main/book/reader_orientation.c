/*
 * 中文：主轴至少 650mg、领先 200mg 并连续稳定 500ms 才换向，平放保持当前方向。
 * English: Change orientation only after a dominant axis exceeds 650mg by 200mg for 500ms; flat placement holds orientation.
 */
#include "reader_orientation.h"
#include <stdlib.h>

void reader_orientation_cancel(reader_orientation_t *state) {
    state->candidate = 0;
    state->since_ms = state->last_ms = 0;
}
bool reader_orientation_feed(reader_orientation_t *s, int x, int y, int z,
                             bool touched, int64_t now_ms, display_orientation_t *next) {
    const int ax = abs(x), ay = abs(y), az = abs(z);
    const int64_t magnitude = (int64_t)x*x + (int64_t)y*y + (int64_t)z*z;
    if (!s->automatic || touched || ax > 1300 || ay > 1300 || az > 800 ||
        magnitude < 700*700 || magnitude > 1300*1300 ||
        (ax > ay ? ax < 650 || ax - ay < 200 : ay < 650 || ay - ax < 200)) {
        reader_orientation_cancel(s);
        return false;
    }
    // 静态加速度与重力反向；board 设备 X 与竖屏右向反向，Y 沿长边。
    // Static acceleration opposes gravity; board device X opposes portrait right, while Y follows the long edge.
    display_orientation_t candidate = ax > ay
        ? (x < 0 ? DISPLAY_ORIENTATION_RIGHT : DISPLAY_ORIENTATION_LEFT)
        : (y < 0 ? DISPLAY_ORIENTATION_PORTRAIT : DISPLAY_ORIENTATION_INVERTED);
    if (candidate == s->locked) {
        reader_orientation_cancel(s);
        return false;
    }
    if (s->candidate != (int)candidate + 1 || now_ms < s->last_ms ||
        now_ms - s->last_ms > 200 || abs(x-s->x) > 160 ||
        abs(y-s->y) > 160 || abs(z-s->z) > 160) {
        s->candidate = (int)candidate + 1;
        s->since_ms = now_ms;
    }
    s->x = x; s->y = y; s->z = z; s->last_ms = now_ms;
    if (now_ms - s->since_ms < 500) return false;
    *next = candidate;
    reader_orientation_cancel(s);
    return true;
}
