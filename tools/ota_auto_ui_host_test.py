#!/usr/bin/env python3
"""中文：执行实际升级入口、触摸和开关保存逻辑。/ English: Execute actual upgrade entry, touch handling and switch persistence logic."""
from pathlib import Path
import subprocess
import tempfile
from home_cover_cache_host_test import function
from run_reader_baseline import toolchain

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "main/apps/app_device_settings.c"
source = SETTINGS.read_text()

def branch(body, marker):
    start = body.index(marker)
    at = body.index("{", start) + 1
    depth = 1
    while depth:
        depth += (body[at] == "{") - (body[at] == "}")
        at += 1
    return body[start:at]

upgrade_touch = branch(function("on_gesture", SETTINGS), "if (s_page == SETTINGS_UPGRADE)")
upgrade_render = branch(function("render", SETTINGS), "if (s_page == SETTINGS_UPGRADE)")
assert '"TF 卡升级"' not in upgrade_render
assert '"自动检查 OTA 更新"' in upgrade_render
assert 'pico_ota_inspect(' not in function("upgrade_open", SETTINGS) + upgrade_touch
assert 's_auto_ota = true;' in (ROOT / "main/settings/settings.c").read_text()
assert 's_auto_ota = nvs_get_u8(h, NVS_KEY_AUTO_OTA, &raw) != ESP_OK || raw != 0;' in function("app_settings_init", ROOT / "main/settings/settings.c")

unit = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ota_online.h"
typedef enum { APP_REDRAW_NONE, APP_REDRAW_PAGE } app_redraw_t;
typedef struct { int x, y, width, height; } EpdRect;
typedef struct { bool request_return; } app_ctx_t;
typedef struct { int type, x0, y0; } ui_gesture_event_t;
enum { SETTINGS_MAIN, SETTINGS_UPGRADE, SETTINGS_EDITOR };
enum { UI_GESTURE_TAP, UI_GESTURE_SWIPE_U, UPGRADE_JOB_NONE };
static int s_page, s_enter_page, s_upgrade_job, s_upgrade_bar_width;
static bool s_upgrade_confirm, s_upgrade_online, s_local_pending, s_upgrade_restart;
static bool s_upgrade_offer_pending, s_upgrade_shortcut_back, s_auto_ota = true;
static char s_upgrade_notice[96];
static pico_update_status_t s_update, backend;
static unsigned writes, checks, downloads, cancels, navigation;
static int saved = -1;
#define NVS_KEY_AUTO_OTA "auto_ota"
static void nvs_put_u8(const char *key, uint8_t value) { assert(!strcmp(key, "auto_ota")); saved = value; ++writes; }
static int64_t esp_timer_get_time(void) { return 42000000; }
static void upgrade_remember(uint32_t now) { assert(now == 42000); }
void pico_online_get_status(pico_update_status_t *out) { *out = backend; }
void pico_online_cancel_join(void) { ++cancels; }
static void pico_ota_auto_poll(bool idle) { assert(!idle); ++checks; }
static bool ui_rect_hit(EpdRect r, int x, int y) { return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height; }
static int ui_nav_hit(int x, int y) { (void)x; return y >= 1096 ? 0 : -1; }
static void ui_nav_request(app_ctx_t *ctx, int tab) { (void)ctx; assert(tab == 0); ++navigation; }
static void upgrade_schedule(bool downloading, uint32_t now) { assert(downloading && now == 42000); ++downloads; }
'''
for name in ("app_settings_auto_ota", "app_settings_set_auto_ota"):
    unit += function(name, ROOT / "main/settings/settings.c") + "\n"
for name in ("upgrade_confirm_button", "upgrade_open", "app_device_settings_offer_update"):
    unit += function(name, SETTINGS) + "\n"
unit += "static app_redraw_t tap(app_ctx_t *ctx,int x,int y) { ui_gesture_event_t e={UI_GESTURE_TAP,x,y}; const ui_gesture_event_t *ev=&e;" + upgrade_touch + "return APP_REDRAW_NONE;}\n"
unit += r'''
int main(void) {
    assert(app_settings_auto_ota()); app_settings_set_auto_ota(true); assert(!writes);
    app_settings_set_auto_ota(false); assert(!app_settings_auto_ota() && saved == 0 && writes == 1);
    app_settings_set_auto_ota(false); assert(writes == 1);
    app_settings_set_auto_ota(true); assert(app_settings_auto_ota() && saved == 1 && writes == 2);
    backend.state = PICO_UPDATE_AVAILABLE; strcpy(backend.release.version, "1.0.2");
    app_ctx_t ctx = {0}; s_page = SETTINGS_EDITOR;
    assert(!app_device_settings_offer_update(true) && s_page == SETTINGS_EDITOR);
    s_page = SETTINGS_MAIN; assert(app_device_settings_offer_update(true));
    assert(s_page == SETTINGS_UPGRADE && s_upgrade_confirm && s_upgrade_online && !downloads);
    assert(!app_device_settings_offer_update(true));
    assert(tap(&ctx, 150, 870) == APP_REDRAW_PAGE && !s_upgrade_confirm && !downloads && !ctx.request_return);
    assert(app_device_settings_offer_update(true));
    assert(tap(&ctx, 500, 870) == APP_REDRAW_PAGE && downloads == 1 && !s_upgrade_confirm);
    assert(app_device_settings_offer_update(false) && s_enter_page == SETTINGS_UPGRADE);
    upgrade_open(); s_upgrade_confirm = s_upgrade_online = s_upgrade_shortcut_back = true;
    assert(tap(&ctx, 150, 870) == APP_REDRAW_NONE && ctx.request_return && downloads == 1);
    ctx.request_return = false; s_upgrade_shortcut_back = false;
    unsigned old_writes = writes;
    assert(tap(&ctx, 500, 850) == APP_REDRAW_NONE && writes == old_writes && !s_upgrade_confirm);
    assert(tap(&ctx, 500, 705) == APP_REDRAW_PAGE && !app_settings_auto_ota() && writes == old_writes + 1 && checks == 1);
    assert(tap(&ctx, 500, 705) == APP_REDRAW_PAGE && app_settings_auto_ota() && checks == 2);
    s_upgrade_shortcut_back = true; assert(tap(&ctx, 50, 100) == APP_REDRAW_NONE && ctx.request_return);
    assert(tap(&ctx, 50, 1150) == APP_REDRAW_NONE && navigation == 1);
    puts("ota_auto_ui: PASS (persistent toggle, hidden TF hit area, deferred editor, one-shot offer, explicit download, later/back return)");
}
'''
with tempfile.TemporaryDirectory(prefix="ota-auto-ui-") as directory:
    work = Path(directory)
    env, _ = toolchain(work)
    (work / "test.c").write_text(unit)
    binary = work / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-Itools/ota_stubs", "-Imain/ota",
                    str(work / "test.c"), "-o", str(binary)], cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
