/*
 * 中文：首页双区域组件的NVS偏好，与设置恢复共用事务；不修改天气城市或缓存。
 * English: NVS preferences for two Home regions, sharing settings restore transactions without changing weather data.
 */
#include "home_components.h"
#include "epdiy.h"
#include "nvs.h"
static bool s_loaded;
static app_home_widget_t s_slots[APP_HOME_SLOT_COUNT] = {APP_HOME_WIDGET_READING, APP_HOME_WIDGET_EMPTY};

bool app_home_components_value_valid(uint8_t value) {
    unsigned top = value & 3u, bottom = (value >> 2) & 3u;
    return (value & 0xf0u) == 0x10u && top <= APP_HOME_WIDGET_JOURNAL && bottom <= APP_HOME_WIDGET_JOURNAL &&
        (top != bottom || top == APP_HOME_WIDGET_EMPTY);
}

uint8_t app_home_components_value(void) {
    return (uint8_t)(0x10u | (unsigned)s_slots[0] | ((unsigned)s_slots[1] << 2));
}

void app_home_components_apply(uint8_t value) {
    if (!app_home_components_value_valid(value)) return;
    s_slots[0] = (app_home_widget_t)(value & 3u);
    s_slots[1] = (app_home_widget_t)((value >> 2) & 3u);
    s_loaded = true;
}

void app_home_components_init(bool legacy_weather_enabled) {
    if (s_loaded) return;
    s_loaded = true;
    s_slots[APP_HOME_SLOT_TOP] = APP_HOME_WIDGET_READING;
    s_slots[APP_HOME_SLOT_BOTTOM] = legacy_weather_enabled ? APP_HOME_WIDGET_WEATHER : APP_HOME_WIDGET_EMPTY;
    nvs_handle_t handle;
    if (nvs_open("read_pico", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t value = 0;
    if (nvs_get_u8(handle, "home_regions", &value) == ESP_OK) app_home_components_apply(value);
    nvs_close(handle);
}

app_home_widget_t app_home_component_at(unsigned slot) {
    return slot < APP_HOME_SLOT_COUNT ? s_slots[slot] : APP_HOME_WIDGET_EMPTY;
}

bool app_home_component_set(unsigned slot, app_home_widget_t widget) {
    if (slot >= APP_HOME_SLOT_COUNT || (unsigned)widget > APP_HOME_WIDGET_JOURNAL) return false;
    app_home_components_init(false);
    if (s_slots[slot] == widget) return true;
    app_home_widget_t next[APP_HOME_SLOT_COUNT] = {s_slots[0], s_slots[1]};
    unsigned other = slot == APP_HOME_SLOT_TOP ? APP_HOME_SLOT_BOTTOM : APP_HOME_SLOT_TOP;
    if (widget != APP_HOME_WIDGET_EMPTY && next[other] == widget) next[other] = next[slot];
    next[slot] = widget;
    uint8_t value = (uint8_t)(0x10u | (unsigned)next[0] | ((unsigned)next[1] << 2));
    if (!epd_flash_begin()) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("read_pico", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "home_regions", value);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    epd_flash_end();
    if (err != ESP_OK) return false;
    app_home_components_apply(value);
    return true;
}

bool app_home_has_widget(app_home_widget_t widget) {
    for (unsigned i = 0; i < APP_HOME_SLOT_COUNT; ++i) if (s_slots[i] == widget) return true;
    return false;
}
