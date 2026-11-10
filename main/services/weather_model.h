/*
 * 中文：天气代理协议与纯数据策略，不接触网络、设置或显示。
 * English: Weather proxy protocol and pure data policy without networking, settings or display.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEATHER_CITY_LIMIT 8
#define WEATHER_SEARCH_BATCH_SIZE 20
#define WEATHER_SEARCH_TOTAL_LIMIT 4000
#define WEATHER_BODY_LIMIT 8192
typedef struct {
    char id[17], admin2[97]; ///< 官方地点标识与地级行政区 / Official location ID and prefecture
    char name[97], admin1[97], country[49], timezone[33]; ///< 展示信息 / Display metadata
    double lat, lon; ///< 代理归一化坐标 / Proxy-normalized coordinates
} weather_location_t;
typedef struct {
    int32_t date; ///< 公历UTC+8日序 / Civil UTC+8 day number
    double min_c, max_c; ///< 当日最低最高 / Daily minimum and maximum
    int humidity, code; ///< 白天湿度与供应商代码 / Daytime humidity and provider code
    char condition[97]; ///< 天气描述 / Condition description
    int64_t sunrise, sunset; ///< UTC秒；零为缺失 / UTC seconds, zero when absent
} weather_day_t;
typedef struct {
    weather_location_t location; ///< 坐标与时区 / Coordinates and timezone
    weather_day_t days[2]; ///< 原始日期，不可重标 / Original dates, never relabeled
    int64_t fetched, fresh, serve; ///< 代理原始有效期 / Original proxy validity times
    uint8_t stale; ///< 代理已降级，保存格式仅允许0/1 / Proxy stale flag, persisted as 0/1 only
} weather_forecast_t;
typedef enum {
    WEATHER_SCENE_NEUTRAL, WEATHER_SCENE_SUN, WEATHER_SCENE_RAIN,
    WEATHER_SCENE_CLOUD, WEATHER_SCENE_SNOW, WEATHER_SCENE_FOG, WEATHER_SCENE_STORM,
    WEATHER_SCENE_DUST, WEATHER_SCENE_SLEET, WEATHER_SCENE_OVERCAST,
    WEATHER_SCENE_COUNT
} weather_scene_t;

bool weather_location_valid(const weather_location_t *location);
/// Open-Meteo 城市搜索结果解析；只保留中国大陆并归一化时区。
/// Parse Open-Meteo geocoding results; keep mainland China only and normalise the timezone.
bool weather_om_locations_parse(const char *json, size_t length,
                                weather_location_t *locations, unsigned capacity, unsigned *count);
/// IP 归属地响应取城市名，已剥掉「市」后缀供地理编码检索；失败时为空。
/// Extract the city from an IP-location response, stripped of the trailing 市 for geocoding.
bool weather_ip_city_parse(const char *json, size_t length, char *city, size_t capacity);
/// Open-Meteo 两日预报解析；有效期在本地合成，不依赖上游代理。
/// Parse the Open-Meteo two-day forecast; validity timestamps are synthesised locally.
bool weather_om_forecast_parse(const char *json, size_t length, int64_t now,
                               const weather_location_t *location, weather_forecast_t *forecast);
/// WMO 天气码 → QWeather 兼容码与中文描述。/ WMO code to QWeather-compatible code and Chinese text.
int weather_om_condition_code(int wmo);
const char *weather_om_condition_text(int wmo);
bool weather_forecast_valid(const weather_forecast_t *forecast, int64_t now);
const weather_day_t *weather_day_for(const weather_forecast_t *forecast, int64_t now, bool tomorrow);
bool weather_clock_valid(int64_t now);
int32_t weather_local_day(int64_t now);
int64_t weather_timestamp(const char *text);
/// 严格解析HTTP的GMT日期，失败返回零。/ Parse an HTTP GMT date strictly; return zero on failure.
int64_t weather_http_date(const char *text);
void weather_date_text(int32_t day, char *text, size_t capacity);
void weather_time_text(int64_t utc, char *text, size_t capacity);
weather_scene_t weather_scene(int code);
bool weather_url_encode(const char *text, char *output, size_t capacity);
bool weather_query_valid(const char *text);
