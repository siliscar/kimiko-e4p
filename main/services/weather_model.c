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
static bool boolean(cJSON *root, const char *key, bool *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsBool(v)) return false;
    *out = cJSON_IsTrue(v); return true;
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
static bool point(cJSON *root, weather_location_t *v) {
    return cJSON_IsObject(root) && number(root, "lat", 0, 54, &v->lat) &&
        number(root, "lon", 73, 135, &v->lon) && string_field(root, "timezone", v->timezone, sizeof(v->timezone), false) &&
        string_field(root, "locationId", v->id, sizeof(v->id), false);
}
static bool location(cJSON *root, weather_location_t *v) {
    memset(v, 0, sizeof(*v));
    return point(root, v) && string_field(root, "name", v->name, sizeof(v->name), false) &&
        string_field(root, "admin1", v->admin1, sizeof(v->admin1), true) &&
        string_field(root, "admin2", v->admin2, sizeof(v->admin2), true) &&
        string_field(root, "country", v->country, sizeof(v->country), false) && weather_location_valid(v);
}
bool weather_location_parse(const char *json, size_t length, weather_location_t *out) {
    cJSON *root = document(json, length); weather_location_t v;
    bool ok = root && location(root, &v);
    if (ok) *out = v;
    cJSON_Delete(root); return ok;
}
bool weather_locations_parse(const char *json, size_t length, weather_location_t *out, unsigned page_size,
                             unsigned *count, unsigned *total, unsigned *page) {
    *count = *total = *page = 0; cJSON *root = document(json, length);
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "locations");
    int n = cJSON_GetArraySize(list);
    int size = 0, all = 0, current = 0;
    bool ok = root && out && page_size && page_size <= WEATHER_SEARCH_BATCH_SIZE &&
        cJSON_IsArray(list) && n <= (int)page_size &&
        integer(root, "pageSize", (int)page_size, (int)page_size, &size) &&
        integer(root, "total", 0, WEATHER_SEARCH_TOTAL_LIMIT, &all) &&
        integer(root, "page", 0, WEATHER_SEARCH_TOTAL_LIMIT - 1, &current) &&
        (all ? current * size < all && n == (all - current * size < size ? all - current * size : size) : !current && !n);
    for (int i = 0; ok && i < n; ++i) ok = location(cJSON_GetArrayItem(list, i), &out[i]);
    if (ok) { *count = (unsigned)n; *total = (unsigned)all; *page = (unsigned)current; }
    cJSON_Delete(root); return ok;
}
bool weather_catalog_parse(const char *json, size_t length, bool *enabled, bool *available) {
    *enabled = *available = false;
    cJSON *root = document(json, length); int schema;
    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "components");
    bool ok = root && integer(root, "schemaVersion", 1, 1, &schema) && cJSON_IsArray(list) && cJSON_GetArraySize(list) <= 16;
    bool found = false; cJSON *item;
    cJSON_ArrayForEach(item, list) {
        cJSON *code = cJSON_GetObjectItemCaseSensitive(item, "code");
        if (cJSON_IsString(code) && !strcmp(code->valuestring, "weather")) {
            if (found || !integer(item, "schemaVersion", 1, 1, &schema) ||
                !boolean(item, "enabled", enabled) || !boolean(item, "available", available)) ok = false;
            found = true;
        }
    }
    cJSON_Delete(root); return ok && found;
}
void weather_error_code(const char *json, size_t length, char *code, size_t capacity) {
    if (!capacity) return;
    code[0] = 0;
    cJSON *root = document(json, length);
    if (root) (void)string_field(root, "code", code, capacity, false);
    cJSON_Delete(root);
}
static bool timestamp_field(cJSON *root, const char *key, bool nullable, int64_t *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (nullable && cJSON_IsNull(v)) { *out = 0; return true; }
    if (!cJSON_IsString(v)) return false;
    *out = weather_timestamp(v->valuestring); return *out != 0;
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
bool weather_forecast_parse(const char *json, size_t length, int64_t now,
                           const weather_location_t *selected, weather_forecast_t *out) {
    cJSON *root = document(json, length); weather_forecast_t f = {0}; int schema; bool stale = false;
    cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "days");
    char source[16];
    bool ok = root && weather_location_valid(selected) && integer(root, "schemaVersion", 1, 1, &schema) &&
        string_field(root, "source", source, sizeof(source), false) && !strcmp(source, "QWeather") &&
        point(cJSON_GetObjectItemCaseSensitive(root, "location"), &f.location) &&
        !strcmp(f.location.id, selected->id) &&
        !strcmp(f.location.timezone, selected->timezone) &&
        fabs(f.location.lat - selected->lat) <= .011 && fabs(f.location.lon - selected->lon) <= .011 &&
        cJSON_IsArray(days) && cJSON_GetArraySize(days) == 2 &&
        timestamp_field(root, "fetchedAt", false, &f.fetched) && timestamp_field(root, "freshUntil", false, &f.fresh) &&
        timestamp_field(root, "serveUntil", false, &f.serve) && boolean(root, "stale", &stale);
    if (ok) { f.location = *selected; f.stale = stale; }
    for (int i = 0; ok && i < 2; ++i) {
        cJSON *item = cJSON_GetArrayItem(days, i); char date[11], code[4]; weather_day_t *d = &f.days[i];
        ok = string_field(item, "date", date, sizeof(date), false) && (d->date = parse_date(date)) >= 0 &&
            number(item, "minC", -90, 65, &d->min_c) && number(item, "maxC", -90, 65, &d->max_c) &&
            integer(item, "dayHumidityPct", 0, 100, &d->humidity) &&
            string_field(item, "conditionCode", code, sizeof(code), false) && strlen(code) == 3 &&
            (d->code = digits(code, 3)) >= 100 && string_field(item, "conditionText", d->condition, sizeof(d->condition), false) &&
            timestamp_field(item, "sunrise", true, &d->sunrise) && timestamp_field(item, "sunset", true, &d->sunset);
    }
    ok = ok && weather_forecast_valid(&f, now);
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
