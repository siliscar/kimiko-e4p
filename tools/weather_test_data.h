/*
 * 中文：固定的代理测试样本，不是实况、定位或产品默认预报。
 * English: Fixed proxy test fixtures, never live observations, location evidence or product defaults.
 */
#pragma once
#define WEATHER_TEST_CITY "{\"locationId\":\"101210101\",\"name\":\"杭州\",\"admin1\":\"浙江省\",\"admin2\":\"杭州市\",\"country\":\"中国\",\"lat\":30.25,\"lon\":120.21,\"timezone\":\"Asia/Shanghai\"}"
#define WEATHER_TEST_ALPHA_CITY "{\"locationId\":\"F4F1\",\"name\":\"盐埔乡\",\"admin1\":\"台湾省\",\"admin2\":\"屏东县\",\"country\":\"中国台湾省\",\"lat\":22.74,\"lon\":120.57,\"timezone\":\"Asia/Taipei\"}"
#define WEATHER_TEST_CATALOG "{\"schemaVersion\":1,\"components\":[{\"code\":\"weather\",\"schemaVersion\":1,\"enabled\":true,\"available\":true}]}"
#define WEATHER_TEST_FORECAST \
    "{\"schemaVersion\":1,\"source\":\"QWeather\",\"stale\":false," \
    "\"location\":" WEATHER_TEST_CITY "," \
    "\"fetchedAt\":\"2026-11-02T00:20:00Z\",\"freshUntil\":\"2026-11-02T06:20:00Z\",\"serveUntil\":\"2026-11-02T06:20:00Z\"," \
    "\"days\":[{\"date\":\"2026-11-02\",\"minC\":17,\"maxC\":24,\"conditionCode\":\"100\",\"conditionText\":\"晴间多云\"," \
    "\"dayHumidityPct\":68,\"sunrise\":\"2026-11-01T22:15:00Z\",\"sunset\":\"2026-11-02T09:12:00Z\"}," \
    "{\"date\":\"2026-11-03\",\"minC\":16,\"maxC\":21,\"conditionCode\":\"305\",\"conditionText\":\"小雨\"," \
    "\"dayHumidityPct\":82,\"sunrise\":\"2026-11-02T22:16:00Z\",\"sunset\":\"2026-11-03T09:11:00Z\"}]}"
