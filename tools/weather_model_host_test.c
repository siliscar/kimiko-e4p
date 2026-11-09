/*
 * 中文：真实天气模型的边界、过期、跨日与坏响应回归。
 * English: Real weather model regressions for bounds, expiry, midnight and malformed responses.
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
static bool parse(cJSON *root) {
    char *json=cJSON_PrintUnformatted(root);weather_forecast_t f;
    bool ok=weather_forecast_parse(json,strlen(json),now,&city,&f);free(json);return ok;
}
static void invalid_field(const char *field,const char *value) {
    cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObjectCaseSensitive(root,field,cJSON_CreateString(value));
    assert(!parse(root));cJSON_Delete(root);
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
int main(void) {
    http_dates();
    now=weather_timestamp("2026-11-02T00:30:00Z");assert(now);
    assert(weather_location_parse(WEATHER_TEST_CITY,strlen(WEATHER_TEST_CITY),&city));
    weather_forecast_t f={0};
    assert(weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),now,&city,&f));
    assert(weather_day_for(&f,now,false)->humidity==68&&weather_day_for(&f,now,true)->min_c==16);
    char text[193];
    weather_date_text(weather_local_day(now),text,sizeof(text));assert(!strcmp(text,"11月02日  星期一"));
    weather_time_text(f.days[0].sunrise,text,sizeof(text));assert(!strcmp(text,"06:15"));
    weather_time_text(f.days[0].sunset,text,sizeof(text));assert(!strcmp(text,"17:12"));
    weather_time_text(0,text,sizeof(text));assert(!strcmp(text,"--:--"));
    assert(weather_forecast_valid(&f,f.serve-1)&&!weather_forecast_valid(&f,f.serve));
    assert(weather_forecast_valid(&f,f.fetched-120)&&!weather_forecast_valid(&f,f.fetched-121));
    assert(!weather_day_for(&f,0,false)&&!weather_forecast_valid(&f,now-3600));
    assert(!weather_forecast_parse("{} trailing",11,now,&city,&f));
    invalid_field("source","Other");invalid_field("fetchedAt","2026-11-02T08:20:00Z");
    invalid_field("freshUntil","2026-11-01T00:00:00Z");invalid_field("serveUntil","2026-11-03T00:00:00Z");
    invalid_field("stale","false");invalid_field("schemaVersion","1");
    const char *bad_dates[]={"2026-02-29T00:00:00Z","2026-11-02T24:00:00Z","2026-11-02T00:60:00Z",
        "2026-11-02T00:00:60Z","2026-11-02T00:00:00+08:00","2026-11-02T00:00:00.Z","abc"};
    for(unsigned i=0;i<sizeof(bad_dates)/sizeof(*bad_dates);++i)assert(!weather_timestamp(bad_dates[i]));
    assert(weather_timestamp("2028-02-29T23:59:59.123Z"));
    for(int i=0;i<8;++i) {
        cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);cJSON *d=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"days"),0);
        if(i==0)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"dayHumidityPct"),101);
        if(i==1)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"minC"),25);
        if(i==2)cJSON_ReplaceItemInObject(d,"date",cJSON_CreateString("2026-11-03"));
        if(i==3)cJSON_ReplaceItemInObject(d,"conditionCode",cJSON_CreateString("abc"));
        if(i==4)cJSON_ReplaceItemInObject(d,"sunrise",cJSON_CreateString("2026-11-02T22:15:00Z"));
        if(i==5)cJSON_DeleteItemFromObject(d,"sunset");
        if(i==6)cJSON_ReplaceItemInObject(d,"conditionText",cJSON_CreateString("晴\n伪造"));
        if(i==7)cJSON_SetNumberValue(cJSON_GetObjectItem(d,"dayHumidityPct"),1.5);
        assert(!parse(root));cJSON_Delete(root);
    }
    cJSON *root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON *d=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"days"),0);
    cJSON_ReplaceItemInObject(d,"sunrise",cJSON_CreateNull());cJSON_ReplaceItemInObject(d,"sunset",cJSON_CreateNull());
    assert(parse(root));cJSON_Delete(root);
    bool enabled,available;
    assert(weather_catalog_parse(WEATHER_TEST_CATALOG,strlen(WEATHER_TEST_CATALOG),&enabled,&available)&&enabled&&available);
    assert(!weather_catalog_parse("{\"schemaVersion\":2,\"components\":[]}",35,&enabled,&available));
    root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObject(root,"fetchedAt",cJSON_CreateString("2026-11-02T15:30:00Z"));
    cJSON_ReplaceItemInObject(root,"freshUntil",cJSON_CreateString("2026-11-02T16:30:00Z"));
    cJSON_ReplaceItemInObject(root,"serveUntil",cJSON_CreateString("2026-11-02T21:30:00Z"));
    char *json=cJSON_PrintUnformatted(root);now=weather_timestamp("2026-11-02T16:10:00Z");
    assert(weather_forecast_parse(json,strlen(json),now,&city,&f));
    assert(weather_day_for(&f,now,false)->humidity==82&&!weather_day_for(&f,now,true));
    free(json);cJSON_Delete(root);
    assert(weather_scene(999)==WEATHER_SCENE_NEUTRAL&&weather_scene(100)==WEATHER_SCENE_SUN);
    assert(weather_scene(302)==WEATHER_SCENE_STORM&&weather_scene(400)==WEATHER_SCENE_SNOW);
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
    weather_location_t results[WEATHER_CITY_LIMIT];unsigned count,total,page;
    assert(WEATHER_CITY_LIMIT==8);
    const char *last="{\"total\":9,\"page\":1,\"pageSize\":8,\"locations\":[" WEATHER_TEST_CITY "]}";
    assert(weather_locations_parse(last,strlen(last),results,WEATHER_CITY_LIMIT,&count,&total,&page)&&count==1&&total==9&&page==1);
    const char *bad_page="{\"total\":9,\"page\":0,\"pageSize\":8,\"locations\":[" WEATHER_TEST_CITY "]}";
    assert(!weather_locations_parse(bad_page,strlen(bad_page),results,WEATHER_CITY_LIMIT,&count,&total,&page)&&!count);
    root=cJSON_Parse("{\"total\":14,\"page\":0,\"pageSize\":8,\"locations\":[]}");
    cJSON *locations=cJSON_GetObjectItem(root,"locations");
    for(unsigned i=0;i<WEATHER_CITY_LIMIT;++i)cJSON_AddItemToArray(locations,cJSON_Parse(WEATHER_TEST_CITY));
    json=cJSON_PrintUnformatted(root);
    assert(weather_locations_parse(json,strlen(json),results,WEATHER_CITY_LIMIT,&count,&total,&page)&&count==8&&total==14&&!page);
    free(json);cJSON_SetNumberValue(cJSON_GetObjectItem(root,"page"),1);
    cJSON_DeleteItemFromArray(locations,7);cJSON_DeleteItemFromArray(locations,6);
    json=cJSON_PrintUnformatted(root);
    assert(weather_locations_parse(json,strlen(json),results,WEATHER_CITY_LIMIT,&count,&total,&page)&&count==6&&total==14&&page==1);
    free(json);cJSON_SetNumberValue(cJSON_GetObjectItem(root,"pageSize"),5);
    json=cJSON_PrintUnformatted(root);
    assert(!weather_locations_parse(json,strlen(json),results,WEATHER_CITY_LIMIT,&count,&total,&page)&&!count);
    free(json);cJSON_Delete(root);
    weather_location_t batch[WEATHER_SEARCH_BATCH_SIZE];
    root=cJSON_Parse("{\"total\":41,\"page\":0,\"pageSize\":20,\"locations\":[]}");
    locations=cJSON_GetObjectItem(root,"locations");
    for(unsigned i=0;i<WEATHER_SEARCH_BATCH_SIZE;++i)cJSON_AddItemToArray(locations,cJSON_Parse(WEATHER_TEST_CITY));
    json=cJSON_PrintUnformatted(root);
    assert(weather_locations_parse(json,strlen(json),batch,20,&count,&total,&page)&&count==20&&total==41&&!page);
    assert(!weather_locations_parse(json,strlen(json),results,8,&count,&total,&page)&&!count);
    assert(!weather_locations_parse(json,strlen(json),batch,0,&count,&total,&page)&&!count);
    assert(!weather_locations_parse(json,strlen(json),batch,21,&count,&total,&page)&&!count);
    free(json);cJSON_AddItemToArray(locations,cJSON_Parse(WEATHER_TEST_CITY));json=cJSON_PrintUnformatted(root);
    assert(!weather_locations_parse(json,strlen(json),batch,20,&count,&total,&page)&&!count);
    free(json);cJSON_DeleteItemFromArray(locations,20);
    cJSON_SetNumberValue(cJSON_GetObjectItem(root,"total"),4001);json=cJSON_PrintUnformatted(root);
    assert(!weather_locations_parse(json,strlen(json),batch,20,&count,&total,&page)&&!count);
    free(json);cJSON_SetNumberValue(cJSON_GetObjectItem(root,"total"),4000);
    cJSON_SetNumberValue(cJSON_GetObjectItem(root,"page"),199);json=cJSON_PrintUnformatted(root);
    assert(weather_locations_parse(json,strlen(json),batch,20,&count,&total,&page)&&count==20&&total==4000&&page==199);
    free(json);cJSON_SetNumberValue(cJSON_GetObjectItem(root,"page"),200);json=cJSON_PrintUnformatted(root);
    assert(!weather_locations_parse(json,strlen(json),batch,20,&count,&total,&page)&&!count);
    free(json);cJSON_Delete(root);
    weather_location_t wrong=city;strcpy(wrong.id,"101210113");
    assert(!weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),
        weather_timestamp("2026-11-02T00:30:00Z"),&wrong,&f));
    strcpy(wrong.id,"bad");assert(!weather_location_valid(&wrong));
    const char *invalid_ids[]={"", "abcD", "F4F1?x=1", "../F4F1", "123?", "F4 F1", "F4_F1", "F4F1%20"};
    for(unsigned i=0;i<sizeof(invalid_ids)/sizeof(*invalid_ids);++i) {
        wrong=city;snprintf(wrong.id,sizeof(wrong.id),"%s",invalid_ids[i]);
        assert(!weather_location_valid(&wrong));
    }
    wrong=city;memset(wrong.id,'A',sizeof(wrong.id));assert(!weather_location_valid(&wrong));
    wrong=city;strcpy(wrong.id,"CA3");assert(weather_location_valid(&wrong));
    strcpy(wrong.id,"0123456789ABCDEF");assert(weather_location_valid(&wrong));
    weather_location_t alpha;
    assert(weather_location_parse(WEATHER_TEST_ALPHA_CITY,strlen(WEATHER_TEST_ALPHA_CITY),&alpha));
    assert(!strcmp(alpha.id,"F4F1")&&!strcmp(alpha.timezone,"Asia/Taipei"));
    root=cJSON_Parse(WEATHER_TEST_FORECAST);
    cJSON_ReplaceItemInObject(root,"location",cJSON_Parse(WEATHER_TEST_ALPHA_CITY));
    json=cJSON_PrintUnformatted(root);
    assert(weather_forecast_parse(json,strlen(json),weather_timestamp("2026-11-02T00:30:00Z"),&alpha,&f));
    assert(!strcmp(f.location.id,"F4F1"));free(json);cJSON_Delete(root);
    wrong=city;wrong.lat=9.55;assert(weather_location_valid(&wrong));
    strcpy(wrong.timezone,"Asia/Taipei");assert(weather_location_valid(&wrong));
    wrong=city;strcpy(wrong.timezone,"Asia/Taipei");
    assert(!weather_forecast_parse(WEATHER_TEST_FORECAST,strlen(WEATHER_TEST_FORECAST),
        weather_timestamp("2026-11-02T00:30:00Z"),&wrong,&f));
    char nested[100];memset(nested,'[',40);memset(nested+40,']',40);nested[80]=0;
    weather_error_code(nested,80,text,sizeof(text));assert(!text[0]);
    f.stale=255;assert(!weather_forecast_valid(&f,now));
    city.timezone[0]=0;assert(!weather_location_valid(&city));
    puts("PASS: weather strict protocol, dates, solar UTC conversion, ranges, stale/expiry, unknown scenes, UTF-8 query");
}
