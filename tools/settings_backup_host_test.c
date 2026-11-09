/* SPDX-License-Identifier: Apache-2.0 */
/* Exercise the firmware backup format and validation against a temporary card. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __APPLE__
/* Some host libc versions predate strlcpy, which ESP-IDF provides. */
size_t strlcpy(char *dst, const char *src, size_t cap) {
    size_t length = strlen(src);
    if (cap) {
        size_t copied = length < cap - 1 ? length : cap - 1;
        memcpy(dst, src, copied);
        dst[copied] = 0;
    }
    return length;
}
#endif

#define APP_SETTINGS_BACKUP_ROOT "build/book-tests/settings-card"
/* v15..v33 追加的尾部块；旧版本夹具要一并去掉。
   Tail blocks appended by v15..v33; legacy fixtures drop them all. */
#define LEGACY_TAIL_BLOCKS (sizeof(settings_backup_background_t) + sizeof(settings_backup_background_t) + \
                            sizeof(settings_backup_home_t) + sizeof(settings_backup_open_full_t) + \
                            sizeof(settings_backup_title_style_t) + sizeof(settings_backup_title_size_t) + \
                            sizeof(settings_backup_double_t) + sizeof(settings_backup_volume_t) + \
                            sizeof(settings_backup_up_t) + sizeof(settings_backup_book_fonts_t) + \
                            sizeof(settings_backup_book_vertical_t) + sizeof(settings_backup_shutdown_style_t) + \
                            sizeof(settings_backup_haptic_t) + sizeof(settings_backup_margins_t) + \
                            sizeof(settings_backup_shake_t) + sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t))
static unsigned flash_depth;
static bool flash_failure;
bool epd_flash_begin(void) { if (flash_failure) return false; ++flash_depth; return true; }
void epd_flash_end(void) { assert(flash_depth); --flash_depth; }
#define nvs_open backup_nvs_open
#define nvs_close backup_nvs_close
#define nvs_get_u8 backup_nvs_get_u8
#define nvs_set_u8 backup_nvs_set_u8
#define nvs_commit backup_nvs_commit
#include "../main/settings/settings.c"
#include "../main/settings/home_components.c"
#undef nvs_open
#undef nvs_close
#undef nvs_get_u8
#undef nvs_set_u8
#undef nvs_commit

static app_weather_view_t weather_view;
void app_weather_init(void) {}
const app_weather_view_t *app_weather_view(void) { return &weather_view; }

static wifi_transfer_wifi_backup_t saved_wifi = {
    .configured = 1, .ssid = "Home_2.4G", .password = "password123"
};
static unsigned wifi_imports;
esp_err_t wifi_transfer_export_wifi_backup(wifi_transfer_wifi_backup_t *out) {
    *out = saved_wifi;
    return ESP_OK;
}
bool wifi_transfer_wifi_backup_valid(const wifi_transfer_wifi_backup_t *backup) {
    if (!backup || backup->configured > 1) return false;
    if (!backup->configured) return !backup->ssid[0] && !backup->password[0];
    return backup->ssid[0] &&
           strnlen(backup->ssid, sizeof(backup->ssid)) < sizeof(backup->ssid) &&
           strnlen(backup->password, sizeof(backup->password)) < sizeof(backup->password);
}
esp_err_t wifi_transfer_import_wifi_backup(const wifi_transfer_wifi_backup_t *backup) {
    if (!wifi_transfer_wifi_backup_valid(backup)) return ESP_ERR_INVALID_ARG;
    saved_wifi = *backup;
    ++wifi_imports;
    return ESP_OK;
}

// 设置层只把开关/强度同步给驱动；宿主不链接马达实现。
// / The settings layer only syncs switch/intensity to the driver; the host does not link the motor.
void haptics_set_enabled(bool on) { (void)on; }
void haptics_set_intensity(uint8_t intensity) { (void)intensity; }

static unsigned history_saves, history_restores;
esp_err_t book_history_backup_write(FILE *file) {
    ++history_saves;
    return fwrite("RPHIST1", 1, 8, file) == 8 ? ESP_OK : ESP_FAIL;
}
bool book_history_backup_validate(FILE *file) {
    char marker[8];
    return fread(marker, 1, 8, file) == 8 && !memcmp(marker, "RPHIST1", 8) && fgetc(file) == EOF;
}
esp_err_t book_history_backup_restore(FILE *file) {
    ++history_restores;
    return book_history_backup_validate(file) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static bool card_mounted = true;
static bool commit_fails;
static int commit_count;
static const char *loaded_device_name;
static char written_device_name[64];
static uint8_t test_loaded_system_size;
static uint8_t test_loaded_fast;
static uint8_t loaded_main_mode;
static bool has_main_mode;
static uint8_t loaded_hold=APP_READER_KEY_REFRESH;
static bool has_hold;
static uint8_t loaded_double=APP_READER_KEY_NONE;
static bool has_double;
static bool keys_v2;
static uint8_t loaded_weight=2;
static bool has_weight;
static uint8_t loaded_open_full=1;
static bool has_open_full;
static uint8_t loaded_fine=32;static bool has_fine;
static uint8_t loaded_aa=1;
static bool has_aa;
static uint8_t loaded_adjust=20;
static bool has_adjust;
static uint8_t loaded_keys[3];
static bool has_keys[3];
static int loaded_shelf=-1, loaded_lock=-1,loaded_home=-1;
static bool loaded_shelf_v22;
static uint8_t loaded_idle;static bool has_idle;
static uint8_t loaded_shutdown;static bool has_shutdown;
static int loaded_chrome_heights[2]={-1,-1};
static int loaded_margin=-1,loaded_margin_l=-1,loaded_margin_r=-1,loaded_margin_t=-1,loaded_margin_b=-1;
esp_err_t board_sd_get_info(board_sd_info_t *info) { info->mounted = card_mounted; return ESP_OK; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { (void)ns; if (mode == NVS_READWRITE) assert(flash_depth); *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
static bool has_tracking,has_para;static uint8_t loaded_tracking,loaded_para;
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) { for(unsigned i=0;i<2;++i)if(!strcmp(key,i?NVS_KEY_CHROME_FOOTER_H:NVS_KEY_CHROME_HEADER_H)&&loaded_chrome_heights[i]>=0){*value=loaded_chrome_heights[i];return ESP_OK;} (void)h; if(!strcmp(key,NVS_KEY_SHUTDOWN_MODE)&&has_shutdown){*value=loaded_shutdown;return ESP_OK;} if(!strcmp(key,NVS_KEY_AUTO_LOCK)&&has_idle){*value=loaded_idle;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_PARA)&&has_para){*value=loaded_para;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_TRACK)&&has_tracking){*value=loaded_tracking;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_WEIGHT_FINE)&&has_fine){*value=loaded_fine;return ESP_OK;} if(!strcmp(key,NVS_KEY_OPEN_FULL)&&has_open_full){*value=loaded_open_full;return ESP_OK;} if(!strcmp(key,NVS_KEY_HOME_COMPONENT)&&loaded_home>=0){*value=(uint8_t)loaded_home;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_AA)&&has_aa){*value=loaded_aa;return ESP_OK;} if(!strcmp(key,NVS_KEY_READER_KEYS_V2)&&keys_v2){*value=1;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_WEIGHT)&&has_weight){*value=loaded_weight;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_INDENT_ADJUST)&&has_adjust){*value=loaded_adjust;return ESP_OK;} if(!strcmp(key,NVS_KEY_SHELF_STYLE)&&loaded_shelf>=0){*value=(uint8_t)loaded_shelf;return ESP_OK;} if(!strcmp(key,NVS_KEY_SHELF_V22)&&loaded_shelf_v22){*value=1;return ESP_OK;} if(!strcmp(key,NVS_KEY_LOCK_STYLE)&&loaded_lock>=0){*value=(uint8_t)loaded_lock;return ESP_OK;} if(!strcmp(key,NVS_KEY_HOLD_ACTION)&&has_hold){*value=loaded_hold;return ESP_OK;} if(!strcmp(key,NVS_KEY_DOUBLE_ACTION)&&has_double){*value=loaded_double;return ESP_OK;} for(unsigned i=0;i<3;++i) if(!strcmp(key,s_reader_key_names[i])&&has_keys[i]){*value=loaded_keys[i];return ESP_OK;} if (!strcmp(key, NVS_KEY_MAIN_REFRESH) && has_main_mode) { *value=loaded_main_mode;return ESP_OK; } if (!strcmp(key, "ui_fast")) { *value = test_loaded_fast; return ESP_OK; } if (!strcmp(key, NVS_KEY_SYS_SIZE) && test_loaded_system_size) { *value = test_loaded_system_size; return ESP_OK; } if(!strcmp(key,NVS_KEY_BOOK_MARGIN)&&loaded_margin>=0){*value=(uint8_t)loaded_margin;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_MARGIN_L)&&loaded_margin_l>=0){*value=(uint8_t)loaded_margin_l;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_MARGIN_R)&&loaded_margin_r>=0){*value=(uint8_t)loaded_margin_r;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_MARGIN_T)&&loaded_margin_t>=0){*value=(uint8_t)loaded_margin_t;return ESP_OK;} if(!strcmp(key,NVS_KEY_BOOK_MARGIN_B)&&loaded_margin_b>=0){*value=(uint8_t)loaded_margin_b;return ESP_OK;} return ESP_FAIL; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) { for(unsigned i=0;i<2;++i)if(!strcmp(key,i?NVS_KEY_CHROME_FOOTER_H:NVS_KEY_CHROME_HEADER_H))loaded_chrome_heights[i]=value; (void)h; if(!strcmp(key,NVS_KEY_SHUTDOWN_MODE)){loaded_shutdown=value;has_shutdown=true;} if(!strcmp(key,NVS_KEY_AUTO_LOCK)){loaded_idle=value;has_idle=true;} if(!strcmp(key,NVS_KEY_BOOK_PARA)){loaded_para=value;has_para=true;} if(!strcmp(key,NVS_KEY_BOOK_TRACK)){loaded_tracking=value;has_tracking=true;} if(!strcmp(key,NVS_KEY_BOOK_WEIGHT_FINE)){loaded_fine=value;has_fine=true;} if(!strcmp(key,NVS_KEY_OPEN_FULL)){loaded_open_full=value;has_open_full=true;} if(!strcmp(key,NVS_KEY_BOOK_AA)){loaded_aa=value;has_aa=true;} if(!strcmp(key,NVS_KEY_READER_KEYS_V2))keys_v2=value==1; if(!strcmp(key,NVS_KEY_BOOK_WEIGHT)){loaded_weight=value;has_weight=true;} if(!strcmp(key,NVS_KEY_BOOK_INDENT_ADJUST)){loaded_adjust=value;has_adjust=true;} if(!strcmp(key,NVS_KEY_SHELF_STYLE))loaded_shelf=value; if(!strcmp(key,NVS_KEY_SHELF_V22))loaded_shelf_v22=value==1; if(!strcmp(key,NVS_KEY_LOCK_STYLE))loaded_lock=value; if(!strcmp(key,NVS_KEY_HOLD_ACTION)){loaded_hold=value;has_hold=true;} if(!strcmp(key,NVS_KEY_DOUBLE_ACTION)){loaded_double=value;has_double=true;} for(unsigned i=0;i<3;++i) if(!strcmp(key,s_reader_key_names[i])){loaded_keys[i]=value;has_keys[i]=true;} if (!strcmp(key, NVS_KEY_MAIN_REFRESH)) {has_main_mode=true;loaded_main_mode=value;} if (!strcmp(key, "ui_fast")) test_loaded_fast=value; return ESP_OK; }
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size) {
 (void)h;
 if(!strcmp(key,NVS_KEY_DEVICE_NAME)&&loaded_device_name) {
  size_t bytes=strlen(loaded_device_name)+1;
  if(*size<bytes)return ESP_FAIL;
  memcpy(value,loaded_device_name,bytes);*size=bytes;return ESP_OK;
 }
 if(!strcmp(key,NVS_KEY_BOOKS_DIR)||!strcmp(key,NVS_KEY_FONTS_DIR)) {
  const char *legacy="/sdcard/legacy/custom";size_t bytes=strlen(legacy)+1;
  if(*size<bytes)return ESP_FAIL;
  memcpy(value,legacy,bytes);*size=bytes;return ESP_OK;
 }
 return ESP_FAIL;
}esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) {
 (void)h;
 if(!strcmp(key,NVS_KEY_DEVICE_NAME))strlcpy(written_device_name,value,sizeof(written_device_name));
 return ESP_OK;
}
/* rc92 引入的时间同步键走 u32；桩按缺省处理即可（恢复时回退 0）。/ rc92's time-sync keys use u32;
   defaulting stubs are enough (restore falls back to zero). */
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value) { (void)h; (void)key; (void)value; return ESP_FAIL; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) { (void)h; (void)key; (void)value; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; (void)key; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { assert(flash_depth); (void)h; ++commit_count; return commit_fails ? ESP_FAIL : ESP_OK; }

// 新布局桩模拟提交边界，旧设置桩的行为不变。
// Model the new layout's commit boundary without changing existing settings stubs.
static int durable_regions = -1, pending_regions = -1;
static bool regions_write_fails;
esp_err_t backup_nvs_open(const char *ns, int mode, nvs_handle_t *h) {
    assert(!strcmp(ns, "read_pico"));
    pending_regions = -1;
    return nvs_open(ns, mode, h);
}
void backup_nvs_close(nvs_handle_t h) { pending_regions = -1; nvs_close(h); }
esp_err_t backup_nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value) {
    if (strcmp(key, "home_regions")) return nvs_get_u8(h, key, value);
    if (durable_regions < 0) return ESP_FAIL;
    *value = (uint8_t)durable_regions;
    return ESP_OK;
}
esp_err_t backup_nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value) {
    if (strcmp(key, "home_regions")) return nvs_set_u8(h, key, value);
    if (regions_write_fails) return ESP_FAIL;
    pending_regions = value;
    return ESP_OK;
}
esp_err_t backup_nvs_commit(nvs_handle_t h) {
    esp_err_t err = nvs_commit(h);
    if (err == ESP_OK && pending_regions >= 0) durable_regions = pending_regions;
    return err;
}

// 去掉 v24/v25/v26 才有的本地扩展块，把文件还原成上游 v20 及更早的尾部布局。
// Drop the v24/v25/v26 local extension blocks to leave an upstream v20-or-earlier tail layout.
static void strip_local_extension_blocks(FILE *f) {
    const long local_at = (long)(sizeof(settings_backup_v1_t) + 7 +
        sizeof(settings_backup_profile_t) + sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) +
        sizeof(settings_backup_indent_adjust_t) + sizeof(settings_backup_weight_t) + sizeof(settings_backup_wifi_t) +
        sizeof(settings_backup_background_t) + sizeof(settings_backup_home_t) + sizeof(settings_backup_background_t));
    const long local_band = (long)(sizeof(settings_backup_open_full_t) + sizeof(settings_backup_title_style_t) +
                                   sizeof(settings_backup_title_size_t) + sizeof(settings_backup_double_t) +
                                   sizeof(settings_backup_volume_t) + sizeof(settings_backup_up_t) +
                                   sizeof(settings_backup_book_fonts_t) + sizeof(settings_backup_book_vertical_t) +
                                   sizeof(settings_backup_shutdown_style_t) + sizeof(settings_backup_haptic_t) +
                                   sizeof(settings_backup_margins_t) + sizeof(settings_backup_shake_t) +
                                  sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t));
    assert(fseek(f, 0, SEEK_END) == 0);
    const long end = ftell(f);
    assert(end >= local_at + local_band);
    const size_t tail = (size_t)(end - local_at - local_band);
    uint8_t *bytes = malloc(tail ? tail : 1);
    assert(bytes);
    assert(fseek(f, local_at + local_band, SEEK_SET) == 0 && fread(bytes, 1, tail, f) == tail);
    assert(fseek(f, local_at, SEEK_SET) == 0 && fwrite(bytes, 1, tail, f) == tail);
    free(bytes);
    assert(fflush(f) == 0 && ftruncate(fileno(f), end - local_band) == 0);
}

static void test_home_regions_backup(void) {
    // 导出先迁移旧开关；不要求首页已被打开过。
    // Export migrates the legacy switch without requiring a prior Home visit.
    s_loaded = false;
    durable_regions = -1;
    weather_view.enabled = true;
    assert(app_settings_backup_save() == ESP_OK);
    assert(app_home_components_value() == 0x14);
    unsigned valid_count = 0;
    for (unsigned value = 0; value < 256; ++value) {
        if (!app_home_components_value_valid((uint8_t)value)) continue;
        ++valid_count;
        app_home_components_apply((uint8_t)value);
        assert(app_settings_backup_save() == ESP_OK);
        app_home_components_apply(0x1a);
        durable_regions = 0x1a;
        int commits = commit_count;
        assert(app_settings_backup_restore() == ESP_OK);
        assert(commit_count == commits + 1 && durable_regions == (int)value);
        assert(app_home_components_value() == value);
        s_loaded = false;
        app_home_components_init(false);
        assert(app_home_components_value() == value);
    }
    assert(valid_count == 13);

    app_home_components_apply(0x11);
    assert(app_settings_backup_save() == ESP_OK);
    FILE *file = fopen(BACKUP_FILE, "rb");
    assert(file && fseek(file, -(long)(8 + sizeof(settings_backup_home_regions_t)), SEEK_END) == 0);
    long regions_at = ftell(file);
    settings_backup_home_regions_t valid;
    assert(fread(&valid, 1, sizeof(valid), file) == sizeof(valid) && fclose(file) == 0);
    assert(valid.value == 0x11 && backup_home_regions_valid(&valid));
    app_home_components_apply(0x1a);
    durable_regions = 0x1a;
    // 校验和正确也不能接受未知版本、越界枚举或重复实际组件。
    // A sealed checksum must not admit unknown schemas, invalid enums or duplicate real widgets.
    for (unsigned value = 0; value < 256; ++value) {
        if (app_home_components_value_valid((uint8_t)value)) continue;
        settings_backup_home_regions_t bad = {.value = (uint8_t)value};
        uint32_t hash = backup_home_regions_checksum(&bad);
        for (unsigned i = 0; i < 4; ++i) bad.checksum[i] = (uint8_t)(hash >> (8 * i));
        file = fopen(BACKUP_FILE, "r+b");
        assert(file && fseek(file, regions_at, SEEK_SET) == 0);
        assert(fwrite(&bad, 1, sizeof(bad), file) == sizeof(bad) && fclose(file) == 0);
        int commits = commit_count;
        unsigned imports = wifi_imports, history = history_restores;
        assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
        assert(commit_count == commits && wifi_imports == imports && history_restores == history);
        assert(durable_regions == 0x1a && app_home_components_value() == 0x1a);
    }
    settings_backup_home_regions_t bad = valid;
    bad.checksum[0] ^= 1;
    file = fopen(BACKUP_FILE, "r+b");
    assert(file && fseek(file, regions_at, SEEK_SET) == 0);
    assert(fwrite(&bad, 1, sizeof(bad), file) == sizeof(bad) && fclose(file) == 0);
    int commits = commit_count;
    unsigned imports = wifi_imports, history = history_restores;
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(commit_count == commits && wifi_imports == imports && history_restores == history);
    assert(durable_regions == 0x1a && app_home_components_value() == 0x1a);

    for (unsigned cut = 0; cut < sizeof(valid); ++cut) {
        app_home_components_apply(0x11);
        assert(app_settings_backup_save() == ESP_OK);
        file = fopen(BACKUP_FILE, "r+b");
        assert(file && ftruncate(fileno(file), regions_at + cut) == 0 && fclose(file) == 0);
        app_home_components_apply(0x1a);
        commits = commit_count;
        imports = wifi_imports; history = history_restores;
        assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
        assert(commit_count == commits && wifi_imports == imports && history_restores == history);
        assert(durable_regions == 0x1a && app_home_components_value() == 0x1a);
    }

    app_home_components_apply(0x11);
    assert(app_settings_backup_save() == ESP_OK);
    app_home_components_apply(0x1a);
    for (unsigned failure = 0; failure < 3; ++failure) {
        regions_write_fails = failure == 0;
        commit_fails = failure == 1;
        flash_failure = failure == 2;
        commits = commit_count;
        imports = wifi_imports; history = history_restores;
        assert(app_settings_backup_restore() != ESP_OK);
        assert(commit_count == commits + (failure == 1) && wifi_imports == imports && history_restores == history);
        assert(durable_regions == 0x1a && app_home_components_value() == 0x1a && !flash_depth);
    }
    regions_write_fails = commit_fails = flash_failure = false;
    commits = commit_count;
    assert(app_settings_backup_restore() == ESP_OK);
    assert(commit_count == commits + 1 && durable_regions == 0x11 && app_home_components_value() == 0x11);

    // v32 只移除新块并重封旧校验；恢复不触碰当前布局及对应持久值。
    // Strip only the new block and reseal v32 checksums; restore preserves the live and durable layout.
    file = fopen(BACKUP_FILE, "r+b");
    settings_backup_v1_t header;
    uint8_t extension[7];
    settings_backup_profile_t profile;
    assert(file && fread(&header, 1, sizeof(header), file) == sizeof(header));
    assert(fread(extension, 1, sizeof(extension), file) == sizeof(extension));
    assert(fread(&profile, 1, sizeof(profile), file) == sizeof(profile));
    memcpy(header.magic, "PICOSETW", 8);
    backup_seal(&header);
    uint32_t hash = backup_shutdown_checksum(&header, extension[0], extension[1], extension[2]);
    for (unsigned i = 0; i < 4; ++i) extension[i + 3] = (uint8_t)(hash >> (8 * i));
    hash = backup_profile_checksum(&header, extension[0], extension[1], extension[2], &profile);
    for (unsigned i = 0; i < 4; ++i) profile.checksum[i] = (uint8_t)(hash >> (8 * i));
    rewind(file);
    assert(fwrite(&header, 1, sizeof(header), file) == sizeof(header));
    assert(fwrite(extension, 1, sizeof(extension), file) == sizeof(extension));
    assert(fwrite(&profile, 1, sizeof(profile), file) == sizeof(profile));
    assert(fseek(file, regions_at, SEEK_SET) == 0 && fwrite("RPHIST1", 1, 8, file) == 8);
    assert(fflush(file) == 0 && ftruncate(fileno(file), regions_at + 8) == 0 && fclose(file) == 0);
    app_home_components_apply(0x19);
    durable_regions = 0x19;
    commits = commit_count;
    assert(app_settings_backup_restore() == ESP_OK && commit_count == commits + 1);
    assert(durable_regions == 0x19 && app_home_components_value() == 0x19);
    assert(!flash_depth);
}

static void test_device_name_branding(void) {
    const struct { const char *stored; const char *expected; } cases[] = {
        {NULL, "kimiko"}, {"", "kimiko"}, {"Pico", "kimiko"}, {"kiikoread", "kimiko"},
        {"kimiko", "kimiko"}, {"My Reader", "My Reader"}, {"kiikoread-custom", "kiikoread-custom"}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        loaded_device_name = cases[i].stored;
        app_settings_init();
        assert(!strcmp(app_settings_device_name(), cases[i].expected));
        if (!cases[i].stored || !cases[i].stored[0]) continue;
        loaded_device_name = NULL;
        strlcpy(s_device_name, cases[i].stored, sizeof(s_device_name));
        assert(app_settings_backup_save() == ESP_OK);
        strlcpy(s_device_name, "Keep current", sizeof(s_device_name));
        commit_fails = true;
        assert(app_settings_backup_restore() == ESP_FAIL);
        assert(!strcmp(app_settings_device_name(), "Keep current"));
        commit_fails = false;
        assert(app_settings_backup_restore() == ESP_OK);
        assert(!strcmp(app_settings_device_name(), cases[i].expected));
        assert(!strcmp(written_device_name, cases[i].expected));
    }
    loaded_device_name = NULL;
    app_settings_init();
    assert(!strcmp(app_settings_device_name(), "kimiko"));
}

int main(void) {
    assert(!strcmp(app_settings_device_name(), "kimiko"));
    flash_failure = true;
    nvs_handle_t rejected = 0;
    assert(settings_write_open(&rejected) == ESP_ERR_INVALID_STATE && !flash_depth && !rejected);
    flash_failure = false;

    loaded_home=3;app_settings_init();assert(app_settings_home_component()==APP_HOME_CLOCK);
    loaded_home=APP_HOME_MOON;app_settings_init();assert(app_settings_home_component()==APP_HOME_MOON);
    loaded_home=-1;keys_v2=false;

    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    app_settings_set_book_margin_left(0);assert(app_settings_book_margin_left()==0);
    app_settings_set_book_margin_left(24);assert(app_settings_book_margin_left()==24);
    app_settings_set_book_margin_left(96);assert(app_settings_book_margin_left()==96);
    app_settings_set_book_margin_left(97);assert(app_settings_book_margin_left()==96);
    app_settings_set_book_margin_right(12);assert(app_settings_book_margin_right()==12);
    app_settings_set_book_margin_top(18);assert(app_settings_book_margin_top()==18);
    app_settings_set_book_margin_bottom(30);assert(app_settings_book_margin_bottom()==30);
    app_settings_set_book_margin_top(97);assert(app_settings_book_margin_top()==18);
    app_settings_set_book_margin_bottom(0);assert(app_settings_book_margin_bottom()==0);
    // 旧单一边距只在左右新键都缺失时接管；只写过一边时另一边回到默认，不被旧键带跑。
    // The legacy single margin takes over both sides only when neither new key exists; after one
    // side has been written the other returns to its default instead of following the legacy key.
    loaded_margin=36;
    app_settings_init();
    assert(app_settings_book_margin_left()==36&&app_settings_book_margin_right()==36&&
           app_settings_book_margin_top()==0&&app_settings_book_margin_bottom()==0);
    loaded_margin=48;loaded_margin_l=96;
    app_settings_init();
    assert(app_settings_book_margin_left()==96&&app_settings_book_margin_right()==24);
    loaded_margin_l=-1;loaded_margin_r=12;
    app_settings_init();
    assert(app_settings_book_margin_left()==24&&app_settings_book_margin_right()==12);
    loaded_margin_l=6;loaded_margin_r=12;loaded_margin_t=96;loaded_margin_b=30;
    app_settings_init();
    assert(app_settings_book_margin_left()==6&&app_settings_book_margin_right()==12&&
           app_settings_book_margin_top()==96&&app_settings_book_margin_bottom()==30);
    loaded_margin=loaded_margin_l=loaded_margin_r=loaded_margin_t=loaded_margin_b=-1;
    app_settings_init();
    assert(app_settings_book_margin_left()==24&&app_settings_book_margin_right()==24&&
           app_settings_book_margin_top()==0&&app_settings_book_margin_bottom()==0);
    // 上面的初始化会提交按键迁移标记；恢复测试夹具的初始状态。
    // The initializations above commit the key-migration marker; restore the fixture state.
    keys_v2=false;
    assert(app_settings_set_book_background_path("/sdcard/pictures/bg.png"));
    assert(!app_settings_set_book_background_path("/sdcard/pictures/../bad.png"));
    has_hold=true;loaded_hold=APP_READER_KEY_REFRESH;
    has_keys[0]=has_keys[1]=has_keys[2]=true;
    loaded_keys[0]=APP_READER_KEY_PREV;loaded_keys[1]=APP_READER_KEY_TOOLS;loaded_keys[2]=APP_READER_KEY_NEXT;
    app_settings_init();assert(keys_v2&&app_settings_reader_hold_action()==APP_READER_KEY_HOME&&loaded_hold==APP_READER_KEY_HOME);
    // A later explicit refresh mapping survives subsequent starts.
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_REFRESH));
    app_settings_init();assert(app_settings_reader_hold_action()==APP_READER_KEY_REFRESH);
    has_hold=false;
    app_settings_init();assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_book_weight()==0);
    assert(app_settings_set_book_weight(2)&&loaded_weight==4);
    s_book_weight=2;app_settings_init();assert(app_settings_book_weight()==2);
    assert(!app_settings_set_book_weight(3));
    commit_fails=true;assert(!app_settings_set_book_weight(-1)&&app_settings_book_weight()==2);commit_fails=false;
    assert(app_settings_set_book_weight(-1));
    loaded_weight=255;has_fine=false;app_settings_init();assert(app_settings_book_weight()==0);
    /* Missing rd_open_full keeps the historical full-screen default. */
    has_open_full=false;app_settings_init();assert(app_settings_reader_open_fullscreen());
    loaded_open_full=0;has_open_full=true;app_settings_init();assert(!app_settings_reader_open_fullscreen());
    loaded_open_full=2;app_settings_init();assert(app_settings_reader_open_fullscreen()); /* invalid value falls back */
    has_open_full=false;app_settings_init();assert(app_settings_reader_open_fullscreen());
    assert(app_settings_set_book_weight_fine(1)&&app_settings_book_weight_fine()==1&&loaded_fine==33);
    app_settings_init();assert(app_settings_book_weight_fine()==1);
    assert(!app_settings_set_book_weight_fine(33)&&app_settings_book_weight_fine()==1);
    commit_fails=true;assert(!app_settings_set_book_weight_fine(2)&&app_settings_book_weight_fine()==1);commit_fails=false;
    has_fine=false;loaded_weight=4;app_settings_init();assert(app_settings_book_weight_fine()==32);
    assert(app_settings_set_book_weight(1));
    assert(app_settings_book_antialias());
    assert(app_settings_set_book_antialias(false)&&loaded_aa==0);
    s_book_antialias=true;app_settings_init();assert(!app_settings_book_antialias());
    commit_fails=true;assert(!app_settings_set_book_antialias(true)&&!app_settings_book_antialias());commit_fails=false;
    assert(app_settings_set_book_antialias(true));
    loaded_aa=255;app_settings_init();assert(app_settings_book_antialias());
    assert(app_settings_set_book_antialias(false));
    app_settings_set_reader_key_preset(1);assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_system_contrast() == 100);
    assert(app_settings_book_line_spacing() == 130); /* New installations start at the middle slider stop. */
    // 默认 5 分钟；存过 0（关闭）必须保留，不能被默认值覆盖。
    // The default is five minutes; a stored 0 (off) must survive the default.
    has_idle=false;app_settings_init();assert(app_settings_auto_lock_minutes()==5);
    loaded_idle=0;has_idle=true;app_settings_init();assert(app_settings_auto_lock_minutes()==0);
    loaded_idle=1;app_settings_init();assert(app_settings_auto_lock_minutes()==1);
    has_idle=false;
    // 关机样式默认仅浅睡。/ Shutdown style defaults to light-only.
    assert(app_settings_shutdown_style()==APP_SHUTDOWN_LIGHT);
    assert(app_settings_touch_haptics() && app_settings_haptic_intensity()==APP_HAPTIC_WEAK);

    assert(app_settings_book_indent_adjust()==0);
    app_settings_set_book_indent_adjust(-20);assert(loaded_adjust==0);
    s_book_indent_adjust=20;app_settings_init();assert(app_settings_book_indent_adjust()==-20);
    app_settings_set_book_indent_adjust(20);app_settings_set_book_indent_adjust(21);assert(loaded_adjust==41);
    app_settings_set_book_indent_adjust(-100);assert(loaded_adjust==200&&app_settings_book_indent_adjust()==-100);
    app_settings_set_book_indent_adjust(100);assert(loaded_adjust==120&&app_settings_book_indent_adjust()==100);
    loaded_adjust=255;app_settings_init();assert(app_settings_book_indent_adjust()==0);
    app_settings_set_book_indent_adjust(-7);
    has_keys[0]=has_keys[1]=has_keys[2]=true;
    loaded_keys[0]=APP_READER_KEY_PREV;loaded_keys[1]=APP_READER_KEY_HOME;loaded_keys[2]=APP_READER_KEY_NONE;
    app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    loaded_hold=APP_READER_KEY_TOOLS;has_hold=true;
    app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME && app_settings_reader_hold_action()==APP_READER_KEY_TOOLS);
    loaded_double=APP_READER_KEY_REFRESH;has_double=true;
    app_settings_init();assert(app_settings_reader_double_action()==APP_READER_KEY_REFRESH);
    loaded_double=APP_READER_KEY_COUNT;
    app_settings_init();assert(app_settings_reader_double_action()==APP_READER_KEY_NONE);
    loaded_hold=APP_READER_KEY_REFRESH;app_settings_init();assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    test_loaded_fast = 1;
    app_settings_init();
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_FAST);
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER && !app_settings_main_fast_refresh());
    loaded_main_mode=1;has_main_mode=true;s_main_refresh=APP_MAIN_REFRESH_NORMAL;
    app_settings_init();assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    loaded_main_mode=3;app_settings_init();assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_WATER);
    s_main_refresh=APP_MAIN_REFRESH_NORMAL;app_settings_init();assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    assert(!app_settings_main_fast_refresh());
    (void)mkdir(APP_SETTINGS_BACKUP_ROOT, 0700);
    (void)remove(BACKUP_FILE);
    (void)remove(BACKUP_PREVIOUS);
    (void)remove(BACKUP_TEMP);
    card_mounted = false;
    assert(app_settings_backup_save() == ESP_ERR_INVALID_STATE);
    card_mounted = true;
    app_settings_set_reader_key_preset(2);
    memset(s_reader_keys,0,sizeof(s_reader_keys));app_settings_init();
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME && app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN && app_settings_reader_key_action(2)==APP_READER_KEY_TOOLS);
    app_settings_set_reader_key_preset(99);assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME);
    app_settings_set_reader_key_action(0,APP_READER_KEY_COUNT);assert(app_settings_reader_key_action(0)==APP_READER_KEY_HOME);
    int guard_commits=commit_count;
    assert(app_settings_reader_key_action_allowed(2,APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    assert(commit_count==guard_commits+1&&app_settings_reader_key_action(2)==APP_READER_KEY_REFRESH);
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_HOME));
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(app_settings_set_reader_key_action(0,APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_REFRESH));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_REFRESH));
    commit_fails=true;assert(!app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN);commit_fails=false;
    loaded_keys[1]=APP_READER_KEY_FULLSCREEN;

    assert(app_settings_reader_hold_action()==APP_READER_KEY_REFRESH);
    app_settings_set_reader_hold_action(APP_READER_KEY_HOME);
    s_reader_hold_action=APP_READER_KEY_REFRESH;app_settings_init();
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    s_system_size = 200;
    s_book_px = 62;
    s_book_tracking = 8;
    s_book_indent = 3;
    app_settings_set_book_indent_adjust(-7);
    s_book_rule_offset = 7; /* +6 px */
    s_reader_full_pages = 5;
    s_reader_turn_effect = 1;
    s_reader_power_turn = true;
    s_reader_volume_turn = true;
    s_reader_up_action = APP_READER_KEY_PREV;
    s_reader_statusbar = false; /* bit BK_IMMERSIVE saves 1 = hidden */
    app_settings_set_reader_open_fullscreen(false); /* v18 band remembers windowed entries */
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_MARK); /* v19 band carries the heading style */
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MAX); /* v20 band carries the heading size */
    s_shelf_style = 3;
    s_shutdown_style = APP_SHUTDOWN_STAGED;
    s_auto_lock_minutes = 5;
    s_home_full_refresh = true;
    s_ble_turner = true;
    s_reader_hold_refresh = true;
    app_settings_set_reader_vertical_turn(true);
    assert(app_settings_reader_vertical_turn());

    strlcpy(s_device_name, "Kiiko Pico", sizeof(s_device_name));
    strlcpy(s_status_signature, "今天也要读书", sizeof(s_status_signature));
    strlcpy(s_avatar, "/sdcard/pictures/missing-avatar.jpg", sizeof(s_avatar));
    strlcpy(s_font, "/sdcard/fonts/missing.ttf", sizeof(s_font));
    s_lock_style = 1;
    strlcpy(s_wallpaper, "/sdcard/pictures/missing.jpg", sizeof(s_wallpaper));
    s_touch_haptics = false;
    s_haptic_intensity = APP_HAPTIC_STRONG;
    // v29 尾部保存四向边距；v30 尾部保存晃动翻页。恢复前先改值，确认都能回来。
    // The v29 tail carries all four margins and the v30 tail the shake switch; change them before restoring so each must return.
    app_settings_set_book_margin_left(96);
    app_settings_set_book_margin_right(12);
    app_settings_set_book_margin_top(18);
    app_settings_set_book_margin_bottom(30);
    app_settings_set_reader_shake_turn(true);
    const uint8_t custom_chrome[6]={APP_READER_SLOT_PAGE,APP_READER_SLOT_DATE,APP_READER_SLOT_BATTERY,
        APP_READER_SLOT_TITLE,APP_READER_SLOT_CLOCK,APP_READER_SLOT_PERCENT};
    for(unsigned i=0;i<6;++i)assert(app_settings_set_reader_chrome_item(i/3,i%3,custom_chrome[i]));
    assert(app_settings_set_reader_chrome_heights(144,121));
    for(unsigned h=0;h<256;++h)for(unsigned f=0;f<256;++f){
        bool valid=reader_chrome_heights_valid(h,f);
        if(!valid)assert(!app_settings_set_reader_chrome_heights(h,f));
    }
    commit_fails=true;
    assert(!app_settings_set_reader_chrome_heights(80,57));
    assert(app_settings_reader_chrome_height(0)==144&&app_settings_reader_chrome_height(1)==121);
    assert(!app_settings_set_reader_chrome_default()&&!memcmp(s_reader_chrome,custom_chrome,6));
    assert(!app_settings_set_reader_chrome_item(0,0,APP_READER_SLOT_BLANK)&&s_reader_chrome[0]==custom_chrome[0]);
    commit_fails=false;
    assert(app_settings_backup_save() == ESP_OK);
    assert(history_saves == 1);
    FILE *saved = fopen(BACKUP_FILE, "rb");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7, SEEK_SET) == 0);
    settings_backup_profile_t saved_profile;
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    settings_backup_keys_t saved_keys;
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys) && backup_keys_valid(&saved_keys));
    settings_backup_hold_t saved_hold;
    assert(fread(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold) && backup_hold_valid(&saved_hold));
    settings_backup_indent_adjust_t saved_adjust;
    assert(fread(&saved_adjust,1,sizeof(saved_adjust),saved)==sizeof(saved_adjust)&&backup_indent_adjust_valid(&saved_adjust));
    settings_backup_weight_t saved_weight;
    assert(fread(&saved_weight,1,sizeof(saved_weight),saved)==sizeof(saved_weight)&&backup_weight_valid(&saved_weight));
    assert(saved_weight.encoded==176);
    settings_backup_wifi_t saved_network;
    assert(fread(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    assert(!strcmp(saved_profile.device_name, "Kiiko Pico") &&
           !strcmp(saved_profile.status_signature, "今天也要读书") &&
           !strcmp(saved_profile.avatar, "/sdcard/pictures/missing-avatar.jpg"));
    assert(!(saved_profile.home_full_refresh & 64));
    assert(test_loaded_fast == 1);
    assert(backup_wifi_valid(&saved_network) && saved_network.credentials.configured &&
           !strcmp(saved_network.credentials.ssid, "Home_2.4G") &&
           !strcmp(saved_network.credentials.password, "password123"));
    // 旧备份仍可恢复，但退出的极速测试位被忽略；其他配置与网络继续恢复。
    // Accept old backups while ignoring the retired fast-test bit; restore other settings and WiFi.
    saved = fopen(BACKUP_FILE, "r+b");
    settings_backup_v1_t legacy_header; uint8_t legacy_ext[7];
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    saved_profile.home_full_refresh |= 64;
    uint32_t legacy_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i=0;i<4;++i) saved_profile.checksum[i]=(uint8_t)(legacy_hash>>(8*i));
    assert(fseek(saved, sizeof(legacy_header) + sizeof(legacy_ext), SEEK_SET) == 0);
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fclose(saved) == 0);
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) + sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) + (sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)) +
                            offsetof(settings_backup_wifi_t, credentials.password), SEEK_SET) == 0);
    assert(fputc('X', saved) != EOF && fclose(saved) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(!saved_wifi.configured && wifi_imports == 0);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fseek(saved, sizeof(settings_backup_v1_t) + 7 +
                            sizeof(settings_backup_profile_t) + sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) + (sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)), SEEK_SET) == 0);
    assert(fwrite(&saved_network, 1, sizeof(saved_network), saved) == sizeof(saved_network));
    assert(fclose(saved) == 0);
    // 映射损坏或截断必须在任何设置/网络/阅读资料写入前拒绝。
    // Reject damaged mappings before writing settings, networks or history.
    long adjust_at=sizeof(settings_backup_v1_t)+7+sizeof(settings_backup_profile_t)+sizeof(settings_backup_keys_t)+sizeof(settings_backup_hold_t);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,adjust_at,SEEK_SET)==0);
    assert(fputc(41,saved)!=EOF&&fclose(saved)==0);
    int before_bad_adjust=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==before_bad_adjust&&wifi_imports==0&&history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,adjust_at,SEEK_SET)==0);
    assert(fwrite(&saved_adjust,1,sizeof(saved_adjust),saved)==sizeof(saved_adjust)&&fclose(saved)==0);
    long weight_at=adjust_at+(long)sizeof(saved_adjust);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,weight_at,SEEK_SET)==0);
    assert(fputc(5,saved)!=EOF&&fclose(saved)==0);
    int before_bad_weight=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==before_bad_weight&&wifi_imports==0&&history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,weight_at,SEEK_SET)==0);
    assert(fwrite(&saved_weight,1,sizeof(saved_weight),saved)==sizeof(saved_weight)&&fclose(saved)==0);
    long keys_at=sizeof(settings_backup_v1_t)+7+sizeof(settings_backup_profile_t);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at,SEEK_SET)==0);
    assert(fputc(APP_READER_KEY_COUNT,saved)!=EOF&&fclose(saved)==0);
    int rejected_commits=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==rejected_commits && wifi_imports==0 && history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at,SEEK_SET)==0);
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys)&&fclose(saved)==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at+(long)sizeof(saved_keys),SEEK_SET)==0);
    assert(fputc(APP_READER_KEY_COUNT,saved)!=EOF&&fclose(saved)==0);
    rejected_commits=commit_count;assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==rejected_commits && wifi_imports==0 && history_restores==0);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,keys_at+(long)sizeof(saved_keys),SEEK_SET)==0);
    assert(fwrite(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold)&&fclose(saved)==0);
    s_reader_hold_action=APP_READER_KEY_REFRESH;
    s_book_weight=2;
    s_system_size = 120;
    s_book_px = 48;
    s_book_tracking = 2;
    s_book_indent = 0;
    s_book_indent_adjust = 40;
    s_book_rule_offset = 4;
    s_reader_full_pages = 15;
    s_reader_turn_effect = 0;
    s_reader_power_turn = false;
    s_reader_volume_turn = false;
    s_reader_shake_turn = false;
    memset(s_reader_chrome,0,sizeof(s_reader_chrome));
    s_reader_chrome_heights[0]=80;s_reader_chrome_heights[1]=57;
    s_reader_up_action = APP_READER_KEY_NONE;
    s_reader_statusbar = true;
    s_reader_open_fullscreen = true; /* dirty value must come back from the backup */
    s_shelf_style = 2;
    s_shutdown_style = APP_SHUTDOWN_FULL;
    s_auto_lock_minutes = 0;
    s_main_refresh=APP_MAIN_REFRESH_NORMAL;
    s_home_full_refresh = false;
    s_ble_turner = false;
    s_reader_hold_refresh = false;
    app_settings_set_reader_vertical_turn(false);

    strlcpy(s_device_name, "Pico", sizeof(s_device_name));
    s_status_signature[0] = 0;
    s_avatar[0] = 0;
    s_lock_style = 0;
    s_wallpaper[0] = 0;
    s_touch_haptics = true;
    s_haptic_intensity = APP_HAPTIC_WEAK;
    app_settings_set_book_margin_left(0);
    app_settings_set_book_margin_right(0);
    app_settings_set_book_margin_top(0);
    app_settings_set_book_margin_bottom(0);
    app_settings_set_reader_key_preset(1);
    const int prior_restore_commits = commit_count;
    assert(app_settings_backup_restore() == ESP_OK);
    assert(history_restores == 1);
    assert(app_settings_book_weight()==1&&app_settings_book_weight_fine()==16);
    assert(!app_settings_book_antialias()&&loaded_aa==0);
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_TOOLS && app_settings_reader_key_action(1)==APP_READER_KEY_FULLSCREEN && app_settings_reader_key_action(2)==APP_READER_KEY_REFRESH);
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER && loaded_main_mode==2);
    assert(app_settings_system_font_size() == 200);
    assert(wifi_imports == 1 && saved_wifi.configured &&
           !strcmp(saved_wifi.ssid, "Home_2.4G") &&
           !strcmp(saved_wifi.password, "password123"));
    assert(app_settings_book_indent_adjust()==-7&&loaded_adjust==13);
    assert(!app_settings_touch_haptics() && app_settings_haptic_intensity()==APP_HAPTIC_STRONG);
    assert(app_settings_book_margin_left()==96 && app_settings_book_margin_right()==12 &&
           app_settings_book_margin_top()==18 && app_settings_book_margin_bottom()==30);
    assert(s_book_px == 62 && s_book_tracking == 8 && s_book_indent == 3 &&
           s_book_rule_offset == 7 && s_reader_full_pages == 5);
    assert(s_reader_turn_effect == 1 && s_reader_power_turn && !s_reader_statusbar && s_shelf_style == 2);
    assert(app_settings_reader_volume_turn());
    assert(app_settings_reader_up_action() == APP_READER_KEY_PREV);
    assert(!app_settings_reader_open_fullscreen());
    assert(app_settings_book_title_style() == APP_BOOK_TITLE_STYLE_MARK);
    assert(app_settings_book_title_size() == APP_BOOK_TITLE_SIZE_MAX);
    assert(s_shutdown_style == APP_SHUTDOWN_STAGED && app_settings_auto_lock_minutes() == 5);
    assert(s_ble_turner);
    assert(s_reader_hold_refresh);
    assert(app_settings_reader_vertical_turn());
    assert(app_settings_reader_shake_turn());
    assert(!memcmp(s_reader_chrome,custom_chrome,6));
    assert(app_settings_reader_chrome_height(0)==144&&app_settings_reader_chrome_height(1)==121);

    assert(s_home_full_refresh && !strcmp(s_device_name, "Kiiko Pico") &&
           !strcmp(s_status_signature, "今天也要读书"));
    assert(!s_avatar[0]); /* Missing avatar falls back to the default mark. */
    assert(!s_font[0]); /* Missing external font falls back to built-in. */
    assert(s_lock_style == 0 && !s_wallpaper[0]); /* Missing wallpaper uses ticket. */
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    assert(commit_count == prior_restore_commits + 1); /* Restore commits exactly once. */
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_FAST);
    assert(app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    assert(app_settings_backup_save()==ESP_OK);
    app_settings_set_main_refresh_mode(APP_MAIN_REFRESH_NORMAL);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    // 新格式 v24：题头样式、字号、全屏偏好与中键双击一起往返。
    // New v24 format: heading style, heading size, full-screen preference and middle double press round-trip together.
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_FRAME);
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MAX);
    app_settings_set_reader_open_fullscreen(false);
    app_settings_set_reader_double_action(APP_READER_KEY_FULLSCREEN);
    assert(app_settings_backup_save() == ESP_OK);
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_EMBLEM);
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MIN);
    app_settings_set_reader_open_fullscreen(true);
    app_settings_set_reader_double_action(APP_READER_KEY_NONE);
    assert(app_settings_backup_restore() == ESP_OK);
    assert(app_settings_book_title_style() == APP_BOOK_TITLE_STYLE_FRAME);
    assert(app_settings_book_title_size() == APP_BOOK_TITLE_SIZE_MAX);
    assert(!app_settings_reader_open_fullscreen());
    assert(app_settings_reader_double_action() == APP_READER_KEY_FULLSCREEN);
    assert(app_settings_backup_save() == ESP_OK);
    // 上游 K=20 备份只有 bookmark，没有本地 21..24 扩展：降级后其余字段照旧，本地扩展回落默认。
    // An upstream K=20 backup has only bookmark, not the local 21..24 extensions; drop them and expect defaults.
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fread(&saved_keys, 1, sizeof(saved_keys), saved) == sizeof(saved_keys));
    assert(fread(&saved_hold, 1, sizeof(saved_hold), saved) == sizeof(saved_hold));
    long local_fields_at = ftell(saved) + (long)(sizeof(settings_backup_indent_adjust_t) + sizeof(settings_backup_weight_t) +
                                               sizeof(settings_backup_wifi_t) + sizeof(settings_backup_background_t) +
                                               sizeof(settings_backup_home_t) + sizeof(settings_backup_background_t));
    const long local_band = (long)(sizeof(settings_backup_open_full_t) + sizeof(settings_backup_title_style_t) +
                                   sizeof(settings_backup_title_size_t) + sizeof(settings_backup_double_t) +
                                   sizeof(settings_backup_volume_t) + sizeof(settings_backup_up_t) +
                                   sizeof(settings_backup_book_fonts_t) + sizeof(settings_backup_book_vertical_t) +
                                   sizeof(settings_backup_shutdown_style_t) + sizeof(settings_backup_haptic_t) +
                                   sizeof(settings_backup_margins_t) + sizeof(settings_backup_shake_t) +
                                  sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t));
    assert(fseek(saved, 0, SEEK_END) == 0);
    long newest_end = ftell(saved);
    size_t newest_tail = (size_t)(newest_end - local_fields_at - local_band);
    uint8_t *newest_bytes = malloc(newest_tail ? newest_tail : 1);
    assert(newest_bytes);
    assert(fseek(saved, local_fields_at + local_band, SEEK_SET) == 0 &&
           fread(newest_bytes, 1, newest_tail, saved) == newest_tail);
    memcpy(legacy_header.magic, "PICOSETK", 8);
    backup_seal(&legacy_header);
    uint32_t k_ext_hash = backup_shutdown_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2]);
    for (int i = 0; i < 4; ++i) legacy_ext[i + 3] = (uint8_t)(k_ext_hash >> (8 * i));
    uint32_t k_profile_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i = 0; i < 4; ++i) saved_profile.checksum[i] = (uint8_t)(k_profile_hash >> (8 * i));
    rewind(saved);
    assert(fwrite(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fwrite(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fwrite(&saved_keys, 1, sizeof(saved_keys), saved) == sizeof(saved_keys));
    assert(fwrite(&saved_hold, 1, sizeof(saved_hold), saved) == sizeof(saved_hold));
    assert(fseek(saved, local_fields_at, SEEK_SET) == 0);
    assert(fwrite(newest_bytes, 1, newest_tail, saved) == newest_tail);
    free(newest_bytes);
    assert(fflush(saved) == 0 && ftruncate(fileno(saved), newest_end - local_band) == 0 && fclose(saved) == 0);
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_EMBLEM);
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MIN);
    app_settings_set_reader_open_fullscreen(false);
    app_settings_set_reader_double_action(APP_READER_KEY_NONE);
    app_settings_set_reader_up_action(APP_READER_KEY_NEXT);
    assert(app_settings_backup_restore() == ESP_OK);
    assert(app_settings_book_title_style() == APP_BOOK_TITLE_STYLE_PLAIN);
    assert(app_settings_book_title_size() == APP_BOOK_TITLE_SIZE_DEFAULT);
    assert(app_settings_reader_open_fullscreen());
    assert(app_settings_reader_double_action() == APP_READER_KEY_NONE);
    assert(app_settings_reader_up_action() == APP_READER_KEY_NONE);
    assert(app_settings_backup_save() == ESP_OK);
    // 旧本地 L=21：18 open_full、19 title_style、20 title_size、21 double_action，且无 bookmark。
    // Old local L=21: 18 open_full, 19 title_style, 20 title_size, 21 double_action, without bookmark.
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_FRAME);
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MAX);
    app_settings_set_reader_open_fullscreen(false);
    app_settings_set_reader_double_action(APP_READER_KEY_FULLSCREEN);
    app_settings_set_book_margin_left(48);
    app_settings_set_book_margin_right(48);
    app_settings_set_book_margin_top(48);
    app_settings_set_book_margin_bottom(48);
    app_settings_set_book_tracking(15);
    assert(app_settings_backup_save() == ESP_OK);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fread(&saved_keys, 1, sizeof(saved_keys), saved) == sizeof(saved_keys));
    assert(fread(&saved_hold, 1, sizeof(saved_hold), saved) == sizeof(saved_hold));
    long bookmark_at = ftell(saved) + (long)(sizeof(settings_backup_indent_adjust_t) + sizeof(settings_backup_weight_t) +
        sizeof(settings_backup_wifi_t) + sizeof(settings_backup_background_t) + sizeof(settings_backup_home_t));
    long local_at = bookmark_at + (long)sizeof(settings_backup_background_t);
    assert(fseek(saved, 0, SEEK_END) == 0);
    long legacy_end = ftell(saved);
    size_t legacy_tail = (size_t)(legacy_end - local_at);
    const size_t l_band = sizeof(settings_backup_open_full_t) + sizeof(settings_backup_title_style_t) +
                          sizeof(settings_backup_title_size_t) + sizeof(settings_backup_double_t);
    uint8_t *legacy_bytes = malloc(legacy_tail ? legacy_tail : 1);
    assert(legacy_bytes);
    assert(fseek(saved, local_at, SEEK_SET) == 0 && fread(legacy_bytes, 1, legacy_tail, saved) == legacy_tail);
    // L=21 在双击块后没有音量键、上键与 v27 块：摘掉它们，历史块紧跟双击块。
    // The L=21 layout has none of the volume, up-key or v27 blocks after the double press: drop
    // them so history follows directly.
    const size_t dropped_blocks = sizeof(settings_backup_volume_t) + sizeof(settings_backup_up_t) +
                                  sizeof(settings_backup_book_fonts_t) + sizeof(settings_backup_book_vertical_t) +
                                  sizeof(settings_backup_shutdown_style_t) + sizeof(settings_backup_haptic_t) +
                                  sizeof(settings_backup_margins_t) + sizeof(settings_backup_shake_t) +
                                  sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t);
    memmove(legacy_bytes + l_band, legacy_bytes + l_band + dropped_blocks,
            legacy_tail - l_band - dropped_blocks);
    legacy_tail -= dropped_blocks;
    legacy_end -= (long)dropped_blocks;
    memcpy(legacy_header.magic, "PICOSETL", 8);
    legacy_header.flags[BK_TRACKING] = 2; // 旧本地 0 px / old local 0 px
    legacy_header.flags[BK_MARGIN] = 60;  // 恢复时夹到 48 / clamped to 48 on restore
    backup_seal(&legacy_header);
    uint32_t l_ext_hash = backup_shutdown_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2]);
    for (int i = 0; i < 4; ++i) legacy_ext[i + 3] = (uint8_t)(l_ext_hash >> (8 * i));
    uint32_t l_profile_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i = 0; i < 4; ++i) saved_profile.checksum[i] = (uint8_t)(l_profile_hash >> (8 * i));
    rewind(saved);
    assert(fwrite(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fwrite(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fwrite(&saved_keys, 1, sizeof(saved_keys), saved) == sizeof(saved_keys));
    assert(fwrite(&saved_hold, 1, sizeof(saved_hold), saved) == sizeof(saved_hold));
    assert(fseek(saved, bookmark_at, SEEK_SET) == 0);
    assert(fwrite(legacy_bytes, 1, legacy_tail, saved) == legacy_tail);
    free(legacy_bytes);
    assert(fflush(saved) == 0 && ftruncate(fileno(saved), legacy_end - (long)sizeof(settings_backup_background_t)) == 0 && fclose(saved) == 0);
    app_settings_set_book_title_style(APP_BOOK_TITLE_STYLE_EMBLEM);
    app_settings_set_book_title_size(APP_BOOK_TITLE_SIZE_MIN);
    app_settings_set_reader_open_fullscreen(true);
    app_settings_set_reader_double_action(APP_READER_KEY_NONE);
    app_settings_set_book_margin_left(0);
    app_settings_set_book_margin_right(0);
    app_settings_set_book_margin_top(0);
    app_settings_set_book_margin_bottom(0);
    app_settings_set_book_tracking(0);
    assert(app_settings_backup_restore() == ESP_OK);
    assert(app_settings_book_title_style() == APP_BOOK_TITLE_STYLE_FRAME);
    assert(app_settings_book_title_size() == APP_BOOK_TITLE_SIZE_MAX);
    assert(!app_settings_reader_open_fullscreen());
    assert(app_settings_reader_double_action() == APP_READER_KEY_FULLSCREEN);
    assert(app_settings_book_margin_left() == 48 && app_settings_book_margin_right() == 48 &&
           app_settings_book_margin_top() == 0 && app_settings_book_margin_bottom() == 0);
    assert(app_settings_book_tracking() == 15);
    assert(app_settings_backup_save() == ESP_OK);
    // v11 没有微调块；保留映射、网络及阅读记录，微调安全回到0。
    // V11 lacks the adjustment block; retain keys/network/history, defaulting adjustment to zero.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fread(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold));
    long v11_adjust_at=ftell(saved);
    assert(fseek(saved,(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)),SEEK_CUR)==0);
    long v11_network_at=ftell(saved);assert(fseek(saved,0,SEEK_END)==0);long v12_end=ftell(saved);
    size_t v11_tail_bytes=(size_t)(v12_end-v11_network_at);uint8_t *v11_tail=malloc(v11_tail_bytes);assert(v11_tail);
    assert(fseek(saved,v11_network_at,SEEK_SET)==0&&fread(v11_tail,1,v11_tail_bytes,saved)==v11_tail_bytes);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(v11_tail+sizeof(settings_backup_wifi_t), v11_tail+sizeof(settings_backup_wifi_t)+(LEGACY_TAIL_BLOCKS), v11_tail_bytes-sizeof(settings_backup_wifi_t)-(LEGACY_TAIL_BLOCKS));
    v11_tail_bytes-=(LEGACY_TAIL_BLOCKS);v12_end-=(long)(LEGACY_TAIL_BLOCKS);
    memcpy(legacy_header.magic,"PICOSETB",8);legacy_header.flags[BK_TRACKING]=2;backup_seal(&legacy_header);    uint32_t v11_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(v11_hash>>(8*i));
    v11_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(v11_hash>>(8*i));
    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fwrite(&saved_hold,1,sizeof(saved_hold),saved)==sizeof(saved_hold));
    assert(ftell(saved)==v11_adjust_at&&fwrite(v11_tail,1,v11_tail_bytes,saved)==v11_tail_bytes);free(v11_tail);
    assert(fflush(saved)==0&&ftruncate(fileno(saved),v12_end-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0&&fclose(saved)==0);
    app_settings_set_book_indent_adjust(12);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_indent_adjust()==0&&loaded_adjust==20&&app_settings_book_weight()==0&&app_settings_book_antialias());
    assert(app_settings_reader_hold_action()==saved_hold.action&&app_settings_reader_key_action(0)==saved_keys.actions[0]);
    assert(app_settings_backup_save()==ESP_OK);
    // v10 按键/网络/阅读数据不变；缺少长按块时恢复返回首页。
    // Preserve v10 mappings/network/history; absent hold blocks default to home.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fread(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    long v10_tail_start=ftell(saved)+(long)sizeof(settings_backup_hold_t)+(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t));
    assert(fseek(saved,0,SEEK_END)==0);long v11_end=ftell(saved);
    size_t v10_tail_size=(size_t)(v11_end-v10_tail_start);uint8_t *v10_tail=malloc(v10_tail_size);assert(v10_tail);
    assert(fseek(saved,v10_tail_start,SEEK_SET)==0&&fread(v10_tail,1,v10_tail_size,saved)==v10_tail_size);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(v10_tail+sizeof(settings_backup_wifi_t), v10_tail+sizeof(settings_backup_wifi_t)+(LEGACY_TAIL_BLOCKS), v10_tail_size-sizeof(settings_backup_wifi_t)-(LEGACY_TAIL_BLOCKS));
    v10_tail_size-=(LEGACY_TAIL_BLOCKS);v11_end-=(long)(LEGACY_TAIL_BLOCKS);    saved_keys.actions[0]=APP_READER_KEY_HOME;saved_keys.actions[1]=APP_READER_KEY_FULLSCREEN;saved_keys.actions[2]=APP_READER_KEY_REFRESH;
    uint32_t missing_tools_hash=backup_keys_checksum(&saved_keys);
    for(int i=0;i<4;++i)saved_keys.checksum[i]=(uint8_t)(missing_tools_hash>>(i*8));
    memcpy(legacy_header.magic,"PICOSETA",8);legacy_header.flags[BK_TRACKING]=4;backup_seal(&legacy_header);
    uint32_t v10_ext_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(v10_ext_hash>>(i*8));
    uint32_t v10_profile_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(v10_profile_hash>>(i*8));
    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(&saved_keys,1,sizeof(saved_keys),saved)==sizeof(saved_keys));
    assert(fwrite(v10_tail,1,v10_tail_size,saved)==v10_tail_size);free(v10_tail);assert(fflush(saved)==0);
    assert(ftruncate(fileno(saved),v11_end-(long)sizeof(settings_backup_hold_t)-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0&&fclose(saved)==0);
    unsigned prior_wifi=wifi_imports,prior_history=history_restores;
    s_reader_hold_action=APP_READER_KEY_HOME;assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME && app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER && app_settings_book_indent_adjust()==0);
    assert(wifi_imports==prior_wifi+1&&history_restores==prior_history+1);
    assert(app_settings_reader_key_action(0)==saved_keys.actions[0]&&app_settings_reader_key_action(1)==saved_keys.actions[1]&&app_settings_reader_key_action(2)==saved_keys.actions[2]);
    assert(app_settings_backup_save()==ESP_OK); // Continue v8 conversion from current v11.
    // v8 的旧实验位不启用新模式，既有备份仍可恢复。
    // A v8 retired-test bit cannot enable a new mode; existing backups remain readable.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fread(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fread(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fread(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    memcpy(legacy_header.magic,"PICOSET8",8);legacy_header.flags[BK_TRACKING]=4;backup_seal(&legacy_header);legacy_ext[2]&=7;
    uint32_t ext_hash=backup_shutdown_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2]);
    for(int i=0;i<4;++i)legacy_ext[i+3]=(uint8_t)(ext_hash>>(i*8));
    saved_profile.home_full_refresh=(saved_profile.home_full_refresh&127)|64;
    legacy_hash=backup_profile_checksum(&legacy_header,legacy_ext[0],legacy_ext[1],legacy_ext[2],&saved_profile);
    for(int i=0;i<4;++i)saved_profile.checksum[i]=(uint8_t)(legacy_hash>>(i*8));
    long tail_start=ftell(saved)+(long)sizeof(settings_backup_keys_t)+(long)sizeof(settings_backup_hold_t)+(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t));
    assert(fseek(saved,0,SEEK_END)==0);long file_end=ftell(saved);
    size_t tail_size=(size_t)(file_end-tail_start);uint8_t *tail=malloc(tail_size);assert(tail);
    assert(fseek(saved,tail_start,SEEK_SET)==0&&fread(tail,1,tail_size,saved)==tail_size);
    /* 旧格式没有背景和首页组件块。/ Legacy tails omit background and Home blocks. */
    memmove(tail+sizeof(settings_backup_wifi_t), tail+sizeof(settings_backup_wifi_t)+(LEGACY_TAIL_BLOCKS), tail_size-sizeof(settings_backup_wifi_t)-(LEGACY_TAIL_BLOCKS));
    tail_size-=(LEGACY_TAIL_BLOCKS);file_end-=(long)(LEGACY_TAIL_BLOCKS);    rewind(saved);assert(fwrite(&legacy_header,1,sizeof(legacy_header),saved)==sizeof(legacy_header));
    assert(fwrite(legacy_ext,1,sizeof(legacy_ext),saved)==sizeof(legacy_ext));
    assert(fwrite(&saved_profile,1,sizeof(saved_profile),saved)==sizeof(saved_profile));
    assert(fwrite(tail,1,tail_size,saved)==tail_size);free(tail);assert(fflush(saved)==0);
    assert(ftruncate(fileno(saved),file_end-(long)sizeof(settings_backup_keys_t)-(long)sizeof(settings_backup_hold_t)-(long)(sizeof(settings_backup_indent_adjust_t)+sizeof(settings_backup_weight_t)))==0);assert(fclose(saved)==0);
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_main_refresh_mode()==APP_MAIN_REFRESH_WATER);
    assert(app_settings_reader_key_action(0)==APP_READER_KEY_PREV && app_settings_reader_key_action(1)==APP_READER_KEY_TOOLS && app_settings_reader_key_action(2)==APP_READER_KEY_NEXT);
    assert(app_settings_reader_hold_action()==APP_READER_KEY_HOME);
    app_settings_set_reader_full_pages(30);
    assert(s_reader_full_pages == 30);
    app_settings_set_reader_full_pages(0);
    assert(s_reader_full_pages == 0);
    app_settings_set_book_reading_line_offset(-8);
    assert(app_settings_book_reading_line_offset() == -8);
    app_settings_set_book_reading_line_offset(7); /* Reject odd-pixel shifts. */
    assert(app_settings_book_reading_line_offset() == -8);

    // 长按是唯一工具栏入口的完整备份必须保持三键自定义，而不能强制覆盖中键短按。
    // A backup with hold as its only toolbar entry must retain all three short actions.
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_TOOLS));
    assert(app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_backup_save()==ESP_OK);
    app_settings_set_reader_key_preset(1);
    assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_key_action(1)==APP_READER_KEY_HOME && app_settings_reader_hold_action()==APP_READER_KEY_TOOLS);
    // 中键双击与单击/长按一起保存、恢复；提交失败不改内存。
    // The middle double action saves and restores alongside click/hold, and a failed commit keeps RAM unchanged.
    assert(app_settings_set_reader_double_action(APP_READER_KEY_FULLSCREEN));
    assert(app_settings_reader_double_action()==APP_READER_KEY_FULLSCREEN);
    assert(app_settings_backup_save()==ESP_OK);
    app_settings_set_reader_double_action(APP_READER_KEY_NONE);
    assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_double_action()==APP_READER_KEY_FULLSCREEN);
    commit_fails=true;assert(!app_settings_set_reader_double_action(APP_READER_KEY_HOME));commit_fails=false;
    assert(app_settings_reader_double_action()==APP_READER_KEY_FULLSCREEN);
    app_settings_set_reader_double_action(APP_READER_KEY_NONE);
    assert(app_settings_set_reader_hold_action(APP_READER_KEY_NONE));
    // 无工具栏按键的映射在保存、恢复和重启后保持不变；固定上滑入口由阅读控制测试覆盖。
    // A mapping without toolbar keys survives save/restore/reboot; reader-control tests cover the invariant swipe entry.
    assert(app_settings_set_reader_key_action(0,APP_READER_KEY_PREV));
    assert(app_settings_set_reader_key_action(1,APP_READER_KEY_HOME));
    assert(app_settings_set_reader_key_action(2,APP_READER_KEY_NEXT));
    assert(app_settings_backup_save()==ESP_OK);app_settings_set_reader_key_preset(1);
    assert(app_settings_backup_restore()==ESP_OK);app_settings_init();
    assert(app_settings_reader_hold_action()==APP_READER_KEY_NONE&&app_settings_reader_key_action(1)==APP_READER_KEY_HOME);
    assert(app_settings_reader_key_action_allowed(4,APP_READER_KEY_TOOLS));
    assert(app_settings_reader_key_action_allowed(5,APP_READER_KEY_TOOLS));
    assert(!app_settings_reader_key_action_allowed(6,APP_READER_KEY_TOOLS));
    assert(!app_settings_reader_key_action_allowed(0,APP_READER_KEY_COUNT));
    app_settings_set_reader_key_preset(1);
    s_book_px = 70;
    assert(app_settings_backup_save() == ESP_OK); /* Overwrite an existing backup. */
    s_book_px = 48;
    commit_fails = true;
    strlcpy(saved_wifi.ssid, "Other_2.4G", sizeof(saved_wifi.ssid));
    assert(app_settings_backup_restore() == ESP_FAIL);
    assert(s_book_px == 48); /* RAM state is unchanged on commit failure. */
    assert(!strcmp(saved_wifi.ssid, "Other_2.4G"));
    commit_fails = false;
    assert(app_settings_backup_restore() == ESP_OK && s_book_px == 70 &&
           s_reader_full_pages == 0 && app_settings_book_reading_line_offset() == -8 &&
           s_shutdown_style == APP_SHUTDOWN_STAGED);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G"));

    /* PICOSET3 backups predate the shutdown choice and restore the safe off default. */
    FILE *file = fopen(BACKUP_FILE, "rb");
    assert(file);
    settings_backup_v1_t legacy;
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET3", sizeof(legacy.magic));
    if(backup_version(&legacy)<19)legacy.flags[BK_TRACKING]=4;
    strlcpy(legacy.books_dir,"/sdcard/legacy/books",sizeof(legacy.books_dir));
    strlcpy(legacy.fonts_dir,"/sdcard/legacy/fonts",sizeof(legacy.fonts_dir));
    backup_seal(&legacy);
    uint8_t v3_extension[6] = {3, 4};
    uint32_t v3_hash = backup_rule_offset_checksum(&legacy, v3_extension[0], v3_extension[1]);
    for (int i = 0; i < 4; ++i) v3_extension[i + 2] = (uint8_t)(v3_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(v3_extension, 1, sizeof(v3_extension), file) == sizeof(v3_extension));
    assert(fclose(file) == 0);
    /* 彻底断电档已移除：旧备份里的 FULL 一律迁移为彻底关机。/ Full-off is retired: legacy FULL in backups migrates to full off. */
    assert(app_settings_backup_restore() == ESP_OK && s_shutdown_style == APP_SHUTDOWN_STAGED);
    assert(!strcmp(app_settings_device_name(), "kimiko"));
    assert(!strcmp(app_settings_books_dir(),"/sdcard/books")&&!strcmp(app_settings_fonts_dir(),"/sdcard/fonts"));
    app_settings_init();
    assert(!strcmp(app_settings_books_dir(),"/sdcard/books")&&!strcmp(app_settings_fonts_dir(),"/sdcard/fonts"));
    assert(!s_home_full_refresh && !strcmp(s_device_name, "kimiko") && !s_status_signature[0]);
    assert(!strcmp(saved_wifi.ssid, "Home_2.4G")); /* Old backups leave network alone. */

    /* Existing PICOSET2 backups remain readable, with centered guide lines. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET2", sizeof(legacy.magic));
    if(backup_version(&legacy)<19)legacy.flags[BK_TRACKING]=4;
    backup_seal(&legacy);
    uint8_t old_extension[5] = {3};
    uint32_t old_hash = backup_indent_checksum(&legacy, old_extension[0]);
    for (int i = 0; i < 4; ++i) old_extension[i + 1] = (uint8_t)(old_hash >> (i * 8));
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fwrite(old_extension, 1, sizeof(old_extension), file) == sizeof(old_extension));
    assert(fclose(file) == 0);
    s_book_rule_offset = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_rule_offset == 4 && !s_reader_hold_refresh);
    assert(!app_settings_reader_vertical_turn());

    /* Existing PICOSET1 backups remain readable and use the two-em default. */
    file = fopen(BACKUP_FILE, "rb");
    assert(file);
    assert(fread(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    memcpy(legacy.magic, "PICOSET1", sizeof(legacy.magic));
    if(backup_version(&legacy)<19)legacy.flags[BK_TRACKING]=4;
    backup_seal(&legacy);
    file = fopen(BACKUP_FILE, "wb");
    assert(file);
    assert(fwrite(&legacy, 1, sizeof(legacy), file) == sizeof(legacy));
    assert(fclose(file) == 0);
    s_book_indent = 0;
    assert(app_settings_backup_restore() == ESP_OK && s_book_indent == 2);
    /* A backup without a configured network clears an existing saved network. */
    memset(&saved_wifi, 0, sizeof(saved_wifi));
    assert(app_settings_backup_save() == ESP_OK);
    saved_wifi.configured = 1;
    strlcpy(saved_wifi.ssid, "Temporary", sizeof(saved_wifi.ssid));
    strlcpy(saved_wifi.password, "temporary123", sizeof(saved_wifi.password));
    assert(app_settings_backup_restore() == ESP_OK && !saved_wifi.configured);

    file = fopen(BACKUP_FILE, "r+b");
    assert(file);
    assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    s_book_px = 48;
    assert(app_settings_backup_restore() == ESP_ERR_INVALID_RESPONSE);
    assert(s_book_px == 48);
    assert(remove(BACKUP_FILE) == 0);
    assert(app_settings_backup_restore() == ESP_ERR_NOT_FOUND);
    for (int percent = 100; percent <= 200; percent += 10) {
        app_settings_set_system_font_size((uint8_t)percent);
        assert(app_settings_system_font_size() == percent);
    }
    app_settings_set_system_font_size(210);
    app_settings_set_system_font_size(195);
    assert(app_settings_system_font_size() == 200);
    // 保存失败保留当前字体和字号；同步阅读字体只在确认后单独保存。
    // Failed commits retain the current fonts and scale; reading-font sync is a separate confirmed write.
    app_settings_set_system_font_path("/sdcard/fonts/System.ttf");
    app_settings_set_font_path("/sdcard/fonts/Reading.ttf");
    app_settings_set_system_font_size(150);
    commit_fails = true;
    app_settings_set_system_font_path("/sdcard/fonts/Other.ttf");
    app_settings_set_font_path("/sdcard/fonts/Other.ttf");
    app_settings_set_system_font_size(200);
    assert(!strcmp(app_settings_system_font_path(), "/sdcard/fonts/System.ttf"));
    assert(!strcmp(app_settings_font_path(), "/sdcard/fonts/Reading.ttf"));
    assert(app_settings_system_font_size() == 150);
    commit_fails = false;
    char too_long_font[256];
    memset(too_long_font, 'x', sizeof(too_long_font)-1);
    too_long_font[sizeof(too_long_font)-1] = 0;
    app_settings_set_system_font_path(too_long_font);
    app_settings_set_font_path(too_long_font);
    assert(!strcmp(app_settings_system_font_path(), "/sdcard/fonts/System.ttf"));
    assert(!strcmp(app_settings_font_path(), "/sdcard/fonts/Reading.ttf"));
    app_settings_set_system_font_path("");
    app_settings_set_font_path("");
    assert(!app_settings_system_font_path()[0] && !app_settings_font_path()[0]);
    test_loaded_system_size = 180; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 180);
    test_loaded_system_size = 200; s_system_size = 120;
    app_settings_init(); assert(app_settings_system_font_size() == 200);


    // 已删除的3/4样式在旧NVS与旧备份都回退到亚克力，1/2保持原选择。
    // Retired styles 3/4 fall back from old NVS and backups; preserve retained choices 1/2.
    for (int style=1;style<=5;++style) {
        loaded_shelf=style;loaded_shelf_v22=true;s_shelf_style=1;
        app_settings_init();assert(app_settings_shelf_style()==((style==3||style==4)?2:style));
        assert(loaded_shelf==((style==3||style==4)?2:style));
        s_shelf_style=(uint8_t)style;
        assert(app_settings_backup_save()==ESP_OK);
        s_shelf_style=1;assert(app_settings_backup_restore()==ESP_OK);
        assert(app_settings_shelf_style()==((style==3||style==4)?2:style));
    }
    for (unsigned style=0;style<3;++style) {
        app_settings_set_lock_style((uint8_t)style);
        s_lock_style=99;app_settings_init();assert(app_settings_lock_style()==style);
    }
    app_settings_set_lock_style(2);s_wallpaper[0]=0;
    assert(app_settings_backup_save()==ESP_OK);
    s_lock_style=0;assert(app_settings_backup_restore()==ESP_OK&&app_settings_lock_style()==2);
    assert(loaded_lock==2&&!s_wallpaper[0]);
    s_home_component=(app_home_component_t)3;assert(app_settings_backup_save()==ESP_OK);
    s_home_component=APP_HOME_KEHAI;assert(app_settings_backup_restore()==ESP_OK&&app_settings_home_component()==APP_HOME_CLOCK);
    assert(remove(BACKUP_FILE)==0);
    for(unsigned old=0;old<=4;++old){has_tracking=true;loaded_tracking=old;app_settings_init();assert(app_settings_book_tracking()==old*2-4-APP_BOOK_TRACK_MIN);}
    for(unsigned old=128;old<=148;++old){
        has_tracking=true;loaded_tracking=old;app_settings_init();
        int px=(int)old-128-4;if(px>15)px=15;
        assert(app_settings_book_tracking()==px-APP_BOOK_TRACK_MIN);
    }
    for(unsigned track=0;track<APP_BOOK_TRACK_COUNT;++track){
        s_book_tracking=255;app_settings_set_book_tracking(track);assert(loaded_tracking==192+track);
        s_book_tracking=255;app_settings_init();assert(app_settings_book_tracking()==track);
    }
    const uint8_t invalid_tracks[]={5,127,149,191,223,255};
    for(unsigned i=0;i<sizeof(invalid_tracks);++i){loaded_tracking=invalid_tracks[i];s_book_tracking=255;app_settings_init();assert(s_book_tracking==-APP_BOOK_TRACK_MIN);}
    assert(APP_BOOK_TRACK_COUNT==31&&-APP_BOOK_TRACK_MIN==APP_BOOK_TRACK_COUNT/2);
    for(unsigned track=0;track<APP_BOOK_TRACK_COUNT;++track){
        app_settings_set_book_tracking(track);assert(app_settings_backup_save()==ESP_OK);
        s_book_tracking=255;assert(app_settings_backup_restore()==ESP_OK);
        assert(s_book_tracking==track&&loaded_tracking==192+track);
    }
    // V19备份按实际像素迁移，超过新上限的+16px收敛为+15px。/ V19 backups retain physical spacing, clamping +16px to +15px.
    for(unsigned old=0;old<=20;++old){
        app_settings_set_book_paragraph_spacing(25);assert(app_settings_backup_save()==ESP_OK);
        FILE *legacy_file=fopen(BACKUP_FILE,"r+b");assert(legacy_file);
        settings_backup_v1_t header;uint8_t ext[7];settings_backup_profile_t profile;
        assert(fread(&header,1,sizeof(header),legacy_file)==sizeof(header));
        assert(fread(ext,1,sizeof(ext),legacy_file)==sizeof(ext));
        assert(fread(&profile,1,sizeof(profile),legacy_file)==sizeof(profile));
        memcpy(header.magic,"PICOSETJ",8);header.flags[BK_TRACKING]=(uint8_t)old;backup_seal(&header);
        uint32_t hash=backup_shutdown_checksum(&header,ext[0],ext[1],ext[2]);
        for(int i=0;i<4;++i)ext[3+i]=(uint8_t)(hash>>(8*i));
        hash=backup_profile_checksum(&header,ext[0],ext[1],ext[2],&profile);
        for(int i=0;i<4;++i)profile.checksum[i]=(uint8_t)(hash>>(8*i));
        rewind(legacy_file);assert(fwrite(&header,1,sizeof(header),legacy_file)==sizeof(header));
        assert(fwrite(ext,1,sizeof(ext),legacy_file)==sizeof(ext));
        assert(fwrite(&profile,1,sizeof(profile),legacy_file)==sizeof(profile));
        strip_local_extension_blocks(legacy_file);
        assert(fclose(legacy_file)==0);
        s_book_tracking=255;assert(app_settings_backup_restore()==ESP_OK);
        int px=(int)old-4;if(px>15)px=15;
        assert(s_book_tracking==px-APP_BOOK_TRACK_MIN&&loaded_tracking==192+s_book_tracking);
    }
    const uint8_t paragraph_stops[]={0,5,10,15,20,25,30,40,50,75,100};
    assert(APP_BOOK_PARAGRAPH_COUNT==11);
    for(unsigned i=0;i<APP_BOOK_PARAGRAPH_COUNT;++i){
        assert(app_settings_book_paragraph_value(i)==paragraph_stops[i]);
        assert(app_settings_book_paragraph_level(paragraph_stops[i])==i);
        app_settings_set_book_paragraph_spacing(paragraph_stops[i]);
        assert(app_settings_book_paragraph_spacing()==paragraph_stops[i]);
        s_book_para=255;app_settings_init();assert(s_book_para==paragraph_stops[i]);
        assert(app_settings_backup_save()==ESP_OK);s_book_para=255;
        assert(app_settings_backup_restore()==ESP_OK&&s_book_para==paragraph_stops[i]);
    }
    assert(app_settings_book_paragraph_value(APP_BOOK_PARAGRAPH_COUNT/2)==25);
    has_tracking=false;s_book_tracking=-APP_BOOK_TRACK_MIN;
    // PNG书签独立于壁纸保存；恢复缺失PNG时退票根，不能留无效锁屏入口。
    // PNG bookmarks persist independently of wallpapers; a missing restored PNG falls back to a usable ticket.
    app_settings_set_bookmark_path("/sdcard/pictures/marker.png");app_settings_set_lock_style(3);
    assert(app_settings_lock_style()==3&&!strcmp(app_settings_bookmark_path(),"/sdcard/pictures/marker.png"));
    app_settings_set_bookmark_path("/sdcard/pictures/not-png.jpg");
    assert(!strcmp(app_settings_bookmark_path(),"/sdcard/pictures/marker.png"));
    assert(app_settings_backup_save()==ESP_OK);s_bookmark[0]=0;s_lock_style=0;
    assert(app_settings_backup_restore()==ESP_OK&&app_settings_lock_style()==0&&!app_settings_bookmark_path()[0]);
    // v27：书内字体、竖排正文与完整关机方式往返保存；旧 v26 备份缺少这些块时回落开启/横排/扩展位。
    // V27 round-trips book fonts, vertical text and every shutdown state; a v26 backup without
    // those blocks falls back to on, horizontal and the extension bit.
    app_settings_set_book_fonts(false);
    app_settings_set_book_vertical(true);
    app_settings_set_shutdown_style(APP_SHUTDOWN_LIGHT);
    assert(app_settings_backup_save() == ESP_OK);
    app_settings_set_book_fonts(true);
    app_settings_set_book_vertical(false);
    app_settings_set_shutdown_style(APP_SHUTDOWN_STAGED);
    assert(app_settings_backup_restore() == ESP_OK);
    assert(!app_settings_book_fonts() && app_settings_book_vertical() &&
           app_settings_shutdown_style() == APP_SHUTDOWN_LIGHT);
    saved = fopen(BACKUP_FILE, "r+b");
    assert(saved && fread(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fread(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fread(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    const long v26_tail_at = ftell(saved);
    assert(fseek(saved, 0, SEEK_END) == 0);
    size_t v26_tail = (size_t)(ftell(saved) - v26_tail_at);
    uint8_t *v26_bytes = malloc(v26_tail);
    assert(v26_bytes && fseek(saved, v26_tail_at, SEEK_SET) == 0 &&
           fread(v26_bytes, 1, v26_tail, saved) == v26_tail);
    const long v27_at = (long)(sizeof(settings_backup_keys_t) + sizeof(settings_backup_hold_t) +
        sizeof(settings_backup_indent_adjust_t) + sizeof(settings_backup_weight_t) + sizeof(settings_backup_wifi_t) +
        sizeof(settings_backup_background_t) + sizeof(settings_backup_home_t) + sizeof(settings_backup_background_t) +
        sizeof(settings_backup_open_full_t) + sizeof(settings_backup_title_style_t) + sizeof(settings_backup_title_size_t) +
        sizeof(settings_backup_double_t) + sizeof(settings_backup_volume_t) + sizeof(settings_backup_up_t));
    const size_t v27_blocks = sizeof(settings_backup_book_fonts_t) + sizeof(settings_backup_book_vertical_t) +
                              sizeof(settings_backup_shutdown_style_t) + sizeof(settings_backup_haptic_t) +
                              sizeof(settings_backup_margins_t) + sizeof(settings_backup_shake_t) +
                                  sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t);
    assert(v26_tail > (size_t)v27_at + v27_blocks);
    memmove(v26_bytes + v27_at, v26_bytes + v27_at + v27_blocks, v26_tail - v27_at - v27_blocks);
    v26_tail -= v27_blocks;
    memcpy(legacy_header.magic, "PICOSETQ", 8);
    backup_seal(&legacy_header);
    uint32_t v26_ext_hash = backup_shutdown_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2]);
    for (int i = 0; i < 4; ++i) legacy_ext[i + 3] = (uint8_t)(v26_ext_hash >> (8 * i));
    uint32_t v26_profile_hash = backup_profile_checksum(&legacy_header, legacy_ext[0], legacy_ext[1], legacy_ext[2], &saved_profile);
    for (int i = 0; i < 4; ++i) saved_profile.checksum[i] = (uint8_t)(v26_profile_hash >> (8 * i));
    rewind(saved);
    assert(fwrite(&legacy_header, 1, sizeof(legacy_header), saved) == sizeof(legacy_header));
    assert(fwrite(legacy_ext, 1, sizeof(legacy_ext), saved) == sizeof(legacy_ext));
    assert(fwrite(&saved_profile, 1, sizeof(saved_profile), saved) == sizeof(saved_profile));
    assert(fwrite(v26_bytes, 1, v26_tail, saved) == v26_tail);
    free(v26_bytes);
    assert(fflush(saved) == 0 && ftruncate(fileno(saved), v26_tail_at + (long)v26_tail) == 0 && fclose(saved) == 0);
    app_settings_set_book_fonts(true);
    app_settings_set_book_vertical(false);
    app_settings_set_shutdown_style(APP_SHUTDOWN_LIGHT);
    assert(app_settings_backup_restore() == ESP_OK);
    assert(app_settings_book_fonts() && !app_settings_book_vertical() &&
           app_settings_shutdown_style() == APP_SHUTDOWN_STAGED);
    // v26 老备份没有晃动块，恢复回落默认关闭。
    // A v26 backup has no shake block; restore falls back to the off default.
    assert(!app_settings_reader_shake_turn());
    assert(!memcmp(s_reader_chrome,s_reader_chrome_defaults,6));
    assert(app_settings_reader_chrome_height(0)==80&&app_settings_reader_chrome_height(1)==57);
    // 损坏枚举或校验和在任何提交前拒绝；还原合法尾块后仍能往返。
    // Reject invalid enums/checksums before committing; restoring the valid tail keeps round trips working.
    for(unsigned i=0;i<6;++i)assert(app_settings_set_reader_chrome_item(i/3,i%3,custom_chrome[i]));
    assert(app_settings_backup_save()==ESP_OK);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fseek(saved,-(long)(8+sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t)),SEEK_END)==0);
    long chrome_at=ftell(saved);settings_backup_chrome_t valid_chrome;
    assert(fread(&valid_chrome,1,sizeof(valid_chrome),saved)==sizeof(valid_chrome));
    assert(backup_chrome_valid(&valid_chrome)&&!memcmp(valid_chrome.items,custom_chrome,6));
    assert(fclose(saved)==0);
    for(unsigned fault=0;fault<2;++fault){
        settings_backup_chrome_t bad=valid_chrome;
        if(fault==0)bad.checksum[0]^=1;
        else{
            bad.items[0]=APP_READER_SLOT_COUNT;
            uint32_t hash=backup_chrome_checksum(&bad);
            for(unsigned j=0;j<4;++j)bad.checksum[j]=(uint8_t)(hash>>(8*j));
        }
        saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,chrome_at,SEEK_SET)==0);
        assert(fwrite(&bad,1,sizeof(bad),saved)==sizeof(bad)&&fclose(saved)==0);
        int commits_before=commit_count;unsigned imports_before=wifi_imports,history_before=history_restores;
        assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
        assert(commit_count==commits_before&&wifi_imports==imports_before&&history_restores==history_before&&!memcmp(s_reader_chrome,custom_chrome,6));
    }
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,chrome_at,SEEK_SET)==0);
    assert(fwrite(&valid_chrome,1,sizeof(valid_chrome),saved)==sizeof(valid_chrome)&&fclose(saved)==0);
    assert(app_settings_set_reader_chrome_default());
    assert(app_settings_backup_restore()==ESP_OK&&!memcmp(s_reader_chrome,custom_chrome,6));
    // 新高度块损坏必须在 NVS、网络凭据和历史恢复前拒绝。
    // Reject a corrupted height block before NVS, credentials or history restoration.
    assert(app_settings_set_reader_chrome_heights(96,73));
    assert(app_settings_backup_save()==ESP_OK);
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    assert(fseek(saved,-(long)(8+sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t)),SEEK_END)==0);
    long height_at=ftell(saved);settings_backup_chrome_heights_t valid_height;
    assert(fread(&valid_height,1,sizeof(valid_height),saved)==sizeof(valid_height)&&fclose(saved)==0);
    for(unsigned fault=0;fault<3;++fault){
        settings_backup_chrome_heights_t bad=valid_height;
        if(fault==0)bad.checksum[0]^=1;
        else{
            if(fault==1)bad.header=65;else bad.footer=122;
            uint32_t hash=backup_chrome_heights_checksum(&bad);
            for(unsigned j=0;j<4;++j)bad.checksum[j]=(uint8_t)(hash>>(8*j));
        }
        saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,height_at,SEEK_SET)==0);
        assert(fwrite(&bad,1,sizeof(bad),saved)==sizeof(bad)&&fclose(saved)==0);
        int before=commit_count;unsigned imports=wifi_imports,history=history_restores;
        assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
        assert(commit_count==before&&wifi_imports==imports&&history_restores==history);
        assert(app_settings_reader_chrome_height(0)==96&&app_settings_reader_chrome_height(1)==73);
    }
    saved=fopen(BACKUP_FILE,"r+b");assert(saved&&fseek(saved,height_at,SEEK_SET)==0);
    assert(fwrite(&valid_height,1,sizeof(valid_height),saved)==sizeof(valid_height)&&fclose(saved)==0);
    assert(app_settings_set_reader_chrome_default());
    assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_chrome_height(0)==96&&app_settings_reader_chrome_height(1)==73);
    // 旧 v31 保留六槽，但没有高度块；恢复时必须重置高度而不是沿用当前值。
    // Legacy v31 retains slots without heights; restore must reset heights rather than keep current values.
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    settings_backup_v1_t v31_header;uint8_t v31_ext[7];settings_backup_profile_t v31_profile;
    assert(fread(&v31_header,1,sizeof(v31_header),saved)==sizeof(v31_header));
    assert(fread(v31_ext,1,sizeof(v31_ext),saved)==sizeof(v31_ext));
    assert(fread(&v31_profile,1,sizeof(v31_profile),saved)==sizeof(v31_profile));
    memcpy(v31_header.magic,"PICOSETV",8);backup_seal(&v31_header);
    uint32_t v31_hash=backup_shutdown_checksum(&v31_header,v31_ext[0],v31_ext[1],v31_ext[2]);
    for(unsigned j=0;j<4;++j)v31_ext[j+3]=(uint8_t)(v31_hash>>(8*j));
    v31_hash=backup_profile_checksum(&v31_header,v31_ext[0],v31_ext[1],v31_ext[2],&v31_profile);
    for(unsigned j=0;j<4;++j)v31_profile.checksum[j]=(uint8_t)(v31_hash>>(8*j));
    rewind(saved);
    assert(fwrite(&v31_header,1,sizeof(v31_header),saved)==sizeof(v31_header));
    assert(fwrite(v31_ext,1,sizeof(v31_ext),saved)==sizeof(v31_ext));
    assert(fwrite(&v31_profile,1,sizeof(v31_profile),saved)==sizeof(v31_profile));
    assert(fseek(saved,height_at,SEEK_SET)==0&&fwrite("RPHIST1",1,8,saved)==8);
    assert(fflush(saved)==0&&ftruncate(fileno(saved),height_at+8)==0&&fclose(saved)==0);
    assert(app_settings_backup_restore()==ESP_OK);
    assert(app_settings_reader_chrome_height(0)==80&&app_settings_reader_chrome_height(1)==57);
    assert(!memcmp(s_reader_chrome,custom_chrome,6));
    for(int h=64;h<=144;h+=8)for(int f=57;f<=121;f+=8){
        assert(app_settings_set_reader_chrome_heights(h,f));
        s_reader_chrome_heights[0]=s_reader_chrome_heights[1]=0;app_settings_init();
        assert(app_settings_reader_chrome_height(0)==h&&app_settings_reader_chrome_height(1)==f);
    }
    loaded_chrome_heights[0]=65;loaded_chrome_heights[1]=122;app_settings_init();
    assert(app_settings_reader_chrome_height(0)==80&&app_settings_reader_chrome_height(1)==57);
    loaded_chrome_heights[0]=loaded_chrome_heights[1]=-1;
    s_reader_chrome_heights[0]=144;s_reader_chrome_heights[1]=121;app_settings_init();
    assert(app_settings_reader_chrome_height(0)==80&&app_settings_reader_chrome_height(1)==57);
    // 所有阅读字号都可保存与备份恢复，范围外仍使用默认48。
    // Every reader size survives settings/backup restore; out-of-range values still use default 48.
    for(int px=18;px<=90;++px){
        app_settings_set_book_px((uint8_t)px);assert(app_settings_book_px()==px);
        assert(app_settings_backup_save()==ESP_OK);s_book_px=0;
        assert(app_settings_backup_restore()==ESP_OK&&app_settings_book_px()==px);
    }
    app_settings_set_book_px(17);assert(app_settings_book_px()==48);
    app_settings_set_book_px(91);assert(app_settings_book_px()==48);
    // 新旧档位均能重启读取和备份往返，未知值与旧 FULL 仍回落立即关机。
    // Old/new styles survive reboot reads and backup round-trips; unknown values and legacy FULL still fall back to immediate off.
    const uint8_t durations[] = {0, 0, 0, 10, 30, 60};
    for(unsigned style=APP_SHUTDOWN_STAGED;style<=APP_SHUTDOWN_LIGHT_60;++style){
        app_settings_set_shutdown_style((app_shutdown_style_t)style);
        s_shutdown_style=APP_SHUTDOWN_FULL;app_settings_init();
        assert(app_settings_shutdown_style()==(app_shutdown_style_t)style);
        assert(app_settings_shutdown_minutes()==durations[style]);
        assert(app_settings_backup_save()==ESP_OK);
        app_settings_set_shutdown_style(style==APP_SHUTDOWN_STAGED?APP_SHUTDOWN_LIGHT:APP_SHUTDOWN_STAGED);
        assert(app_settings_backup_restore()==ESP_OK);
        assert(app_settings_shutdown_style()==(app_shutdown_style_t)style&&loaded_shutdown==style);
        s_shutdown_style=APP_SHUTDOWN_FULL;app_settings_init();
        assert(app_settings_shutdown_minutes()==durations[style]);
    }
    const unsigned invalid_styles[]={APP_SHUTDOWN_FULL,6,255};
    for(unsigned i=0;i<sizeof(invalid_styles)/sizeof(invalid_styles[0]);++i){
        loaded_shutdown=invalid_styles[i];has_shutdown=true;app_settings_init();
        assert(app_settings_shutdown_style()==APP_SHUTDOWN_STAGED&&!app_settings_shutdown_minutes());
        app_settings_set_shutdown_style(APP_SHUTDOWN_LIGHT);
        app_settings_set_shutdown_style((app_shutdown_style_t)invalid_styles[i]);
        assert(app_settings_shutdown_style()==APP_SHUTDOWN_STAGED&&loaded_shutdown==APP_SHUTDOWN_STAGED);
    }
    settings_backup_shutdown_style_t bad_shutdown={.style=6};
    uint32_t bad_hash=backup_shutdown_style_checksum(&bad_shutdown);
    for(unsigned i=0;i<4;++i)bad_shutdown.checksum[i]=(uint8_t)(bad_hash>>(8*i));
    assert(!backup_shutdown_style_valid(&bad_shutdown));
    saved=fopen(BACKUP_FILE,"r+b");assert(saved);
    const long shutdown_tail=(long)(sizeof(settings_backup_shutdown_style_t)+sizeof(settings_backup_haptic_t)+
        sizeof(settings_backup_margins_t)+sizeof(settings_backup_shake_t)+sizeof(settings_backup_chrome_t) + sizeof(settings_backup_chrome_heights_t) + sizeof(settings_backup_home_regions_t));
    assert(fseek(saved,-shutdown_tail,SEEK_END)==0);
    assert(fwrite(&bad_shutdown,1,sizeof(bad_shutdown),saved)==sizeof(bad_shutdown)&&fclose(saved)==0);
    int commits_before_bad_shutdown=commit_count;
    unsigned imports_before_bad_shutdown=wifi_imports;
    assert(app_settings_backup_restore()==ESP_ERR_INVALID_RESPONSE);
    assert(commit_count==commits_before_bad_shutdown&&wifi_imports==imports_before_bad_shutdown&&
           app_settings_shutdown_style()==APP_SHUTDOWN_STAGED);
    test_home_regions_backup();
    test_device_name_branding();
    puts("settings backup host test passed (legacy formats, Home regions, 200% sizes, retired shelf migration)");
    return 0;
}
