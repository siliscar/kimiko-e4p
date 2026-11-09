/*
 * 中文：真实服务配确定性HTTP/任务/NVS，覆盖所有权、取消和缓存交接。
 * English: Real service with deterministic HTTP/tasks/NVS covering ownership, cancellation and cache handoff.
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
#include "../main/services/weather_service.c"
#undef time
static void (*pending)(void *);
static bool done,worker,flash,saved=true,fail_flash,fail_task,fail_alloc,cancel_read,cancel_connect,ota_busy,ble_connected;
static unsigned starts,stops,requests,auto_calls,forecast_calls,search_calls,ns_writes,time_cancels;
static int network_state,scenario;
static int64_t monotonic;
static unsigned pending_opens,header_calls;
static unsigned header_eagain,body_eagain,body_reads,heap_allocs;
static unsigned search_total=17;
static int fail_alloc_after=-1,fail_search_page=-1;
static bool stall_headers,stall_body,fail_headers,fail_body,incomplete_body,cancel_headers;
static bool clear_during_read;
static int64_t body_progress_us;
static int io_timeout_ms;
static char *alphanumeric_forecast;
static bool stall_http_connect,cancel_http_connect;
static const char *forecast_date,*catalog_date,*forecast_override;
static bool duplicate_date,cancel_forecast_read,incomplete_forecast;
static int64_t forecast_progress_us;
static unsigned char prefs_bytes[1024],cache_bytes[2048];
static size_t prefs_size,cache_size;
static pmu_snapshot_t battery={80};
struct FakeHttp {
    const char *body,*date;size_t size,at;int status;char payload[WEATHER_BODY_LIMIT+1];
    bool forecast,headers_sent;
    int (*event_handler)(esp_http_client_event_t *);
    void *user_data;
};
static void run_worker(void) {assert(pending);void(*f)(void*)=pending;pending=NULL;worker=true;f(NULL);worker=false;}
void *heap_caps_malloc(size_t n,int c){
    (void)c;if(fail_alloc||fail_alloc_after==0)return NULL;if(fail_alloc_after>0)--fail_alloc_after;
    void *p=malloc(n);if(p)++heap_allocs;return p;
}
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
    assert(!strncmp(c->url,"https://kimiko.mantake.shop/api/v2/components",strlen("https://kimiko.mantake.shop/api/v2/components")));
    struct FakeHttp *h=calloc(1,sizeof(*h));h->status=200;++requests;
    h->event_handler=c->event_handler;h->user_data=c->user_data;
    if(strstr(c->url,"/auto-location")) {
        ++auto_calls;h->body=WEATHER_TEST_CITY;
        if(scenario==1){h->body="{\"code\":\"CITY_SELECTION_REQUIRED\"}";h->status=503;}
    } else if(strstr(c->url,"/forecast?")) {
        assert(strstr(c->url,scenario==6?"locationId=F4F1":"locationId=101210101")&&!strstr(c->url,"lat="));
        ++forecast_calls;h->body=scenario==6?alphanumeric_forecast:WEATHER_TEST_FORECAST;
        h->forecast=true;h->date=forecast_date;
        if(forecast_override)h->body=forecast_override;
        if(scenario==3){h->body="{\"code\":\"WEATHER_UPSTREAM_UNAVAILABLE\"}";h->status=503;}
        if(scenario==4){h->body="{}";}
        if(scenario==5){h->body="{\"code\":\"BUDGET_EXHAUSTED\"}";h->status=429;}
    } else if(strstr(c->url,"/locations?")) {
        ++search_calls;assert(strstr(c->url,"q=%E6%9D%AD%E5%B7%9E")||strstr(c->url,"q=%E4%B8%8A%E6%B5%B7"));
        unsigned page,size;assert(sscanf(strstr(c->url,"&page="),"&page=%u&pageSize=%u",&page,&size)==2&&size==20);
        cJSON *root=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(root,"locations");
        cJSON_AddNumberToObject(root,"total",search_total);cJSON_AddNumberToObject(root,"page",page);cJSON_AddNumberToObject(root,"pageSize",size);
        for(unsigned i=page*size;i<search_total&&i<(page+1)*size;++i){
            cJSON *city=cJSON_Parse(WEATHER_TEST_CITY);char id[17];snprintf(id,sizeof(id),"%u",101210101+i);
            cJSON_ReplaceItemInObject(city,"locationId",cJSON_CreateString(id));cJSON_AddItemToArray(list,city);
        }
        char *json=cJSON_PrintUnformatted(root);assert(json&&strlen(json)<sizeof(h->payload));
        strcpy(h->payload,json);free(json);cJSON_Delete(root);h->body=h->payload;
        if((int)page==fail_search_page){h->body="{}";h->status=503;}
    } else {
        h->body=WEATHER_TEST_CATALOG;h->date=catalog_date;
        if(scenario==2)h->body="{\"schemaVersion\":1,\"components\":[{\"code\":\"weather\",\"schemaVersion\":1,\"enabled\":false,\"available\":false}]}";
    }
    h->size=strlen(h->body);return h;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char*k,const char*v){
    (void)h;assert((!strcmp(k,"Accept")&&!strcmp(v,"application/json"))||(!strcmp(k,"Accept-Encoding")&&!strcmp(v,"identity")));return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t h,int n){
    (void)h;assert(n==0);
    if(cancel_http_connect){atomic_store(&s_cancel,true);return ESP_ERR_HTTP_EAGAIN;}
    if(stall_http_connect)return ESP_ERR_HTTP_EAGAIN;
    if(pending_opens){--pending_opens;return ESP_ERR_HTTP_EAGAIN;}
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t h,int ms){(void)h;assert(ms>0&&ms<=200);io_timeout_ms=ms;return ESP_OK;}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h){
    ++header_calls;
    if(cancel_headers){atomic_store(&s_cancel,true);return -ESP_ERR_HTTP_EAGAIN;}
    if(fail_headers)return ESP_FAIL;
    if(header_eagain||stall_headers){if(header_eagain)--header_eagain;monotonic+=(int64_t)io_timeout_ms*1000;return -ESP_ERR_HTTP_EAGAIN;}
    if(!h->headers_sent&&h->date&&h->event_handler) {
        esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=h->user_data,
            .header_key="dAtE",.header_value=(char*)h->date};
        assert(h->event_handler(&event)==ESP_OK);
        if(duplicate_date)assert(h->event_handler(&event)==ESP_OK);
    }
    h->headers_sent=true;
    return (int64_t)h->size;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){return h->status;}
int esp_http_client_read(esp_http_client_handle_t h,char *out,int size){
    ++body_reads;assert(size>0&&size<=512);
    if(h->forecast&&cancel_forecast_read){atomic_store(&s_cancel,true);return -1;}
    if(h->forecast&&h->at&&incomplete_forecast)return 0;
    if(cancel_read){atomic_store(&s_cancel,true);cancel_read=false;return -1;}
    if(clear_during_read){app_weather_clear_search();clear_during_read=false;}
    if(body_eagain||stall_body){if(body_eagain)--body_eagain;monotonic+=(int64_t)io_timeout_ms*1000;return -ESP_ERR_HTTP_EAGAIN;}
    if(h->at&&fail_body)return ESP_FAIL;
    if(h->at&&incomplete_body)return 0;
    size_t n=h->size-h->at;if(n>(size_t)size)n=(size_t)size;if(n>97)n=97;
    if(n)monotonic+=body_progress_us+(h->forecast?forecast_progress_us:0);
    memcpy(out,h->body+h->at,n);h->at+=n;return (int)n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){return h->at==h->size;}
esp_err_t esp_http_client_close(esp_http_client_handle_t h){(void)h;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){free(h);return ESP_OK;}
static void request(app_weather_request_t type,const char *query) {
    assert(app_weather_request(type,query)&&app_weather_busy());
    run_worker();assert(!app_weather_busy());assert(app_weather_poll(false));assert(!pending&&!done);
}
static void clock_setup(time_t device_time) {
    assert(!pending&&!s_pending&&!done&&!app_weather_busy());
    app_weather_clear_search();assert(!heap_allocs);
    wall_time=device_time;monotonic=0;prefs_size=cache_size=0;
    forecast_date="Mon, 02 Nov 2026 00:30:00 GMT";
    catalog_date=forecast_override=NULL;duplicate_date=cancel_forecast_read=incomplete_forecast=false;
    forecast_progress_us=0;scenario=0;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    weather_location_t city;
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&city)&&app_weather_select(&city));
}
static void weather_clocks(void) {
    const int64_t server=weather_timestamp("2026-11-02T00:30:00Z");
    const int64_t fetched=weather_timestamp("2026-11-02T00:20:00Z");
    const int64_t skewed[]={fetched-120,fetched-121,fetched-3600,fetched+3600,fetched+86400};
    for(unsigned i=0;i<sizeof(skewed)/sizeof(*skewed);++i) {
        clock_setup(skewed[i]);request(APP_WEATHER_FETCH,NULL);
        assert(s_view.state==APP_WEATHER_READY&&s_view.has_forecast&&s_display_valid);
        assert(wall_time==skewed[i]&&app_weather_now()==server);
        weather_cache_t cache;memcpy(&cache,cache_bytes,sizeof(cache));
        assert(cache.schema==2&&cache.forecast.fetched==fetched&&cache.forecast.serve==fetched+21600);
    }
    for(unsigned i=0;i<4;++i) {
        clock_setup(fetched-121);
        if(i==0)forecast_date=NULL;
        if(i==1)forecast_date="invalid";
        if(i==2)duplicate_date=true;
        if(i==3){forecast_date=NULL;catalog_date="Mon, 02 Nov 2026 00:30:00 GMT";}
        request(APP_WEATHER_FETCH,NULL);
        assert(s_view.state==APP_WEATHER_FAILED&&!s_view.has_forecast&&app_weather_now()==wall_time);
    }
    clock_setup(server);forecast_date=NULL;request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&s_view.has_forecast&&app_weather_now()==wall_time);
    const char *invalid_dates[]={"Mon, 02 Nov 2026 00:17:59 GMT","Mon, 02 Nov 2026 06:20:00 GMT"};
    for(unsigned i=0;i<2;++i) {
        clock_setup(server);forecast_date=invalid_dates[i];request(APP_WEATHER_FETCH,NULL);
        assert(s_view.state==APP_WEATHER_FAILED&&!s_view.has_forecast&&app_weather_now()==wall_time);
    }
    clock_setup(fetched-121);forecast_date="Mon, 02 Nov 2026 00:18:00 GMT";request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&s_view.has_forecast);
    for(unsigned i=0;i<6;++i) {
        clock_setup(fetched-3600);
        if(i==0)scenario=3;
        if(i==1)scenario=4;
        if(i==2)incomplete_forecast=true;
        if(i==3)cancel_forecast_read=true;
        if(i==4)fail_flash=true;
        if(i==5) {
            assert(app_weather_request(APP_WEATHER_FETCH,NULL));run_worker();app_weather_cancel_join();
        } else request(i==4?APP_WEATHER_LOCATE:APP_WEATHER_FETCH,NULL);
        fail_flash=false;
        assert(!s_view.has_forecast&&!cache_size&&app_weather_now()==wall_time);
    }
    clock_setup(fetched-3600);forecast_progress_us=250000;
    assert(app_weather_request(APP_WEATHER_FETCH,NULL));run_worker();
    assert(monotonic>0&&app_weather_now()==wall_time&&!s_view.has_forecast);
    forecast_progress_us=0;monotonic+=7000000;wall_time+=86400;
    assert(app_weather_poll(false)&&s_display_valid&&app_weather_now()==server+monotonic/1000000);
    int64_t weather_now=app_weather_now(),serve=s_view.forecast.serve;
    wall_time=fetched-86400;assert(app_weather_now()==weather_now);
    forecast_date="Mon, 02 Nov 2026 00:25:00 GMT";request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&app_weather_now()==weather_now);
    forecast_date=NULL;request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&app_weather_now()==weather_now);
    scenario=3;request(APP_WEATHER_FETCH,NULL);scenario=0;
    assert(s_view.has_forecast&&app_weather_now()==weather_now&&s_display_valid);
    monotonic+=600000000;forecast_date="Mon, 02 Nov 2026 00:40:00 GMT";
    request(APP_WEATHER_FETCH,NULL);
    assert(s_view.state==APP_WEATHER_READY&&s_view.forecast.fetched==fetched&&s_view.forecast.serve==serve);
    monotonic+=(serve-app_weather_now()-1)*1000000;
    wall_time=server+86400;app_weather_poll(false);
    assert(s_display_valid&&weather_day_for(&s_view.forecast,app_weather_now(),false));
    wall_time=fetched-86400;monotonic+=1000000;
    assert(app_weather_poll(true)&&!s_display_valid&&pending);
    assert(!weather_day_for(&s_view.forecast,app_weather_now(),false));
    scenario=3;run_worker();app_weather_poll(false);
    assert(!s_display_valid&&app_weather_now()>=serve&&s_view.forecast.serve==serve);

    clock_setup(server);
    cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObject(root,"fetchedAt",cJSON_CreateString("2026-11-02T15:30:00Z"));
    cJSON_ReplaceItemInObject(root,"freshUntil",cJSON_CreateString("2026-11-02T21:30:00Z"));
    cJSON_ReplaceItemInObject(root,"serveUntil",cJSON_CreateString("2026-11-02T21:30:00Z"));
    char *midnight=cJSON_PrintUnformatted(root);cJSON_Delete(root);assert(midnight);
    forecast_override=midnight;forecast_date="Mon, 02 Nov 2026 15:59:59 GMT";
    request(APP_WEATHER_FETCH,NULL);assert(s_view.has_forecast&&s_display_valid);
    assert(weather_day_for(&s_view.forecast,app_weather_now(),false)->humidity==68);
    monotonic+=1000000;assert(app_weather_poll(false));
    assert(weather_day_for(&s_view.forecast,app_weather_now(),false)->humidity==82);
    assert(!weather_day_for(&s_view.forecast,app_weather_now(),true));
    monotonic+=WEATHER_RETRY_US;assert(app_weather_poll(true)&&pending);app_weather_cancel_join();
    free(midnight);forecast_override=NULL;
    unsigned writes=ns_writes;weather_cache_t cache;
    memcpy(&cache,cache_bytes,sizeof(cache));wall_time=cache.forecast.fetched-121;monotonic=0;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    assert(s_view.has_city&&!s_view.has_forecast&&app_weather_now()==wall_time&&ns_writes==writes);
    weather_cache_t unchanged;memcpy(&unchanged,cache_bytes,sizeof(unchanged));
    assert(!memcmp(&cache,&unchanged,sizeof(cache)));
    wall_time=cache.forecast.fetched+60;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    assert(s_view.has_forecast&&app_weather_now()==wall_time&&ns_writes==writes);
    assert(starts==stops&&!heap_allocs&&!pending&&!done);
    puts("PASS: weather HTTPS Date reference, clock skew, strict fallback, original expiry, monotonic delays, midnight and reboot isolation");
}
int main(void) {
    wall_time=weather_timestamp("2026-11-02T00:30:00Z");
    app_weather_init();assert(!s_view.enabled&&!s_view.has_city&&!requests&&s_view.auto_refresh);
    assert(!app_weather_poll(false)&&!requests);
    assert(app_weather_poll(true)&&pending&&!s_view.enabled);
    run_worker();assert(app_weather_poll(false));
    assert(s_view.has_city&&s_view.has_forecast&&auto_calls==1&&forecast_calls==1&&starts==stops);
    assert(cache_size&&prefs_size&&!flash);
    assert(app_weather_set_enabled(true));
    const time_t base_time=wall_time;
    weather_forecast_t original_forecast=s_view.forecast;
    unsigned writes_before=ns_writes;
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    assert(s_view.enabled&&s_view.has_city&&s_view.has_forecast&&s_view.auto_refresh&&ns_writes==writes_before);
    fail_flash=true;assert(!app_weather_set_auto_refresh(false)&&s_view.auto_refresh);fail_flash=false;
    assert(app_weather_set_auto_refresh(false)&&!s_view.auto_refresh);
    weather_preferences_t stored;memcpy(&stored,prefs_bytes,sizeof(stored));
    assert(stored.schema==3&&!stored.auto_refresh&&stored.has_city&&stored.enabled);
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();assert(!s_view.auto_refresh&&s_view.has_forecast);
    wall_time=original_forecast.fetched+WEATHER_REFRESH_SECONDS;monotonic+=WEATHER_RETRY_US+1;
    app_weather_poll(true);assert(!pending);
    wall_time=base_time;request(APP_WEATHER_FETCH,NULL);assert(!s_view.auto_refresh&&s_view.has_forecast);
    weather_location_t selected=s_view.city;
    assert(app_weather_select(&selected)&&!s_view.auto_refresh);
    assert(app_weather_set_enabled(false)&&!s_view.auto_refresh);
    assert(app_weather_set_enabled(true)&&!s_view.auto_refresh);
    request(APP_WEATHER_FETCH,NULL);assert(!s_view.auto_refresh&&s_view.has_forecast);
    assert(app_weather_request(APP_WEATHER_FETCH,NULL));unsigned starts_before=starts;
    assert(app_weather_set_auto_refresh(false)&&!pending&&!s_pending&&starts==starts_before);
    assert(app_weather_set_auto_refresh(true));
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();assert(s_view.auto_refresh&&s_view.has_forecast);
    wall_time=original_forecast.fetched+3601;monotonic+=WEATHER_RETRY_US+1;
    app_weather_poll(true);assert(!pending);
    wall_time=original_forecast.fetched+WEATHER_REFRESH_SECONDS-1;app_weather_poll(true);assert(!pending);
    wall_time++;app_weather_poll(false);assert(!pending);
    app_weather_poll(true);assert(pending);
    scenario=3;run_worker();app_weather_poll(true);assert(!pending&&s_view.state==APP_WEATHER_UNAVAILABLE);
    monotonic=s_next_attempt-1;app_weather_poll(true);assert(!pending);
    monotonic++;app_weather_poll(true);assert(pending);app_weather_cancel_join();scenario=0;
    wall_time=base_time;s_view.forecast=original_forecast;
    s_view.forecast.serve=s_view.forecast.fetched+7200;
    s_view.forecast.fresh=s_view.forecast.fetched+3600;
    monotonic+=WEATHER_RETRY_US+1;wall_time=s_view.forecast.serve-1;app_weather_poll(true);assert(!pending);
    wall_time++;app_weather_poll(true);assert(pending);app_weather_cancel_join();
    s_view.forecast=original_forecast;
    s_view.forecast.fetched=weather_timestamp("2026-11-02T15:50:00Z");
    s_view.forecast.fresh=s_view.forecast.fetched+3600;s_view.forecast.serve=s_view.forecast.fetched+21600;
    wall_time=weather_timestamp("2026-11-02T16:10:00Z");monotonic+=WEATHER_RETRY_US+1;
    assert(weather_forecast_valid(&s_view.forecast,wall_time));
    app_weather_poll(true);assert(pending);app_weather_cancel_join();
    s_view.forecast=original_forecast;wall_time=base_time;
    s_next_attempt=0;s_display_day=weather_local_day(wall_time);s_display_valid=true;
    unsigned before=requests;
    for(int i=0;i<40;i++)assert(!app_weather_poll(true));
    assert(requests==before);
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();assert(s_view.enabled&&s_view.has_forecast);
    fail_flash=true;assert(!app_weather_set_enabled(false)&&s_view.enabled);fail_flash=false;
    network_state=WIFI_TRANSFER_READY;before=stops;request(APP_WEATHER_FETCH,NULL);
    assert(stops==before&&network_state==WIFI_TRANSFER_READY&&s_view.has_forecast&&s_view.state==APP_WEATHER_READY);network_state=0;
    network_state=1;before=stops;request(APP_WEATHER_FETCH,NULL);
    assert(stops==before&&s_view.state==APP_WEATHER_UNAVAILABLE);network_state=0;
    scenario=3;request(APP_WEATHER_FETCH,NULL);assert(s_view.has_forecast&&s_view.state==APP_WEATHER_UNAVAILABLE);
    scenario=4;request(APP_WEATHER_FETCH,NULL);assert(s_view.has_forecast&&s_view.state==APP_WEATHER_FAILED);
    scenario=5;request(APP_WEATHER_FETCH,NULL);assert(s_view.state==APP_WEATHER_UNAVAILABLE);
    scenario=2;before=forecast_calls;request(APP_WEATHER_FETCH,NULL);
    assert(!s_view.has_forecast&&!cache_size&&forecast_calls==before&&s_view.state==APP_WEATHER_DISABLED);
    scenario=1;request(APP_WEATHER_LOCATE,NULL);assert(s_view.state==APP_WEATHER_CITY_REQUIRED);
    monotonic+=WEATHER_RETRY_US+1;before=auto_calls;app_weather_poll(true);assert(!pending&&auto_calls==before);
    scenario=0;request(APP_WEATHER_SEARCH,"杭州");assert(s_view.result_count==8&&s_view.result_total==17&&search_calls==1);
    unsigned paging_requests=requests,paging_starts=starts,paging_cancels=time_cancels,paging_writes=ns_writes;
    ota_busy=ble_connected=true;battery.battery=5;wall_time=0;
    assert(app_weather_search("杭州",1)&&!pending&&!app_weather_busy());
    assert(s_view.result_page==1&&s_view.result_total==17&&s_view.result_count==8&&!strcmp(s_view.results[0].id,"101210109"));
    assert(app_weather_search("杭州",2)&&!pending&&s_view.result_count==1&&!strcmp(s_view.results[0].id,"101210117"));
    assert(app_weather_search("杭州",0)&&!pending&&s_view.result_count==8);
    assert(!app_weather_search("杭州",3)&&!pending);
    assert(requests==paging_requests&&starts==paging_starts&&time_cancels==paging_cancels&&ns_writes==paging_writes);
    ota_busy=ble_connected=false;battery.battery=80;wall_time=base_time;
    search_total=41;request(APP_WEATHER_SEARCH,"杭州");paging_requests=requests;
    assert(app_weather_search("杭州",2)&&pending);run_worker();assert(app_weather_poll(false));
    assert(requests==paging_requests+2&&s_view.result_page==2&&s_view.result_count==8&&!strcmp(s_view.results[7].id,"101210124"));
    paging_requests=requests;
    assert(app_weather_search("杭州",3)&&!pending&&s_view.result_count==8);
    assert(app_weather_search("杭州",4)&&!pending&&s_view.result_count==8&&requests==paging_requests);
    fail_search_page=2;assert(app_weather_search("杭州",5)&&pending);run_worker();assert(app_weather_poll(false));
    assert(s_view.state==APP_WEATHER_UNAVAILABLE&&s_view.result_page==4&&s_view.result_count==8);
    fail_search_page=-1;assert(app_weather_search("杭州",5)&&pending);run_worker();assert(app_weather_poll(false));
    assert(s_view.result_page==5&&s_view.result_count==1&&!strcmp(s_view.results[0].id,"101210141"));
    paging_requests=requests;assert(app_weather_search("杭州",0)&&!pending&&requests==paging_requests);
    request(APP_WEATHER_SEARCH,"上海");paging_requests=requests;
    assert(app_weather_search("杭州",1)&&pending);run_worker();assert(app_weather_poll(false));
    assert(requests==paging_requests+2&&s_view.result_page==1);
    app_weather_clear_search();assert(!heap_allocs&&!s_cached_query[0]);
    fail_search_page=1;assert(app_weather_search("杭州",2));run_worker();assert(app_weather_poll(false));
    assert(s_view.state==APP_WEATHER_UNAVAILABLE&&!s_view.result_count&&!heap_allocs);
    fail_search_page=-1;
    search_total=4000;assert(app_weather_search("杭州",499));run_worker();assert(app_weather_poll(false));
    assert(s_view.result_page==499&&s_view.result_count==8&&!strcmp(s_view.results[7].id,"101214100"));
    assert(!app_weather_search("杭州",500)&&!app_weather_search("杭州",~0u));
    app_weather_clear_search();search_total=17;
    assert(app_weather_request(APP_WEATHER_SEARCH,"杭州"));app_weather_clear_search();run_worker();
    assert(app_weather_poll(false)&&s_view.result_count==0&&!heap_allocs);
    assert(app_weather_request(APP_WEATHER_SEARCH,"杭州"));run_worker();app_weather_clear_search();
    assert(app_weather_poll(false)&&!s_view.result_count&&!heap_allocs);
    clear_during_read=true;request(APP_WEATHER_SEARCH,"杭州");assert(!s_view.result_count&&!heap_allocs);
    fail_alloc_after=1;request(APP_WEATHER_SEARCH,"杭州");fail_alloc_after=-1;
    assert(s_view.state==APP_WEATHER_FAILED&&!s_view.result_count&&!heap_allocs);
    search_total=41;fail_alloc_after=2;assert(app_weather_search("杭州",2));run_worker();assert(app_weather_poll(false));fail_alloc_after=-1;
    assert(s_view.state==APP_WEATHER_FAILED&&!s_view.result_count&&!heap_allocs);
    search_total=17;
    assert(app_weather_request(APP_WEATHER_SEARCH,"杭州"));run_worker();app_weather_cancel_join();assert(!heap_allocs);
    search_total=0;request(APP_WEATHER_SEARCH,"杭州");paging_requests=requests;
    assert(!s_view.result_count&&!s_view.result_total&&s_view.state==APP_WEATHER_READY);
    assert(app_weather_search("杭州",0)&&!pending&&requests==paging_requests);
    app_weather_clear_search();search_total=17;
    request(APP_WEATHER_SEARCH,"杭州");assert(s_view.result_count==8);
    weather_location_t city=s_view.results[0];assert(app_weather_select(&city)&&s_view.result_count==0&&!heap_allocs);
    request(APP_WEATHER_FETCH,NULL);assert(s_view.has_forecast);
    before=starts;assert(app_weather_request(APP_WEATHER_FETCH,NULL));
    app_weather_cancel_join();assert(!pending&&!app_weather_busy()&&starts==before);
    cancel_connect=true;request(APP_WEATHER_FETCH,NULL);cancel_connect=false;assert(starts==stops);
    cancel_read=true;request(APP_WEATHER_FETCH,NULL);assert(starts==stops&&s_view.has_forecast);
    pending_opens=80;int64_t connect_started=monotonic;
    request(APP_WEATHER_FETCH,NULL);
    assert(!pending_opens&&monotonic-connect_started>=1600000&&s_view.has_forecast&&s_view.state==APP_WEATHER_READY);
    stall_http_connect=true;connect_started=monotonic;unsigned headers_before=header_calls;
    request(APP_WEATHER_FETCH,NULL);stall_http_connect=false;
    assert(monotonic-connect_started==WEATHER_CONNECT_US&&headers_before==header_calls&&starts==stops);
    cancel_http_connect=true;connect_started=monotonic;headers_before=header_calls;
    request(APP_WEATHER_FETCH,NULL);cancel_http_connect=false;
    assert(monotonic-connect_started<=20000&&headers_before==header_calls&&starts==stops);
    header_eagain=20;connect_started=monotonic;
    request(APP_WEATHER_FETCH,NULL);
    assert(!header_eagain&&s_view.state==APP_WEATHER_READY&&monotonic-connect_started>=4000000);
    stall_headers=true;connect_started=monotonic;unsigned reads_before=body_reads;
    request(APP_WEATHER_FETCH,NULL);stall_headers=false;
    assert(s_view.state==APP_WEATHER_FAILED&&monotonic-connect_started>=WEATHER_RESPONSE_US&&
        monotonic-connect_started<=WEATHER_RESPONSE_US+20000&&body_reads==reads_before);
    fail_headers=true;headers_before=header_calls;request(APP_WEATHER_FETCH,NULL);fail_headers=false;
    assert(s_view.state==APP_WEATHER_FAILED&&header_calls==headers_before+1);
    cancel_headers=true;connect_started=monotonic;request(APP_WEATHER_FETCH,NULL);cancel_headers=false;
    assert(monotonic-connect_started<=20000&&s_view.has_forecast);
    body_eagain=20;connect_started=monotonic;request(APP_WEATHER_FETCH,NULL);
    assert(!body_eagain&&s_view.state==APP_WEATHER_READY&&monotonic-connect_started>=4000000);
    stall_body=true;connect_started=monotonic;request(APP_WEATHER_FETCH,NULL);stall_body=false;
    assert(s_view.state==APP_WEATHER_FAILED&&monotonic-connect_started>=WEATHER_RESPONSE_US&&
        monotonic-connect_started<=WEATHER_RESPONSE_US+20000);
    fail_body=true;reads_before=body_reads;request(APP_WEATHER_FETCH,NULL);fail_body=false;
    assert(s_view.state==APP_WEATHER_FAILED&&body_reads==reads_before+2);
    incomplete_body=true;request(APP_WEATHER_FETCH,NULL);incomplete_body=false;
    assert(s_view.state==APP_WEATHER_FAILED&&s_view.has_forecast);
    body_progress_us=600000;connect_started=monotonic;request(APP_WEATHER_FETCH,NULL);body_progress_us=0;
    assert(s_view.state==APP_WEATHER_READY&&monotonic-connect_started>WEATHER_RESPONSE_US&&starts==stops&&!heap_allocs);
    saved=false;before=starts;request(APP_WEATHER_FETCH,NULL);assert(starts==before);saved=true;
    fail_alloc=true;request(APP_WEATHER_FETCH,NULL);fail_alloc=false;assert(starts==before);
    fail_task=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL)&&!app_weather_busy());fail_task=false;
    ota_busy=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));ota_busy=false;
    ble_connected=true;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));ble_connected=false;
    battery.battery=5;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));battery.battery=80;
    scenario=0;before=auto_calls;unsigned searches_before=search_calls;request(APP_WEATHER_LOCATE,NULL);
    assert(auto_calls==before+1&&search_calls==searches_before&&s_view.has_forecast&&!strcmp(s_view.city.name,"杭州"));
    assert(!strcmp(s_view.city.id,"101210101"));
    for(int i=0;i<20;i++){monotonic+=WEATHER_RETRY_US+1;app_weather_poll(true);assert(!pending);}
    assert(search_calls==searches_before);
    fail_flash=true;request(APP_WEATHER_LOCATE,NULL);fail_flash=false;
    assert(!strcmp(s_view.city.name,"杭州")&&s_view.notice[0]);
    assert(weather_location_parse(WEATHER_TEST_ALPHA_CITY,strlen(WEATHER_TEST_ALPHA_CITY),&city));
    assert(app_weather_select(&city));
    cJSON *alpha=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObject(alpha,"location",cJSON_Parse(WEATHER_TEST_ALPHA_CITY));
    alphanumeric_forecast=cJSON_PrintUnformatted(alpha);cJSON_Delete(alpha);assert(alphanumeric_forecast);
    scenario=6;before=search_calls;request(APP_WEATHER_FETCH,NULL);
    assert(s_view.has_forecast&&!strcmp(s_view.forecast.location.id,"F4F1")&&search_calls==before);
    s_view=(app_weather_view_t){0};s_loaded=false;app_weather_init();
    assert(s_view.has_city&&s_view.has_forecast&&!strcmp(s_view.city.id,"F4F1"));
    free(alphanumeric_forecast);alphanumeric_forecast=NULL;
    wall_time=0;assert(!app_weather_request(APP_WEATHER_FETCH,NULL));
    assert(!weather_day_for(&s_view.forecast,wall_time,false));
    assert(starts==stops&&time_cancels>0&&!heap_allocs);
    weather_clocks();
    puts("PASS: weather proxy session, six-hour boundary, shorter expiry, midnight, toggle persistence, city-ID fetch, paginated search, no device relabeling, owned cleanup, cancellation, NVS main-only and resource guards");
}
