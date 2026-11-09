/*
 * 中文：把持久设置转换成当前同步刷新所需的只读属性，不缓存页面状态。
 * English: Convert persistent settings into read-only synchronous-refresh attributes without caching page state.
 */
#include "display_refresh_policy.h"
#include "settings.h"

display_refresh_context_t display_refresh_context(void) {
    return (display_refresh_context_t){
        .main_ordinary=app_settings_main_refresh_mode()==APP_MAIN_REFRESH_NORMAL,
        .main_fast=app_settings_main_fast_refresh(),
        .reader_background=app_settings_book_background_path()[0]!=0,
    };
}
