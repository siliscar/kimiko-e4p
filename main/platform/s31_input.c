/*
 * 中文：隔离 S31 触摸兼容驱动类型，完整保留触点、原始字节及错误返回时的快照。
 * English: Isolate S31 compatibility-driver types, preserving contacts, raw bytes and snapshots on errors.
 */
#include "s31_input.h"
#include "cst836u.h"
#include <string.h>

_Static_assert(APP_TOUCH_MAX_POINTS == CST836U_MAX_POINTS, "touch point capacity");
_Static_assert(APP_TOUCH_RAW_LEN == CST836U_RAW_LEN, "touch raw capacity");

esp_err_t s31_input_read_touch(void *device, app_touch_t *touch) {
    if (!touch) return ESP_ERR_INVALID_ARG;
    // 驱动早退时可能不写载荷，先保留调用方快照；不通过类型强转共享结构体。
    // A driver early return may leave payloads untouched; retain caller state without struct aliasing.
    cst836u_touch_t sample = {
        .touched = touch->touched, .count = touch->count, .x = touch->x, .y = touch->y,
    };
    for (unsigned i = 0; i < APP_TOUCH_MAX_POINTS; ++i) {
        sample.points[i] = (cst836u_point_t){
            .active = touch->points[i].active, .id = touch->points[i].id,
            .event = touch->points[i].event, .x = touch->points[i].x, .y = touch->points[i].y,
        };
    }
    memcpy(sample.raw, touch->raw, sizeof(sample.raw));
    esp_err_t error = cst836u_read(device, &sample);
    touch->touched = sample.touched;
    touch->count = sample.count;
    touch->x = sample.x;
    touch->y = sample.y;
    for (unsigned i = 0; i < APP_TOUCH_MAX_POINTS; ++i) {
        touch->points[i] = (app_touch_point_t){
            .active = sample.points[i].active, .id = sample.points[i].id,
            .event = sample.points[i].event, .x = sample.points[i].x, .y = sample.points[i].y,
        };
    }
    memcpy(touch->raw, sample.raw, sizeof(touch->raw));
    return error;
}
