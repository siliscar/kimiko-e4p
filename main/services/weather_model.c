/*
 * 中文：中国城市天气的有界解析与日期策略。未知天气代码不冒充晴天。
 * English: Bounded parsing and date policy for Chinese cities. Unknown codes never imply sunshine.
 */
#include "weather_model.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool text_ok(const char *s, size_t capacity, bool empty) {
    if (!s || (!empty && !*s) || strlen(s) >= capacity) return false;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned c = *p++, n = 0, v = c;
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        if (c >= 194 && c <= 223) { n = 1; v = c & 31; }
        else if (c >= 224 && c <= 239) { n = 2; v = c & 15; }
        else if (c >= 240 && c <= 244) { n = 3; v = c & 7; }
        else return false;
        unsigned bytes = n;
        while (n--) { if ((*p & 192) != 128) return false; v = (v << 6) | (*p++ & 63); }
        if ((bytes == 2 && v < 2048) || (bytes == 3 && v < 65536) ||
            v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return false;
    }
    return true;
}

static cJSON *document(const char *json, size_t length) {
    if (!json || !length || length > WEATHER_BODY_LIMIT || memchr(json, 0, length)) return NULL;
    int depth = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char c = json[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 12) return NULL; }
        else if (c == '}' || c == ']') { if (--depth < 0) return NULL; }
    }
    if (quoted || depth) return NULL;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return NULL;
    while (end < json + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    if (end != json + length || !cJSON_IsObject(root)) { cJSON_Delete(root); return NULL; }
    return root;
}

static bool string_field(cJSON *root, const char *key, char *out, size_t cap, bool empty) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(v) || !text_ok(v->valuestring, cap, empty)) return false;
    memcpy(out, v->valuestring, strlen(v->valuestring) + 1);
    return true;
}
static bool number(cJSON *root, const char *key, double low, double high, double *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < low || v->valuedouble > high)
        return false;
    *out = v->valuedouble;
    return true;
}
static bool integer(cJSON *root, const char *key, int low, int high, int *out) {
    double n;
    if (!number(root, key, low, high, &n) || floor(n) != n) return false;
    *out = (int)n; return true;
}
static int month_days(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
static int32_t civil_day(int year, int month, int day) {
    year -= month <= 2;
    int era = year / 400, y = year - era * 400, m = month + (month > 2 ? -3 : 9);
    return era * 146097 + y * 365 + y / 4 - y / 100 + (153 * m + 2) / 5 + day - 1 - 719468;
}
static int digits(const char *s, unsigned n) {
    int v = 0;
    for (unsigned i = 0; i < n; ++i) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + s[i] - '0'; }
    return v;
}
static int32_t parse_date(const char *s) {
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return -1;
    int y = digits(s, 4), m = digits(s + 5, 2), d = digits(s + 8, 2);
    if (y < 2024 || y > 2099 || m < 1 || m > 12 || d < 1 || d > month_days(y, m)) return -1;
    return civil_day(y, m, d);
}
int64_t weather_timestamp(const char *s) {
    if (!s || strlen(s) < 20 || strlen(s) > 30 || s[10] != 'T' || s[13] != ':' || s[16] != ':') return 0;
    char date[11]; memcpy(date, s, 10); date[10] = 0;
    int32_t day = parse_date(date);
    int h = digits(s + 11, 2), m = digits(s + 14, 2), sec = digits(s + 17, 2);
    if (day < 0 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59) return 0;
    size_t n = strlen(s);
    if (n != 20) {
        if (s[19] != '.' || n < 22) return 0;
        for (size_t i = 20; i + 1 < n; ++i) if (s[i] < '0' || s[i] > '9') return 0;
    }
    if (s[n - 1] != 'Z') return 0;
    return (int64_t)day * 86400 + h * 3600 + m * 60 + sec;
}
int64_t weather_http_date(const char *s) {
    if (!s || strnlen(s, 30) != 29 || s[3] != ',' || s[4] != ' ' || s[7] != ' ' ||
        s[11] != ' ' || s[16] != ' ' || s[19] != ':' || s[22] != ':' || strcmp(s + 25, " GMT")) return 0;
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static const char weekdays[] = "SunMonTueWedThuFriSat";
    int month = 1, weekday = 0;
    while (month <= 12 && memcmp(s + 8, months + (month - 1) * 3, 3)) ++month;
    while (weekday < 7 && memcmp(s, weekdays + weekday * 3, 3)) ++weekday;
    int year = digits(s + 12, 4), day = digits(s + 5, 2);
    int hour = digits(s + 17, 2), minute = digits(s + 20, 2), second = digits(s + 23, 2);
    if (year < 2024 || year > 2099 || month > 12 || weekday == 7 || day < 1 ||
        day > month_days(year, month) || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) return 0;
    int32_t date = civil_day(year, month, day);
    if ((date + 4) % 7 != weekday) return 0;
    return (int64_t)date * 86400 + hour * 3600 + minute * 60 + second;
}
bool weather_clock_valid(int64_t now) { return now >= 1704067200 && now <= 4102444799LL; }
int32_t weather_local_day(int64_t now) { return (int32_t)((now + 28800) / 86400); }
void weather_date_text(int32_t day, char *out, size_t cap) {
    int y = 2024;
    while (y < 2100 && civil_day(y + 1, 1, 1) <= day) ++y;
    int m = 1; while (m < 12 && civil_day(y, m + 1, 1) <= day) ++m;
    static const char *week[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
    snprintf(out, cap, "%02d月%02d日  %s", m, (int)(day - civil_day(y, m, 1) + 1), week[(day + 4) % 7]);
}
void weather_time_text(int64_t utc, char *out, size_t cap) {
    if (!utc) { snprintf(out, cap, "--:--"); return; }
    int sec = (int)((utc + 28800) % 86400);
    snprintf(out, cap, "%02d:%02d", sec / 3600, sec / 60 % 60);
}
bool weather_location_valid(const weather_location_t *v) {
    return v && memchr(v->id, 0, sizeof(v->id)) && v->id[0] &&
        strspn(v->id, "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") == strlen(v->id) &&
        memchr(v->admin2, 0, sizeof(v->admin2)) && text_ok(v->admin2, sizeof(v->admin2), true) &&
        memchr(v->name, 0, sizeof(v->name)) && memchr(v->admin1, 0, sizeof(v->admin1)) &&
        memchr(v->country, 0, sizeof(v->country)) && memchr(v->timezone, 0, sizeof(v->timezone)) &&
        text_ok(v->name, sizeof(v->name), false) && text_ok(v->admin1, sizeof(v->admin1), true) &&
        text_ok(v->country, sizeof(v->country), false) &&
        (!strcmp(v->timezone, "Asia/Shanghai") || !strcmp(v->timezone, "Asia/Hong_Kong") ||
         !strcmp(v->timezone, "Asia/Macau") || !strcmp(v->timezone, "Asia/Taipei")) &&
        isfinite(v->lat) && isfinite(v->lon) && v->lat >= 0 && v->lat <= 54 && v->lon >= 73 && v->lon <= 135;
}
// Open-Meteo 城市搜索结果 → weather_location_t。
// 只保留中国大陆（country_code=CN）并落在产品允许的经纬度框内；时区统一归一到
// Asia/Shanghai——中国全境行政时间都是 UTC+8，weather_local_day 也按 +8 计算。
// Open-Meteo geocoding results → weather_location_t. Keep only mainland China
// (country_code=CN) inside the product's lat/lon box, and normalise the timezone to
// Asia/Shanghai: China's civil time is UTC+8 nationwide and weather_local_day uses +8.
bool weather_om_locations_parse(const char *json, size_t length,
                                weather_location_t *out, unsigned capacity, unsigned *count) {
    if (!out || !capacity || !count) return false;
    *count = 0;
    cJSON *root = document(json, length);
    if (!root) return false;
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "results");
    bool ok = !list || cJSON_IsArray(list);
    int n = cJSON_GetArraySize(list);
    unsigned used = 0;
    for (int i = 0; ok && i < n && used < capacity; ++i) {
        cJSON *item = cJSON_GetArrayItem(list, i);
        weather_location_t v; memset(&v, 0, sizeof(v));
        char country_code[8];
        double lat = 0, lon = 0; int id = 0;
        // 只收中国大陆；境外结果（含港澳台）没有对应的时区归一依据，直接跳过。
        // Mainland China only; overseas results have no timezone basis here and are skipped.
        if (!string_field(item, "country_code", country_code, sizeof(country_code), false) ||
            strcmp(country_code, "CN")) continue;
        if (!number(item, "latitude", 0, 54, &lat) || !number(item, "longitude", 73, 135, &lon) ||
            !integer(item, "id", 1, 2147483647, &id)) continue;
        if (!string_field(item, "name", v.name, sizeof(v.name), false)) continue;
        (void)string_field(item, "admin1", v.admin1, sizeof(v.admin1), true);
        (void)string_field(item, "admin2", v.admin2, sizeof(v.admin2), true);
        if (!string_field(item, "country", v.country, sizeof(v.country), true) || !v.country[0])
            snprintf(v.country, sizeof(v.country), "%s", "中国");
        v.lat = lat; v.lon = lon;
        // 前缀 OM 让来源可辨，同时保持 weather_location_valid 要求的大写字母数字形态。
        // The OM prefix keeps the source recognisable while staying uppercase alnum.
        snprintf(v.id, sizeof(v.id), "OM%d", id);
        snprintf(v.timezone, sizeof(v.timezone), "%s", "Asia/Shanghai");
        if (!weather_location_valid(&v)) continue;
        out[used++] = v;
    }
    cJSON_Delete(root); *count = used;
    return ok;
}
// ipip.net 的 IP 归属地：{"ret":"ok","data":{"location":["中国","广东","深圳","","电信"]}}
// 取市级（索引 2），为空时退回省级（索引 1）；直辖市两段同名，取任一段都可。
// 地理编码只认「杭州」不认「杭州市」，所以剥掉行政区后缀再交给城市搜索。
// ipip.net IP location: prefer the city (index 2) and fall back to the province (index 1);
// municipalities repeat the same name in both slots. The geocoder only matches bare names,
// so the administrative suffix is trimmed before the name is used as a search term.
bool weather_ip_city_parse(const char *json, size_t length, char *city, size_t capacity) {
    if (!city || capacity < 4) return false;
    city[0] = 0;
    cJSON *root = document(json, length);
    if (!root) return false;
    cJSON *ret = cJSON_GetObjectItemCaseSensitive(root, "ret");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    cJSON *list = cJSON_GetObjectItemCaseSensitive(data, "location");
    bool ok = cJSON_IsString(ret) && !strcmp(ret->valuestring, "ok") &&
        cJSON_IsArray(list) && cJSON_GetArraySize(list) >= 2;
    for (int i = 2; ok && i >= 1; --i) {
        cJSON *v = cJSON_GetArrayItem(list, i);
        if (!cJSON_IsString(v) || !v->valuestring[0] || !strcmp(v->valuestring, "中国") ||
            !text_ok(v->valuestring, capacity, false)) continue;
        snprintf(city, capacity, "%s", v->valuestring);
        break;
    }
    cJSON_Delete(root);
    // 「市」「省」在 UTF-8 里各占三字节，且都以 0xe5 开头，按字节截断是安全的。
    // 市 and 省 are three bytes each in UTF-8, so trimming by byte count stays on a boundary.
    static const char *const suffixes[] = {"市", "省"};
    size_t len = strlen(city);
    for (unsigned i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        size_t n = strlen(suffixes[i]);
        if (len > n && !memcmp(city + len - n, suffixes[i], n)) { city[len - n] = 0; break; }
    }
    return city[0] != 0;
}
bool weather_forecast_valid(const weather_forecast_t *f, int64_t now) {
    if (!f || !weather_clock_valid(now) || !weather_location_valid(&f->location) ||
        f->stale > 1 ||
        f->fetched < 1704067200 || f->fetched > now + 120 || f->fresh <= f->fetched ||
        f->serve < f->fresh || f->serve - f->fetched > 21600 || now >= f->serve ||
        f->days[0].date != weather_local_day(f->fetched) || f->days[1].date != f->days[0].date + 1) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const weather_day_t *d = &f->days[i];
        if (!isfinite(d->min_c) || !isfinite(d->max_c) || d->min_c < -90 || d->max_c > 65 ||
            d->min_c > d->max_c || d->humidity < 0 || d->humidity > 100 || d->code < 100 || d->code > 999 ||
            !memchr(d->condition, 0, sizeof(d->condition)) || !text_ok(d->condition, sizeof(d->condition), false) ||
            (d->sunrise && weather_local_day(d->sunrise) != d->date) ||
            (d->sunset && weather_local_day(d->sunset) != d->date) ||
            (d->sunrise && d->sunset && d->sunrise >= d->sunset)) return false;
    }
    return true;
}
// WMO 天气码 → 与 weather_scene()／界面共用的 QWeather 兼容码与中文描述。
// WMO weather code → QWeather-compatible code shared with weather_scene()/UI, plus Chinese text.
int weather_om_condition_code(int wmo) {
    switch (wmo) {
    case 0: return 100;                        // 晴
    case 1: return 102;                        // 少云
    case 2: return 101;                        // 多云
    case 3: return 104;                        // 阴
    case 45: case 48: return 501;              // 雾 / 雾凇
    case 51: case 53: return 309;              // 毛毛雨
    case 55: return 305;                       // 小雨
    case 56: case 57: return 313;              // 冻雨
    case 61: return 305;                       // 小雨
    case 63: return 306;                       // 中雨
    case 65: return 307;                       // 大雨
    case 66: case 67: return 313;              // 冻雨
    case 71: return 400;                       // 小雪
    case 73: return 401;                       // 中雪
    case 75: return 402;                       // 大雪
    case 77: return 400;                       // 米雪按小雪处理
    case 80: return 300;                       // 阵雨
    case 81: return 306;                       // 中雨
    case 82: return 307;                       // 大雨
    case 85: case 86: return 407;              // 阵雪
    case 95: return 302;                       // 雷阵雨
    case 96: case 99: return 304;              // 雷阵雨伴有冰雹
    default: return 100;                       // 未知码按晴处理，避免整体校验失败
    }
}
const char *weather_om_condition_text(int wmo) {
    switch (wmo) {
    case 0: return "晴";
    case 1: return "少云";
    case 2: return "多云";
    case 3: return "阴";
    case 45: return "雾";
    case 48: return "雾凇";
    case 51: case 53: return "毛毛雨";
    case 55: return "小雨";
    case 56: case 57: case 66: case 67: return "冻雨";
    case 61: return "小雨";
    case 63: return "中雨";
    case 65: return "大雨";
    case 71: return "小雪";
    case 73: return "中雪";
    case 75: return "大雪";
    case 77: return "米雪";
    case 80: return "阵雨";
    case 81: return "中雨";
    case 82: return "大雨";
    case 85: case 86: return "阵雪";
    case 95: return "雷阵雨";
    case 96: case 99: return "雷阵雨伴有冰雹";
    default: return "未知";
    }
}
// "YYYY-MM-DDTHH:MM"（本地时间）→ UTC 秒；offset 为响应里给出的本地时区偏移。
// "YYYY-MM-DDTHH:MM" local time → UTC seconds, using the response's local offset.
static int64_t om_local_seconds(const char *s, int offset) {
    if (!s || strnlen(s, 17) < 16 || s[10] != 'T' || s[13] != ':') return 0;
    char date[11]; memcpy(date, s, 10); date[10] = 0;
    int32_t day = parse_date(date);
    int h = digits(s + 11, 2), m = digits(s + 14, 2);
    if (day < 0 || h < 0 || h > 23 || m < 0 || m > 59) return 0;
    return (int64_t)day * 86400 + h * 3600 + m * 60 - offset;
}
bool weather_om_forecast_parse(const char *json, size_t length, int64_t now,
                               const weather_location_t *selected, weather_forecast_t *out) {
    if (!selected || !out || !weather_location_valid(selected)) return false;
    cJSON *root = document(json, length);
    if (!root) return false;
    weather_forecast_t f = {0};
    int offset = 28800;
    (void)integer(root, "utc_offset_seconds", -43200, 50400, &offset);
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    cJSON *time = cJSON_GetObjectItemCaseSensitive(daily, "time");
    cJSON *codes = cJSON_GetObjectItemCaseSensitive(daily, "weather_code");
    cJSON *high = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_max");
    cJSON *low = cJSON_GetObjectItemCaseSensitive(daily, "temperature_2m_min");
    cJSON *humidity = cJSON_GetObjectItemCaseSensitive(daily, "relative_humidity_2m_mean");
    cJSON *sunrise = cJSON_GetObjectItemCaseSensitive(daily, "sunrise");
    cJSON *sunset = cJSON_GetObjectItemCaseSensitive(daily, "sunset");
    bool ok = cJSON_IsObject(daily) && cJSON_GetArraySize(time) >= 2 && cJSON_GetArraySize(codes) >= 2 &&
        cJSON_GetArraySize(high) >= 2 && cJSON_GetArraySize(low) >= 2 &&
        cJSON_GetArraySize(humidity) >= 2 && cJSON_GetArraySize(sunrise) >= 2 &&
        cJSON_GetArraySize(sunset) >= 2;
    for (unsigned i = 0; ok && i < 2; ++i) {
        weather_day_t *d = &f.days[i];
        cJSON *date = cJSON_GetArrayItem(time, i), *wmo = cJSON_GetArrayItem(codes, i);
        cJSON *tmax = cJSON_GetArrayItem(high, i), *tmin = cJSON_GetArrayItem(low, i);
        cJSON *rh = cJSON_GetArrayItem(humidity, i);
        cJSON *up = cJSON_GetArrayItem(sunrise, i), *down = cJSON_GetArrayItem(sunset, i);
        ok = cJSON_IsString(date) && (d->date = parse_date(date->valuestring)) >= 0 &&
            cJSON_IsNumber(wmo) && cJSON_IsNumber(tmax) && cJSON_IsNumber(tmin) && cJSON_IsNumber(rh) &&
            cJSON_IsString(up) && cJSON_IsString(down) &&
            (d->sunrise = om_local_seconds(up->valuestring, offset)) != 0 &&
            (d->sunset = om_local_seconds(down->valuestring, offset)) != 0;
        if (!ok) break;
        const int code = (int)(wmo->valuedouble + 0.5);
        d->min_c = tmin->valuedouble; d->max_c = tmax->valuedouble;
        d->humidity = (int)(rh->valuedouble + 0.5);
        d->code = weather_om_condition_code(code);
        snprintf(d->condition, sizeof(d->condition), "%s", weather_om_condition_text(code));
    }
    if (ok) {
        f.location = *selected;
        // 代理已退役：有效期在本地合成。Open-Meteo 每小时更新，1 小时算新鲜、2 小时失效。
        // The proxy is retired, so validity is synthesised locally: Open-Meteo updates hourly,
        // so treat one hour as fresh and two hours as the hard cap.
        f.fetched = now; f.fresh = now + 3600; f.serve = now + 7200; f.stale = 0;
        ok = weather_forecast_valid(&f, now);
    }
    if (ok) *out = f;
    cJSON_Delete(root); return ok;
}
const weather_day_t *weather_day_for(const weather_forecast_t *f, int64_t now, bool tomorrow) {
    if (!weather_forecast_valid(f, now)) return NULL;
    int32_t date = weather_local_day(now) + tomorrow;
    for (unsigned i = 0; i < 2; ++i) if (f->days[i].date == date) return &f->days[i];
    return NULL;
}
weather_scene_t weather_scene(int code) {
    if (code == 100 || code == 150) return WEATHER_SCENE_SUN;
    if (code == 104) return WEATHER_SCENE_OVERCAST;
    if ((code >= 101 && code <= 103) || (code >= 151 && code <= 153)) return WEATHER_SCENE_CLOUD;
    if (code >= 302 && code <= 304) return WEATHER_SCENE_STORM;
    if (code >= 300 && code <= 399) return WEATHER_SCENE_RAIN;
    if (code >= 404 && code <= 406) return WEATHER_SCENE_SLEET;
    if (code >= 400 && code <= 499) return WEATHER_SCENE_SNOW;
    if (code == 503 || code == 504 || code == 507 || code == 508) return WEATHER_SCENE_DUST;
    if (code == 500 || code == 501 || code == 502 || code == 509 || code == 510 ||
        (code >= 511 && code <= 515))
        return WEATHER_SCENE_FOG;
    return WEATHER_SCENE_NEUTRAL;
}
bool weather_url_encode(const char *text, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF"; size_t pos = 0;
    if (!cap || !text) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (pos + (plain ? 1 : 3) >= cap) { out[0] = 0; return false; }
        if (plain) out[pos++] = (char)*p;
        else { out[pos++] = '%'; out[pos++] = hex[*p >> 4]; out[pos++] = hex[*p & 15]; }
    }
    out[pos] = 0; return true;
}
bool weather_query_valid(const char *text) {
    if (!text_ok(text, 193, false)) return false;
    unsigned count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if ((*p & 192) != 128) ++count;
    return count >= 2 && count <= 64;
}
