/*
 * 中文：首页上下区域的独立组件偏好，不改变旧设置备份布局。
 * English: Independent Home-region preferences without changing legacy settings backups.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_HOME_WIDGET_READING,
    APP_HOME_WIDGET_WEATHER,
    APP_HOME_WIDGET_EMPTY,
    APP_HOME_WIDGET_JOURNAL
} app_home_widget_t;
enum { APP_HOME_SLOT_TOP, APP_HOME_SLOT_BOTTOM, APP_HOME_SLOT_COUNT };

/// 首次无新偏好时沿用旧天气显隐，仅显式修改时保存。/ Migrate legacy weather visibility in memory; persist only explicit changes.
void app_home_components_init(bool legacy_weather_enabled);
/// 无效槽位返回留白。/ Invalid slots return Empty.
app_home_widget_t app_home_component_at(unsigned slot);
/// 实际组件唯一，重复选择自动对调；失败不改变两槽。/ Real widgets are unique and swap on selection; failures preserve both slots.
bool app_home_component_set(unsigned slot, app_home_widget_t widget);
/// 判断任一槽位是否使用组件。/ Check whether either slot uses a widget.
bool app_home_has_widget(app_home_widget_t widget);
/// 带版本的完整两槽值，用于同事务设置备份恢复。/ Versioned two-slot value for same-transaction settings backup/restore.
uint8_t app_home_components_value(void);
bool app_home_components_value_valid(uint8_t value);
/// 设置恢复提交成功后更新内存，不另写NVS；忽略非法值。/ Apply after settings restore commits, without another NVS write; ignore invalid values.
void app_home_components_apply(uint8_t value);
