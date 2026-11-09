/*
 * 中文：固定的 Open-Meteo 测试样本，不是实况、定位或产品默认预报。
 * English: Fixed Open-Meteo test fixtures, never live observations, location evidence or product defaults.
 */
#pragma once

// 城市搜索样本：两条中国大陆结果（含新疆，时区需归一到 +08）与一条境外结果（应被丢弃）。
// Geocoding fixture: two mainland results (incl. Xinjiang normalised to +08) and one overseas
// result that must be dropped.
#define WEATHER_TEST_GEOCODING \
    "{\"results\":[" \
    "{\"id\":1808926,\"name\":\"杭州\",\"latitude\":30.29365,\"longitude\":120.16142," \
    "\"country_code\":\"CN\",\"timezone\":\"Asia/Shanghai\",\"country\":\"中国\"," \
    "\"admin1\":\"浙江\",\"admin2\":\"杭州市\"}," \
    "{\"id\":1529102,\"name\":\"乌鲁木齐\",\"latitude\":43.80096,\"longitude\":87.60046," \
    "\"country_code\":\"CN\",\"timezone\":\"Asia/Urumqi\",\"country\":\"中国\",\"admin1\":\"新疆\"}," \
    "{\"id\":1850147,\"name\":\"東京\",\"latitude\":35.6895,\"longitude\":139.69171," \
    "\"country_code\":\"JP\",\"timezone\":\"Asia/Tokyo\",\"country\":\"日本\",\"admin1\":\"東京都\"}]}"

// 只有一条命中的搜索响应，供服务层搜索用例使用。
// Single-hit geocoding response for the service-level search cases.
#define WEATHER_TEST_GEOCODING_ONE \
    "{\"results\":[{\"id\":1808926,\"name\":\"杭州\",\"latitude\":30.29365,\"longitude\":120.16142," \
    "\"country_code\":\"CN\",\"timezone\":\"Asia/Shanghai\",\"country\":\"中国\",\"admin1\":\"浙江\"}]}"

// 两日预报样本：2 → 多云(101)，61 → 小雨(305)；日出日落为本地时间，需按 +08 折成 UTC。
// Two-day forecast: 2 → partly cloudy (101), 61 → light rain (305); sunrise/sunset are local
// wall-clock strings that must be converted to UTC with the +08 offset.
#define WEATHER_TEST_FORECAST \
    "{\"latitude\":30.263618,\"longitude\":120.14051,\"utc_offset_seconds\":28800," \
    "\"timezone\":\"Asia/Shanghai\",\"daily\":{" \
    "\"time\":[\"2026-11-02\",\"2026-11-03\"]," \
    "\"weather_code\":[2,61]," \
    "\"temperature_2m_max\":[24.0,21.0]," \
    "\"temperature_2m_min\":[17.0,16.0]," \
    "\"relative_humidity_2m_mean\":[68,82]," \
    "\"sunrise\":[\"2026-11-02T06:15\",\"2026-11-03T06:16\"]," \
    "\"sunset\":[\"2026-11-02T17:12\",\"2026-11-03T17:11\"]}}"
