/*
 * 中文：页面与手势共享的触摸快照，不包含芯片句柄；读取入口同步借用调用方上下文。
 * English: Chip-independent touch snapshots for pages and gestures; reads synchronously borrow caller context.
 */
#ifndef MOGO_APP_TOUCH_H
#define MOGO_APP_TOUCH_H
#include <stdbool.h>
#include <stdint.h>

#define APP_TOUCH_MAX_POINTS 2
#define APP_TOUCH_RAW_LEN 15

typedef struct {
    bool active; ///< 当前触点有效 / Contact is active
    uint8_t id; ///< 触点编号 / Contact identifier
    uint8_t event; ///< 原始触点事件 / Raw contact event
    uint16_t x, y; ///< 触点坐标 / Contact coordinates
} app_touch_point_t;

typedef struct {
    bool touched; ///< 至少一个有效触点 / At least one active contact
    uint8_t count; ///< 有效触点数 / Active contact count
    uint16_t x, y; ///< 首个触点坐标 / Primary contact coordinates
    app_touch_point_t points[APP_TOUCH_MAX_POINTS]; ///< 完整触点载荷 / Complete contact payload
    uint8_t raw[APP_TOUCH_RAW_LEN]; ///< 诊断原始字节 / Raw diagnostic bytes
} app_touch_t;


#endif
