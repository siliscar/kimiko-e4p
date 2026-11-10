/*
 * 中文：真实天气服务配确定性 HTTP/任务/NVS，覆盖联网所有权、取消、缓存与 Open-Meteo 协议。
 * English: Real weather service with deterministic HTTP/tasks/NVS, covering radio ownership,
 * cancellation, cache handoff and the Open-Meteo protocol.
 */
#include "weather_test_data.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
static time_t wall_time;
static time_t weather_test_time(time_t *out) {if(out)*out=wall_time;return wall_time;}
#define time weather_test_time
// 两个 Open-Meteo 源与定位源都由测试注入，只验证 URL 拼接、可信源校验与响应处理，不联网。
// Both Open-Meteo origins and the locate origin are injected so the test only exercises URL
// assembly, trusted-origin checks and response handling; nothing reaches the network.
#define CONFIG_WEATHER_GEOCODING_ORIGIN "https://geo.test"
#define CONFIG_WEATHER_FORECAST_ORIGIN "https://fc.test"
#define CONFIG_WEATHER_IP_ORIGIN "https://ip.test"
#include "../main/services/weather_service.c"
#undef time
static void (*pending)(void *);
static bool done,worker,flash,saved=true,fail_flash,fail_task,fail_alloc,cancel_read,cancel_connect,ota_busy,ble_connected;
static unsigned starts,stops,requests,forecast_calls,search_calls,suffix_calls,ip_calls,ns_writes,time_cancels;
static int network_state;
static int64_t monotonic;
static unsigned body_reads,heap_allocs;
static int forecast_status=200,geocoding_status=200,ip_status=200;
static const char *forecast_body=WEATHER_TEST_FORECAST,*geocoding_body=WEATHER_TEST_GEOCODING;
static const char *suffix_body="{\"results\":[]}";
static const char *ip_body=WEATHER_TEST_IP_LOCATION;
static const char *date_header="Mon, 02 Nov 2026 00:30:00 GMT";
static bool stall_body,fail_body,incomplete_body,cancel_forecast_read;
static char geocoding_payload[WEATHER_BODY_LIMIT+1];
static size_t prefs_size,cache_size;
static pmu_snapshot_t battery={80};
static unsigned char prefs_bytes[1024],cache_bytes[2048];
struct FakeHttp {
    const char *body,*date;size_t size,at;int status;bool forecast,headers_sent;
    int (*event_handler)(esp_http_client_event_t *);
    void *user_data;
};
static void run_worker(void) {assert(pending);void(*f)(void*)=pending;pending=NULL;worker=true;f(NULL);worker=false;}
void *heap_caps_malloc(size_t n,int c){
    (void)c;if(fail_alloc)return NULL;
    void *p=malloc(n);if(p)++heap_allocs;return p;
}
void *heap_caps_calloc(size_t n,size_t s,int c){(void)c;if(fail_alloc)return NULL;void *p=calloc(n,s);if(p)++heap_allocs;return p;}
void heap_caps_free(void *p){if(p){assert(heap_allocs);--heap_allocs;}free(p);}
SemaphoreHandle_t xSemaphoreCreateBinary(void){return (void*)1;}
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t wait){
    assert(s==(void*)1);if(wait&&pending)run_worker();
    if(!done)return 0;done=false;return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t s){assert(s==(void*)1&&!done);done=true;return pdTRUE;}
int xTaskCreate(void(*f)(void*),const char*n,unsigned stack,void*a,unsigned p,void*t){
    (void)n;(void)a;(void)p;(void)t;assert(stack==8192&&!pending);if(fail_task)return 0;pending=f;return pdPASS;
}
void vTaskDelete(void *p){(void)p;}
void vTaskDelay(unsigned ms){monotonic+=(int64_t)ms*1000;if(cancel_connect)atomic_store(&s_cancel,true);}
int64_t esp_timer_get_time(void){return monotonic;}
bool epd_flash_begin(void){assert(!worker&&!flash);if(fail_flash)return false;flash=true;return true;}
void epd_flash_end(void){assert(flash);flash=false;}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){
    assert(!strcmp(ns,"kiiko_weather"));if(mode==NVS_READWRITE)assert(flash&&!worker);*h=1;return ESP_OK;
}
void nvs_close(nvs_handle_t h){assert(h==1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
    assert(h==1);size_t n=!strcmp(key,"prefs")?prefs_size:cache_size;
    const void *p=!strcmp(key,"prefs")?prefs_bytes:cache_bytes;
    if(!n||*size<n)return ESP_ERR_NVS_NOT_FOUND;memcpy(out,p,n);*size=n;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *v,size_t n){
    assert(h==1&&flash&&!worker);++ns_writes;
    if(!strcmp(key,"prefs")){assert(n<=sizeof(prefs_bytes));memcpy(prefs_bytes,v,n);prefs_size=n;}
    else{assert(!strcmp(key,"cache")&&n<=sizeof(cache_bytes));memcpy(cache_bytes,v,n);cache_size=n;}return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){assert(h==1&&flash&&!worker&&!strcmp(key,"cache"));cache_size=0;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1&&flash&&!worker);return ESP_OK;}
bool pico_online_busy(void){return ota_busy;}
bool ble_pt_connected(void){return ble_connected;}
void app_time_service_cancel(void){++time_cancels;}
const pmu_snapshot_t *device_power_get(void){return &battery;}
int pmu_battery_percent(const pmu_snapshot_t *p){return p->battery;}
bool pmu_battery_charging(const pmu_snapshot_t *p){(void)p;return false;}
void wifi_transfer_get_status(wifi_transfer_status_t *s){
    *s=(wifi_transfer_status_t){.state=network_state,.mode=WIFI_TRANSFER_MODE_STA,
        .network_ready=network_state==WIFI_TRANSFER_READY};
}
esp_err_t wifi_transfer_get_saved_wifi(char *ssid,bool *configured){strcpy(ssid,"fixture");*configured=saved;return ESP_OK;}
esp_err_t wifi_transfer_start(const wifi_transfer_cfg_t *cfg){
    assert(worker&&network_state==WIFI_TRANSFER_STOPPED&&cfg->network_only);++starts;
    network_state=cancel_connect?1:WIFI_TRANSFER_READY;return ESP_OK;
}
void wifi_transfer_stop(void){assert(worker);++stops;network_state=WIFI_TRANSFER_STOPPED;}
int esp_crt_bundle_attach(void *p){(void)p;return 0;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){
    assert(worker&&c->disable_auto_redirect&&c->crt_bundle_attach&&c->timeout_ms==6000&&c->is_async);
    struct FakeHttp *h=calloc(1,sizeof(*h));h->status=200;++requests;
    h->event_handler=c->event_handler;h->user_data=c->user_data;
    if(strstr(c->url,"/v1/forecast?")) {
        // 预报请求必须带坐标与中国时区，不携带任何城市 ID。
        // A forecast request carries coordinates and the China timezone, never a city id.
        assert(strstr(c->url,"latitude=")&&strstr(c->url,"longitude=")&&
               strstr(c->url,"timezone=Asia%2FShanghai")&&strstr(c->url,"forecast_days=2")&&
               !strstr(c->url,"locationId="));
        assert(!strncmp(c->url,CONFIG_WEATHER_FORECAST_ORIGIN,strlen(CONFIG_WEATHER_FORECAST_ORIGIN)));
        ++forecast_calls;h->body=forecast_body;h->status=forecast_status;h->forecast=true;h->date=date_header;
    } else if(!strncmp(c->url,CONFIG_WEATHER_IP_ORIGIN,strlen(CONFIG_WEATHER_IP_ORIGIN))) {
        // 定位请求只问「这个出口 IP 属于哪个城市」，不带任何设备标识。
        // The locate request only asks which city the egress IP belongs to, with no device id.
        assert(strstr(c->url,"/json"));
        ++ip_calls;h->body=ip_body;h->status=ip_status;
    } else {
        // 城市搜索请求使用地理编码源，并带上中文语言与批量大小；定位只取少量候选。
        // City search hits the geocoding origin with Chinese language and the batch size; the
        // locate step only needs a few candidates.
        assert(strstr(c->url,"/v1/search?name=")&&strstr(c->url,"language=zh")&&
               (strstr(c->url,"count=20")||strstr(c->url,"count=5"))&&!strstr(c->url,"&page="));
        assert(!strncmp(c->url,CONFIG_WEATHER_GEOCODING_ORIGIN,strlen(CONFIG_WEATHER_GEOCODING_ORIGIN)));
        // 补「市」的那次重试走独立响应，便于区分首查与重查。
        // The 市 retry has its own reply so the first and second lookups stay distinguishable.
        if(strstr(c->url,"%E5%B8%82")){++suffix_calls;h->body=suffix_body;h->status=geocoding_status;}
        else{++search_calls;h->body=geocoding_body;h->status=geocoding_status;}
    }
    h->size=strlen(h->body);return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char*k,const char*v){
    (void)h;assert((!strcmp(k,"Accept")&&!strcmp(v,"application/json"))||
                   (!strcmp(k,"Accept-Encoding")&&!strcmp(v,"identity")));return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){(void)h;assert(n==0);return ESP_OK;}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){(void)h;assert(ms>0&&ms<=200);return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
    if(!h->headers_sent&&h->date&&h->event_handler) {
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->user_data,
            .header_key="dAtE",.header_value=(char*)h->date};
        assert(h->event_handler(&event)==ESP_OK);
    }
    h->headers_sent=true;return (int64_t)h->size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h->status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int size){
    ++body_reads;assert(size>0&&size<=512);
    if(h->forecast&&cancel_forecast_read){atomic_store(&s_cancel,true);return -1;}
    if(cancel_read){atomic_store(&s_cancel,true);cancel_read=false;return -1;}
    if(stall_body)return -ESP_ERR_HTTP_EAGAIN;
    if(h->at&&fail_body)return ESP_FAIL;
    if(h->at&&incomplete_body)return 0;
    size_t n=h->size-h->at;if(n>(size_t)size)n=(size_t)size;if(n>97)n=97;
    memcpy(out,h->body+h->at,n);h->at+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->at==h->size;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){(void)h;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){free(h);return ESP_OK;}
static void request(app_weather_request_t type,const char *query) {
    assert(app_weather_request(type,query)&&app_weather_busy());
    run_worker();assert(!app_weather_busy());assert(app_weather_poll(false));assert(!pending&&!done);
}
// 用一条近似的行政结果生成 N 个中国城市，覆盖本地分页切分。
// Build N mainland hits so the local page slicing gets exercised.
static const char *geocoding_fixture(unsigned count) {
    if (count == 0) return "{\"results\":[]}";
    cJSON *root=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(root,"results");
    for(unsigned i=0;i<count;++i) {
        cJSON *city=cJSON_CreateObject();
        cJSON_AddNumberToObject(city,"id",1808900+i);
        cJSON_AddStringToObject(city,"name","杭州");
        cJSON_AddNumberToObject(city,"latitude",30.29);
        cJSON_AddNumberToObject(city,"longitude",120.16);
        cJSON_AddStringToObject(city,"country_code","CN");
        cJSON_AddStringToObject(city,"timezone","Asia/Shanghai");
        cJSON_AddStringToObject(city,"country","中国");
        cJSON_AddStringToObject(city,"admin1","浙江");
        cJSON_AddItemToArray(list,city);
    }
    char *json=cJSON_PrintUnformatted(root);assert(json&&strlen(json)<sizeof(geocoding_payload));
    strcpy(geocoding_payload,json);free(json);cJSON_Delete(root);
    return geocoding_payload;
}
static void reset(void) {
    assert(!pending&&!done&&!app_weather_busy());
    app_weather_clear_search();
    monotonic=0;prefs_size=cache_size=0;
    forecast_status=geocoding_status=ip_status=200;
    forecast_body=WEATHER_TEST_FORECAST;geocoding_body=WEATHER_TEST_GEOCODING;
    suffix_body="{\"results\":[]}";
    ip_body=WEATHER_TEST_IP_LOCATION;
    date_header="Mon, 02 Nov 2026 00:30:00 GMT";
    stall_body=fail_body=incomplete_body=cancel_forecast_read=false;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
}
static void pick_city(void) {
    weather_location_t one[1];unsigned count=0;
    assert(weather_om_locations_parse(WEATHER_TEST_GEOCODING_ONE,strlen(WEATHER_TEST_GEOCODING_ONE),one,1,&count));
    assert(count==1&&app_weather_select(&one[0]));
}
int main(void) {
    const time_t server=(time_t)weather_timestamp("2026-11-02T00:30:00Z");
    wall_time=server;

    // 1) 初始化只读设置：没有城市就不发请求。
    // 1) Init only reads settings: no city means no request.
    app_weather_init();
    assert(!s_view.enabled&&!s_view.has_city&&!requests&&s_view.auto_refresh);
    assert(!app_weather_poll(false)&&!requests);

    // 2) 没有城市时先用出口 IP 自动选城，再取预报；表内城市不花一次地理编码往返。
    // 2) With no city the worker locates one from the egress IP, then fetches the forecast; a city
    // present in the built-in table never costs a geocoding round trip.
    unsigned ip_before=ip_calls,search_before=search_calls,forecast_before=forecast_calls;
    request(APP_WEATHER_FETCH,NULL);
    assert(ip_calls==ip_before+1&&search_calls==search_before&&forecast_calls==forecast_before+1);
    assert(s_view.state==APP_WEATHER_READY&&s_view.has_city&&s_view.has_forecast);
    assert(!strcmp(s_view.city.name,"杭州")&&s_view.city.id[0]=='C');
    reset();
    ip_before=ip_calls;
    request(APP_WEATHER_LOCATE,NULL);
    assert(ip_calls==ip_before+1&&s_view.has_city&&s_view.state==APP_WEATHER_READY);
    // 表外地名才走地理编码；夹具把它解析成杭州，城市来自网络结果。
    // A name outside the table falls back to geocoding; the fixture resolves it to 杭州.
    reset();
    ip_body=WEATHER_TEST_IP_LOCATION_UNKNOWN;
    search_before=search_calls;forecast_before=forecast_calls;
    request(APP_WEATHER_LOCATE,NULL);
    assert(search_calls==search_before+1&&forecast_calls==forecast_before+1);
    assert(s_view.has_city&&!strcmp(s_view.city.id,"OM1808926"));
    // IP 服务报错时回退到手动选城，不臆测城市，也不继续查预报。
    // When the IP service reports an error the worker asks for a manual choice instead of
    // inventing a city, and never moves on to the forecast.
    reset();
    ip_body="{\"ret\":\"err\"}";
    ip_before=ip_calls;forecast_before=forecast_calls;
    request(APP_WEATHER_LOCATE,NULL);
    assert(ip_calls==ip_before+1&&forecast_calls==forecast_before);
    assert(s_view.state==APP_WEATHER_CITY_REQUIRED&&!s_view.has_city);
    // 表外地名在地理编码里也没有中国大陆结果时同样回退。
    // The same fallback applies when an off-table name yields no mainland geocoding hit.
    reset();
    ip_body=WEATHER_TEST_IP_LOCATION_UNKNOWN;
    geocoding_body="{\"results\":[]}";
    forecast_before=forecast_calls;
    request(APP_WEATHER_LOCATE,NULL);
    assert(forecast_calls==forecast_before);
    assert(s_view.state==APP_WEATHER_CITY_REQUIRED&&!s_view.has_city);

    // 3) 搜索：一次请求取回整批，本地按 8 条分页。
    // 3) Search fetches one batch and slices it locally, eight per page.
    reset();
    geocoding_body=geocoding_fixture(10);
    const unsigned search_base=search_calls;
    request(APP_WEATHER_SEARCH,"杭州");
    assert(search_calls==search_base+1&&s_view.state==APP_WEATHER_READY);
    assert(s_view.result_count==8&&s_view.result_total==10&&s_view.result_page==0);
    assert(!strcmp(s_view.results[0].id,"OM1808900"));
    const unsigned paging=requests;
    assert(app_weather_search("杭州",1)&&!pending&&s_view.result_count==2&&!strcmp(s_view.results[0].id,"OM1808908"));
    assert(app_weather_search("杭州",0)&&!pending&&s_view.result_count==8);
    assert(!app_weather_search("杭州",2)&&!pending);
    assert(requests==paging);
    // 空结果是成功解析，提示换名而不是报错。
    // An empty result parses successfully and prompts for another name.
    reset();geocoding_body=geocoding_fixture(0);
    request(APP_WEATHER_SEARCH,"杭州");
    assert(s_view.state==APP_WEATHER_READY&&!s_view.result_count&&!s_view.result_total);
    // 源库把部分城市只存成「温州市」：原名查空后补「市」重查应能命中，且只重查一次。
    // Some cities exist only as 「温州市」: after an empty first pass the 市 retry must hit,
    // and it runs exactly once.
    reset();geocoding_body=geocoding_fixture(0);suffix_body=WEATHER_TEST_GEOCODING;
    const unsigned search_mark=search_calls,suffix_mark=suffix_calls;
    request(APP_WEATHER_SEARCH,"温州");
    assert(search_calls==search_mark+1&&suffix_calls==suffix_mark+1);
    assert(s_view.state==APP_WEATHER_READY&&s_view.result_count==2&&s_view.result_total==2);
    // 首查命中时不会多打一次重试。
    // A first-pass hit never triggers the retry.
    reset();
    const unsigned suffix_quiet=suffix_calls;
    request(APP_WEATHER_SEARCH,"杭州");
    assert(suffix_calls==suffix_quiet&&s_view.result_count==2);

    // 4) 选城后取预报：两日、缓存落盘、无线借用后归还。
    // 4) After selecting a city, fetch the forecast: two days, cached, radio borrowed and returned.
    reset();
    pick_city();
    assert(s_view.has_city==true&&!s_view.has_forecast);
    const unsigned radio_starts=starts, radio_stops=stops, forecast_base=forecast_calls;
    request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&s_view.has_forecast);
    // 已有城市时直接查预报，不再走定位。
    // With a city already selected the worker goes straight to the forecast, never locating again.
    assert(forecast_calls==forecast_base+1&&starts==radio_starts+1&&stops==radio_stops+1&&starts==stops);
    assert(cache_size&&prefs_size&&!flash);
    const weather_forecast_t original=s_view.forecast;
    assert(original.days[0].date==weather_local_day(original.fetched));
    assert(original.days[1].date==original.days[0].date+1);
    assert(original.serve==original.fetched+7200&&original.fresh==original.fetched+3600);
    // 缓存在重启后可读回，且不追加写。
    // The cache survives a simulated reboot without extra writes.
    unsigned writes=ns_writes;
    wall_time=original.fetched+60;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    assert(s_view.has_city&&s_view.has_forecast&&ns_writes==writes);

    // 5) 上游错误只降级，不清掉已有预报。
    // 5) Upstream errors only degrade; the cached forecast stays.
    reset();pick_city();request(APP_WEATHER_FETCH,NULL);
    assert(s_view.has_forecast);
    forecast_status=429;
    request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_UNAVAILABLE&&s_view.has_forecast);
    forecast_status=200;forecast_body="{}";
    request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_FAILED&&s_view.has_forecast);
    forecast_body="not json";
    request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_FAILED&&s_view.has_forecast);
    geocoding_status=503;forecast_body=WEATHER_TEST_FORECAST;
    assert(app_weather_search("杭州",0));run_worker();assert(app_weather_poll(false));
    assert(s_view.state==APP_WEATHER_UNAVAILABLE&&s_view.has_forecast);

    // 6) 取消与坏读：不写入半截预报。
    // 6) Cancellation and short reads never persist a half forecast.
    reset();pick_city();
    cancel_forecast_read=true;request(APP_WEATHER_FETCH,NULL);cancel_forecast_read=false;
    assert(!s_view.has_forecast&&!cache_size);
    reset();pick_city();
    incomplete_body=true;request(APP_WEATHER_FETCH,NULL);incomplete_body=false;
    assert(!s_view.has_forecast&&!cache_size);
    reset();pick_city();
    fail_body=true;request(APP_WEATHER_FETCH,NULL);fail_body=false;
    assert(!s_view.has_forecast&&!cache_size&&s_view.state==APP_WEATHER_FAILED);
    reset();pick_city();
    cancel_read=true;request(APP_WEATHER_FETCH,NULL);
    assert(!s_view.has_forecast&&!cache_size);

    // 7) 内置坐标表整表合法，且只做精确匹配。
    // 7) Every built-in coordinate row validates, and lookups are exact-name only.
    assert(sizeof(city_coords)/sizeof(city_coords[0])>=290);
    for(size_t i=0;i<sizeof(city_coords)/sizeof(city_coords[0]);++i) {
        weather_location_t v;
        assert(city_coords_lookup(city_coords[i].name,&v));
        assert(!strcmp(v.name,city_coords[i].name));
        assert(v.lat==city_coords[i].lat&&v.lon==city_coords[i].lon);
        assert(weather_location_valid(&v));
    }
    weather_location_t probe_city;
    assert(city_coords_lookup("杭州",&probe_city)&&probe_city.lat>30&&probe_city.lat<31);
    assert(city_coords_lookup("乌鲁木齐",&probe_city)&&probe_city.lon<88);
    assert(!city_coords_lookup("千岛湖",&probe_city));
    assert(!city_coords_lookup("杭州 ",&probe_city)&&!city_coords_lookup("",&probe_city)&&
           !city_coords_lookup(NULL,&probe_city));

    // 8) 可信源校验：非配置主机一律拒绝，避免被拼到任意地址。
    // 7) Trusted-origin guard: any host outside the configured origins is refused.
    assert(url_trusted("https://fc.test/v1/forecast?latitude=1&longitude=2"));
    assert(url_trusted("https://geo.test/v1/search?name=x"));
    // 定位源单独放行，且只放行它自己的路径。
    // The locate origin is allowed on its own, and only for its own paths.
    assert(url_trusted("https://ip.test/json"));
    assert(!url_trusted("https://ip.test.evil/json"));
    assert(!url_trusted("https://evil.test/json"));
    assert(!url_trusted("https://evil.test/v1/forecast"));
    assert(!url_trusted("https://fc.test.evil/v1/forecast"));
    assert(!url_trusted("https://fc.test@evil/v1/forecast"));
    assert(!url_trusted("https://fc.test:8443/v1/forecast"));
    assert(!url_trusted("http://fc.test/v1/forecast"));
    assert(!url_trusted("https://fc.test"));
    // 主机配置本身也要拒绝带路径、端口、凭据或明文的写法。
    // The configured origins themselves reject paths, ports, credentials and plain HTTP.
    assert(origin_ok("https://fc.test"));
    assert(!origin_ok("https://fc.test/"));
    assert(!origin_ok("https://fc.test:8443"));
    assert(!origin_ok("https://user@fc.test"));
    assert(!origin_ok("http://fc.test"));
    assert(!origin_ok(""));
    assert(!origin_ok("https://"));

    // 8) 前置守卫：OTA、蓝牙、低电量、未存 WiFi、时钟未校准都不联网。
    // 8) Guards: OTA, BLE, low battery, missing WiFi and an unset clock never go online.
    reset();pick_city();unsigned before=requests;
    ota_busy=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));ota_busy=false;
    ble_connected=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));ble_connected=false;
    battery.battery=5;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));battery.battery=80;
    // 没存 WiFi 时 worker 起网前就放弃，一个请求都不发。
    // Without saved WiFi the worker gives up before starting the radio and sends nothing.
    saved=false;request(APP_WEATHER_FETCH,NULL);saved=true;
    assert(s_view.state==APP_WEATHER_UNAVAILABLE&&requests==before);
    wall_time=0;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));wall_time=server;
    assert(requests==before);
    fail_task=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL)&&!app_weather_busy());fail_task=false;
    // 借用已有网络时不重复起停。
    // A borrowed network session is not started or stopped again.
    network_state=WIFI_TRANSFER_READY;unsigned s0=starts,t0=stops;
    request(APP_WEATHER_FETCH,NULL);
    assert(starts==s0&&stops==t0&&s_view.state==APP_WEATHER_READY);network_state=WIFI_TRANSFER_STOPPED;
    // 网络被别的会话占用时给出可读提示。
    // A session owned elsewhere yields a readable notice.
    network_state=1;request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_UNAVAILABLE);network_state=WIFI_TRANSFER_STOPPED;
    // 分配失败不泄漏，也不留忙状态。
    // Allocation failure leaks nothing and never leaves the worker busy.
    fail_alloc=true;request(APP_WEATHER_FETCH,NULL);fail_alloc=false;
    assert(s_view.state==APP_WEATHER_FAILED&&!heap_allocs&&!pending&&!app_weather_busy());

    // 9) 开关持久化：自动更新关闭后不再自动请求。
    // 9) Preference persistence: auto refresh off stops automatic requests.
    reset();pick_city();request(APP_WEATHER_FETCH,NULL);
    assert(app_weather_set_auto_refresh(false)&&!s_view.auto_refresh);
    weather_preferences_t stored;memcpy(&stored,prefs_bytes,sizeof(stored));
    assert(stored.schema==3&&!stored.auto_refresh&&stored.has_city);
    app_weather_clear_search();
    int64_t next=app_weather_now()+WEATHER_REFRESH_SECONDS+1;
    wall_time=(time_t)next;monotonic+=WEATHER_RETRY_US+1;
    assert(!app_weather_poll(true)||!pending);
    assert(app_weather_set_auto_refresh(true)&&s_view.auto_refresh);

    assert(starts==stops&&time_cancels>0&&!heap_allocs&&!pending&&!s_cached_query[0]);
    puts("PASS: Open-Meteo geocoding/forecast service, trusted origins, guards, cache, cancellation, paging");
}
