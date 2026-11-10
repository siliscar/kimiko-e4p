/*
 * 中文：真实天气模型的边界、过期、跨日与坏响应回归（Open-Meteo 协议）。
 * English: Real weather model regressions for bounds, expiry, midnight and malformed
 * Open-Meteo responses.
 */
#include "weather_model.h"
#include "weather_test_data.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static weather_location_t city;
static int64_t now;

// 用一条城市搜索结果填充 city，供预报用例复用。
// Fill city from one geocoding hit so the forecast cases can reuse it.
static void pick_city(void) {
    weather_location_t one[1];
    unsigned count = 0;
    assert(weather_om_locations_parse(WEATHER_TEST_GEOCODING_ONE, strlen(WEATHER_TEST_GEOCODING_ONE),
                                      one, 1, &count) && count == 1);
    city = one[0];
}
static bool parse(cJSON *root) {
    char *json = cJSON_PrintUnformatted(root);
    weather_forecast_t f;
    bool ok = weather_om_forecast_parse(json, strlen(json), now, &city, &f);
    free(json);
    return ok;
}
static void http_dates(void) {
    const char *dates[]={
        "Thu, 01 Jan 2026 00:30:00 GMT", "Sun, 01 Feb 2026 00:30:00 GMT",
        "Sun, 01 Mar 2026 00:30:00 GMT", "Wed, 01 Apr 2026 00:30:00 GMT",
        "Fri, 01 May 2026 00:30:00 GMT", "Mon, 01 Jun 2026 00:30:00 GMT",
        "Wed, 01 Jul 2026 00:30:00 GMT", "Sat, 01 Aug 2026 00:30:00 GMT",
        "Tue, 01 Sep 2026 00:30:00 GMT", "Thu, 01 Oct 2026 00:30:00 GMT",
        "Sun, 01 Nov 2026 00:30:00 GMT", "Tue, 01 Dec 2026 00:30:00 GMT"
    };
    for(unsigned i=0;i<12;++i) {
        char iso[21];snprintf(iso,sizeof(iso),"2026-%02u-01T00:30:00Z",i+1);
        assert(weather_http_date(dates[i])==weather_timestamp(iso));
    }
    assert(weather_http_date("Tue, 29 Feb 2028 23:59:59 GMT")==weather_timestamp("2028-02-29T23:59:59Z"));
    assert(weather_http_date("Mon, 01 Jan 2024 00:00:00 GMT")==1704067200);
    assert(weather_http_date("Thu, 31 Dec 2099 23:59:59 GMT")==4102444799LL);
    const char *bad[]={NULL,"","Mon, 02 Nov 2026 00:30:00 GMT trailing",
        "Mon, 02 Nov 2026 00:30:00 GMT\r\n", "Mon, 02 Nov 2026 00:30:00 UTC",
        "Mon, 02 Nov 2026 00:30:00 +0800", "Tue, 02 Nov 2026 00:30:00 GMT",
        "mon, 02 Nov 2026 00:30:00 GMT", "Mon, 02 Foo 2026 00:30:00 GMT",
        "Mon, 02 Nov 2026 24:30:00 GMT", "Mon, 02 Nov 2026 00:60:00 GMT",
        "Mon, 02 Nov 2026 00:30:60 GMT", "Mon, 00 Nov 2026 00:30:00 GMT",
        "Mon, 31 Nov 2026 00:30:00 GMT", "Sun, 29 Feb 2026 00:30:00 GMT",
        "Mon, 02 Nov 2026 00:30:00 gmt", "Mon, 2 Nov 2026 00:30:00 GMT",
        "Monday, 02-Nov-26 00:30:00 GMT", "Mon Nov  2 00:30:00 2026",
        "2026-11-02T00:30:00Z", "Mon; 02 Nov 2026 00:30:00 GMT",
        "Mon, 02 Nov 20x6 00:30:00 GMT", "Mon, 02 Nov 2026 0x:30:00 GMT",
        "Sun, 31 Dec 2023 23:59:59 GMT", "Fri, 01 Jan 2100 00:00:00 GMT"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!weather_http_date(bad[i]));
    char truncated[30];const char *valid="Mon, 02 Nov 2026 00:30:00 GMT";
    for(size_t i=0;i<29;++i){memcpy(truncated,valid,i);truncated[i]=0;assert(!weather_http_date(truncated));}
}
// 城市搜索：只留中国大陆，时区归一到 +08，id 带 OM 前缀。
// Geocoding keeps mainland China only, normalises the timezone to +08 and prefixes the id.
static void geocoding(void) {
    weather_location_t results[WEATHER_SEARCH_BATCH_SIZE];
    unsigned count = 99;
    assert(weather_om_locations_parse(WEATHER_TEST_GEOCODING, strlen(WEATHER_TEST_GEOCODING),
                                      results, WEATHER_SEARCH_BATCH_SIZE, &count));
    assert(count == 2);
    assert(!strcmp(results[0].name, "杭州") && !strcmp(results[0].admin1, "浙江"));
    assert(!strcmp(results[0].id, "OM1808926"));
    assert(!strcmp(results[0].country, "中国"));
    assert(results[0].lat > 30.28 && results[0].lat < 30.30 && results[0].lon > 120.15 && results[0].lon < 120.17);
    assert(!strcmp(results[0].timezone, "Asia/Shanghai"));
    // 新疆在 tzdata 里是 Asia/Urumqi，但中国行政时间统一 +08，必须归一到 +08。
    // Xinjiang is Asia/Urumqi in tzdata, but China's civil time is +08 everywhere.
    assert(!strcmp(results[1].name, "乌鲁木齐") && !strcmp(results[1].timezone, "Asia/Shanghai"));
    assert(weather_location_valid(&results[0]) && weather_location_valid(&results[1]));
    // 容量限制只截断，不影响其余结果的合法性。
    // A smaller capacity only truncates; the kept entries stay valid.
    count = 99;
    assert(weather_om_locations_parse(WEATHER_TEST_GEOCODING, strlen(WEATHER_TEST_GEOCODING),
                                      results, 1, &count) && count == 1);
    // 无 results 键（未命中）是合法空结果，不是错误。
    // A missing results key (no hits) is a valid empty result, not an error.
    count = 99;
    const char *empty = "{\"generationtime_ms\":0.1}";
    assert(weather_om_locations_parse(empty, strlen(empty), results, 8, &count) && count == 0);
    const char *not_array = "{\"results\":{}}";
    assert(!weather_om_locations_parse(not_array, strlen(not_array), results, 8, &count) && count == 0);
    const char *trailing = "{} trailing";
    assert(!weather_om_locations_parse(trailing, strlen(trailing), results, 8, &count));
    assert(!weather_om_locations_parse(WEATHER_TEST_GEOCODING, strlen(WEATHER_TEST_GEOCODING), results, 0, &count));
    // 境外结果全部丢弃后仍是成功解析、零命中。
    // Dropping every overseas result is still a successful parse with zero hits.
    cJSON *root = cJSON_Parse(WEATHER_TEST_GEOCODING);
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "results");
    cJSON_DeleteItemFromArray(list, 0);
    cJSON_DeleteItemFromArray(list, 0);
    char *json = cJSON_PrintUnformatted(root);
    count = 99;
    assert(weather_om_locations_parse(json, strlen(json), results, 8, &count) && count == 0);
    free(json);
    cJSON_Delete(root);
}
int main(void) {
    http_dates();
    geocoding();
    now = weather_timestamp("2026-11-02T00:30:00Z"); assert(now);
    pick_city();
    weather_forecast_t f = {0};
    assert(weather_om_forecast_parse(WEATHER_TEST_FORECAST, strlen(WEATHER_TEST_FORECAST), now, &city, &f));
    // WMO 2 → 多云(101)，61 → 小雨(305)；湿度与温度直接来自 daily 数组。
    // WMO 2 → partly cloudy (101), 61 → light rain (305); humidity/temperature come from daily.
    assert(f.days[0].code == 101 && !strcmp(f.days[0].condition, "多云"));
    assert(f.days[1].code == 305 && !strcmp(f.days[1].condition, "小雨"));
    assert(f.days[0].min_c == 17.0 && f.days[0].max_c == 24.0);
    assert(weather_day_for(&f, now, false)->humidity == 68);
    assert(weather_day_for(&f, now, true)->min_c == 16.0);
    // 有效期在本地合成：1 小时新鲜、2 小时失效。
    // Validity is synthesised locally: fresh for one hour, expired after two.
    assert(f.fetched == now && f.fresh == now + 3600 && f.serve == now + 7200 && f.stale == 0);
    char text[193];
    weather_date_text(weather_local_day(now), text, sizeof(text));
    assert(!strcmp(text, "11月02日  星期一"));
    // 本地 06:15 日出折成 UTC 后，按 +08 显示仍是 06:15。
    // A 06:15 local sunrise converts to UTC and still renders as 06:15 in +08.
    weather_time_text(f.days[0].sunrise, text, sizeof(text)); assert(!strcmp(text, "06:15"));
    weather_time_text(f.days[0].sunset, text, sizeof(text)); assert(!strcmp(text, "17:12"));
    weather_time_text(0, text, sizeof(text)); assert(!strcmp(text, "--:--"));
    assert(weather_local_day(f.days[0].sunrise) == f.days[0].date);
    assert(weather_forecast_valid(&f, f.serve - 1) && !weather_forecast_valid(&f, f.serve));
    assert(weather_forecast_valid(&f, f.fetched - 120) && !weather_forecast_valid(&f, f.fetched - 121));
    assert(!weather_day_for(&f, 0, false) && !weather_forecast_valid(&f, now - 3600));
    assert(!weather_om_forecast_parse("{} trailing", 11, now, &city, &f));
    // 坏响应：缺数组、少一天、温度倒挂、日期与本地日不符、非数字湿度。
    // Malformed responses: missing arrays, one day only, inverted range, wrong local date,
    // non-numeric humidity.
    cJSON *root = cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    const char *drops[] = {"relative_humidity_2m_mean", "sunrise", "sunset", "weather_code"};
    for (unsigned i = 0; i < sizeof(drops) / sizeof(*drops); ++i) {
        cJSON *copy = cJSON_Duplicate(root, 1);
        cJSON_DeleteItemFromObject(cJSON_GetObjectItemCaseSensitive(copy, "daily"), drops[i]);
        assert(!parse(copy));
        cJSON_Delete(copy);
    }
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(daily, "time"), 1);
    assert(!parse(root));
    cJSON_Delete(root);
    root = cJSON_Parse(WEATHER_TEST_FORECAST);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_SetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min"), 0), 25.0);
    assert(!parse(root));
    cJSON_Delete(root);
    root = cJSON_Parse(WEATHER_TEST_FORECAST);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(daily, "time"), 0, cJSON_CreateString("2026-11-03"));
    assert(!parse(root));
    cJSON_Delete(root);
    root = cJSON_Parse(WEATHER_TEST_FORECAST);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(daily, "relative_humidity_2m_mean"), 0,
                             cJSON_CreateString("x"));
    assert(!parse(root));
    cJSON_Delete(root);
    // 湿度越界与时间格式错误同样拒绝。
    // Out-of-range humidity and a malformed time string are rejected too.
    root = cJSON_Parse(WEATHER_TEST_FORECAST);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_SetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(daily, "relative_humidity_2m_mean"), 0), 101.0);
    assert(!parse(root));
    cJSON_Delete(root);
    root = cJSON_Parse(WEATHER_TEST_FORECAST);
    daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON_ReplaceItemInArray(cJSON_GetObjectItemCaseSensitive(daily, "sunrise"), 0, cJSON_CreateString("2026-11-02 06:15"));
    assert(!parse(root));
    cJSON_Delete(root);
    // WMO 码表：每个码都要落进校验区间并有中文描述。
    // WMO table: every code must land inside the validated range with Chinese text.
    const int wmo_codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67,
                             71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99, 42, -1};
    for (unsigned i = 0; i < sizeof(wmo_codes) / sizeof(*wmo_codes); ++i) {
        int code = weather_om_condition_code(wmo_codes[i]);
        assert(code >= 100 && code <= 999);
        assert(weather_om_condition_text(wmo_codes[i])[0]);
        assert(weather_scene(code) != WEATHER_SCENE_NEUTRAL);
    }
    assert(weather_om_condition_code(0) == 100 && weather_om_condition_code(3) == 104);
    assert(weather_om_condition_code(65) == 307 && weather_om_condition_code(75) == 402);
    assert(weather_om_condition_code(95) == 302 && weather_om_condition_code(96) == 304);
    assert(weather_om_condition_code(45) == 501 && weather_scene(501) == WEATHER_SCENE_FOG);
    assert(weather_scene(999) == WEATHER_SCENE_NEUTRAL && weather_scene(100) == WEATHER_SCENE_SUN);
    assert(weather_scene(302) == WEATHER_SCENE_STORM && weather_scene(400) == WEATHER_SCENE_SNOW);
    const int scene_codes[][24]={
        {100,150},
        {300,301,305,306,307,308,309,310,311,312,313,314,315,316,317,318,350,351,399},
        {101,102,103,151,152,153},
        {400,401,402,403,407,408,409,410,456,457,499},
        {500,501,502,509,510,511,512,513,514,515},
        {302,303,304},
        {503,504,507,508},
        {404,405,406},
        {104}
    };
    unsigned seen_scenes=0;
    for(unsigned i=0;i<sizeof(scene_codes)/sizeof(*scene_codes);++i){
        weather_scene_t scene=weather_scene(scene_codes[i][0]);
        assert(scene>WEATHER_SCENE_NEUTRAL&&scene<WEATHER_SCENE_COUNT);
        assert(!(seen_scenes&(1u<<scene)));
        seen_scenes|=1u<<scene;
        for(unsigned j=0;j<24&&scene_codes[i][j];++j)assert(weather_scene(scene_codes[i][j])==scene);
    }
    assert((seen_scenes>>1)==((1u<<(WEATHER_SCENE_COUNT-1))-1));
    const int neutral_codes[]={0,99,105,149,154,200,299,505,506,516,899,900,901,999,1000};
    for(unsigned i=0;i<sizeof(neutral_codes)/sizeof(*neutral_codes);++i)
        assert(weather_scene(neutral_codes[i])==WEATHER_SCENE_NEUTRAL);
    assert(weather_url_encode("杭州 &?",text,sizeof(text))&&!strcmp(text,"%E6%9D%AD%E5%B7%9E%20%26%3F"));
    assert(!weather_url_encode("杭州",text,5)&&!text[0]);
    assert(weather_query_valid("杭州")&&weather_query_valid("hangzhou")&&!weather_query_valid("杭")&&!weather_query_valid("\xff\xff"));
    // 地点校验：id 形态、时区白名单与经纬度框。
    // Location validation: id shape, timezone allow-list and the lat/lon box.
    weather_location_t wrong=city;strcpy(wrong.id,"bad");assert(!weather_location_valid(&wrong));
    const char *invalid_ids[]={"", "abcD", "F4F1?x=1", "../F4F1", "123?", "F4 F1", "F4_F1", "F4F1%20"};
    for(unsigned i=0;i<sizeof(invalid_ids)/sizeof(*invalid_ids);++i) {
        wrong=city;snprintf(wrong.id,sizeof(wrong.id),"%s",invalid_ids[i]);
        assert(!weather_location_valid(&wrong));
    }
    wrong=city;memset(wrong.id,'A',sizeof(wrong.id));assert(!weather_location_valid(&wrong));
    wrong=city;strcpy(wrong.id,"CA3");assert(weather_location_valid(&wrong));
    strcpy(wrong.id,"0123456789ABCDEF");assert(weather_location_valid(&wrong));
    wrong=city;wrong.lat=9.55;assert(weather_location_valid(&wrong));
    wrong=city;wrong.lat=60;assert(!weather_location_valid(&wrong));
    wrong=city;wrong.lon=140;assert(!weather_location_valid(&wrong));
    wrong=city;strcpy(wrong.timezone,"Asia/Tokyo");assert(!weather_location_valid(&wrong));
    wrong=city;strcpy(wrong.timezone,"Asia/Urumqi");assert(!weather_location_valid(&wrong));
    f.stale=255;assert(!weather_forecast_valid(&f,now));
    city.timezone[0]=0;assert(!weather_location_valid(&city));
    // IP 归属地：取市级、剥后缀、ret 必须是 ok，缺字段或越界都拒绝。
    // IP location: prefer the city, trim the suffix, require ret=ok and reject malformed replies.
    char ip_city[65];
    const char *ip_ok[] = {
        "{\"ret\":\"ok\",\"data\":{\"ip\":\"1.2.3.4\",\"location\":[\"中国\",\"广东\",\"深圳\",\"\",\"电信\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"ip\":\"1.2.3.4\",\"location\":[\"中国\",\"广东省\",\"深圳市\",\"\",\"电信\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",\"北京\",\"北京\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",\"浙江\",\"\",\"\",\"电信\"]}}",
    };
    const char *ip_ok_want[] = {"深圳", "深圳", "北京", "浙江"};
    for (unsigned i = 0; i < sizeof(ip_ok) / sizeof(*ip_ok); ++i) {
        assert(weather_ip_city_parse(ip_ok[i], strlen(ip_ok[i]), ip_city, sizeof(ip_city)));
        assert(!strcmp(ip_city, ip_ok_want[i]));
    }
    // 直辖市两段重复，剥一次「市」即可；不重复截断成空串。
    const char *ip_muni = "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",\"上海市\",\"上海市\"]}}";
    assert(weather_ip_city_parse(ip_muni, strlen(ip_muni), ip_city, sizeof(ip_city)) && !strcmp(ip_city, "上海"));
    const char *ip_bad[] = {
        "{\"ret\":\"err\",\"data\":{\"location\":[\"中国\",\"广东\",\"深圳\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"ip\":\"1.2.3.4\"}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[]}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",\"\"]}}",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",3,null]}}",
        "{\"ret\":\"ok\",\"data\":\"x\"}",
        "not json",
        "{\"ret\":\"ok\",\"data\":{\"location\":[\"中国\",\"广东\",\"深圳\"]}} trailing",
    };
    for (unsigned i = 0; i < sizeof(ip_bad) / sizeof(*ip_bad); ++i) {
        assert(!weather_ip_city_parse(ip_bad[i], strlen(ip_bad[i]), ip_city, sizeof(ip_city)));
        assert(!ip_city[0]);
    }
    // 容量不足时拒绝，而不是截断出半个汉字让地理编码去猜。
    // Reject rather than truncating a UTF-8 name and letting the geocoder guess.
    assert(!weather_ip_city_parse(ip_ok[0], strlen(ip_ok[0]), ip_city, 4));
    assert(!weather_ip_city_parse(ip_ok[0], strlen(ip_ok[0]), NULL, 65));
    puts("PASS: Open-Meteo geocoding/forecast, IP location, WMO mapping, dates, solar UTC conversion, ranges, expiry");
}
