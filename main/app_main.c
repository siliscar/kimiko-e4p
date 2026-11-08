/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：硬件前建立持久化启动保护；深睡恢复入口先消费再开书，中断图书进入可操作恢复页。
 * User revision: persist startup protection before hardware; consume deep-sleep checkpoints before opening books, and show an operable recovery page after interrupted opening.
 * 只做开机装配：拉起板级硬件、读设置、定 VCOM、放开机图、开字体；
 * 未标定则先拦住进设定页。然后把控制权交给 app_loop。
 *
 * Boot wiring only: board init, settings, PMU RTC restore, VCOM, splash, fonts. If VCOM
 * is unset, the factory page blocks first. Then control goes to app_loop.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_loop.h"
#include "ui_builtin_image.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "lock_screen.h"
#include "lock_pin.h"
#include "ttf_font.h"
#include "book_store.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "board_sd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/stat.h>
#include "app_registry.h"
#include "display.h"
#include "epd_highlevel.h"
#include "epd_test.h"
#include "epdiy.h"
#include "esp_log.h"
#include "frontlight.h"
#include "ota_update.h"
#include "pmu_selftest.h"
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "soc/rtc_cntl_reg.h"
#endif
#include "board_hardware.h"
#include "board_crashlog.h"
#include "board_init.h"
#include "device_power.h"
#include "pmu_protocol.h"
#include "haptics.h"
#include "settings.h"
#include "sleep.h"
#include "ttf_font.h"
#include "usb_storage.h"
#include "app_font_context.h"
#include "ui_kit.h"
#include "vcom_setup.h"

static const char* TAG = "read_pico";
extern const app_desc_t app_boot_recovery;

// 软复位保留有效的 S3 RTC；断电才用 PMU 的实时秒和毫秒恢复。
// Retain a valid S3 RTC across soft reset; restore fresh PMU seconds and milliseconds after power loss.
static void restore_time_from_pmu(void) {
    struct timeval current;
    if (gettimeofday(&current, NULL) == 0 && current.tv_sec >= 1704067200 &&
        current.tv_sec <= 4102444799U) return;
    if (device_power_cmd(PMU_CMD_TIME_GET, NULL, 0) != ESP_OK) return;
    const pmu_snapshot_t *pmu = device_power_get();
    if (!pmu || !pmu->time_ok || !pmu->time_synced || pmu->unix_millis >= 1000 ||
        pmu->unix_sec < 1704067200 || pmu->unix_sec > 4102444799U) return;
    struct timeval tv = {.tv_sec = (time_t)pmu->unix_sec, .tv_usec = pmu->unix_millis * 1000};
    if (settimeofday(&tv, NULL) == 0) ESP_LOGI(TAG, "system clock restored from PMU");
}

// 已标定则开机读一次喂给 epdiy。未标定先用板级默认出开机图，随后拦住进标定页。
// Load VCOM once when set. Otherwise keep the board default for the splash,
// then gate into the setup page.
static bool resolve_vcom_at_boot(void) {
    int mv = 0;
    for (int i = 0; i < 3; i++) {
        if (device_power_vcom_get(&mv) == ESP_OK) {
            epd_set_vcom((uint16_t)mv);
            ESP_LOGI(TAG, "panel VCOM loaded from PMU");
            return true;
        }
    }
    ESP_LOGW(TAG, "panel VCOM unset, keeping board default until setup");
    return false;
}

// 压缩资源声明的原始像素长度必须匹配面板。/ The compressed resources' declared raw pixel lengths must match the panel.
static bool images_match_panel(void) {
    size_t lock_size = ui_builtin_image_size(UI_BUILTIN_IMAGE_LOCK);
    size_t loading_size = ui_builtin_image_size(UI_BUILTIN_IMAGE_LOADING);
    size_t expected = (size_t)epd_width() * epd_height() / 2;
    if (lock_size == expected && loading_size == expected) return true;
    ESP_LOGE(
        TAG, "Image size mismatch: lock %u loading %u expected %u",
        (unsigned)lock_size, (unsigned)loading_size, (unsigned)expected
    );
    return false;
}

// 字形失败落到卡根目录：设备很少接串口，把卡插到电脑上就能看到。
// Glyph failures land at the card root: this board is rarely on a cable, and the card can be
// read from any computer.
// 中文：每行带时间、开机秒数、codepoint、gid、字号、原因、PSRAM 余量和字体路径。
// English: each line carries the time, uptime, codepoint, gid, pixel size, reason, free PSRAM
// and the active face.
#define GLYPH_DIAG_PATH "/sdcard/kimiko-font.log"
static void glyph_failure_note(uint32_t codepoint, int gid, int pixel_height,
                               ttf_font_failure_t reason) {
    static unsigned logged;
    if (logged >= 8) return;
    ++logged;
    const char *why = reason == TTF_FONT_FAIL_EMPTY ? "empty" :
                      reason == TTF_FONT_FAIL_ALLOC ? "alloc" :
                      reason == TTF_FONT_FAIL_RETRY_OK ? "retry" : "read";
    rp_crash_log("glyph %04X g%d %s", (unsigned)codepoint, gid, why);
    FILE *f = fopen(GLYPH_DIAG_PATH, "a");
    if (f == NULL) return;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d up=%lus glyph=U+%04X gid=%d px=%d reason=%s "
               "free=%uKB font=%s\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
            (unsigned long)(esp_timer_get_time() / 1000000),
            (unsigned)codepoint, gid, pixel_height, why,
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024),
            ttf_font_path());
    fclose(f);
}

// 缓存验证失败时继续原开机路径，不假设未知屏幕已经全白。
// A rejected cache keeps the original startup path; never assume an unknown panel is white.
static bool restore_retained_lock(EpdiyHighlevelState *hl,uint8_t *framebuffer) {
    board_sd_info_t info;
    for(unsigned wait=0;wait<150 && board_sd_get_info(&info)==ESP_ERR_NOT_FINISHED;++wait)
        vTaskDelay(pdMS_TO_TICKS(20));
    if(!lock_screen_restore(framebuffer))return false;
    memcpy(hl->back_fb,framebuffer,(size_t)epd_width()*epd_height()/2);
    return true;
}

void app_main(void) {
    // 共享 TF 卡期间复位后，先把内部 PHY 交回串口，再清理已用完的 BOOT 请求。
    // Reclaim Serial/JTAG after a reset during card sharing, then clear the served BOOT request.
    usb_storage_phy_init();
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // S3 专用：清掉 ROM 下载模式标志，避免共享 USB PHY 后复位进下载。
    // S3 only: clear the ROM download-boot flag after sharing the USB PHY.
    REG_CLR_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#endif

    const esp_reset_reason_t reset = esp_reset_reason();
    ESP_LOGI(TAG, "startup reset=%d stack=%u", (int)reset, (unsigned)uxTaskGetStackHighWaterMark(NULL));
    app_settings_init();
    // 在大缓冲和字体之前读取小型锁屏设置；读取失败不阻塞，之后的检查仍可重试。
    // Read the tiny lock setting before large buffers/fonts; keep failures nonblocking and let subsequent checks retry.
    (void)lock_pin_enabled();
    pico_boot_init(reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
                   reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT);

    board_handle_t hw;
    if (board_init(&hw) != ESP_OK) return;
    // 马达能力与定时器在板级就绪后建立；开关/强度已由设置初始化写入。
    // / Set up the motor timer once the board is ready; settings init already applied switch and intensity.
    haptics_init();
    // 电量校准日志按设置启动；默认关闭时不创建 batt.log，也不打印 batt 行。
    // Start the battery calibration log per the setting; the off default creates no batt.log
    // and prints no batt line.
    device_power_set_batt_log(app_settings_batt_log());
    // 前光在板级初始化里已挂好 LEDC；这里按 NVS 恢复用户状态，早于首帧。
    // The board step already wired LEDC; restore the user state before the first frame.
    const bool fl_on = app_settings_frontlight_on();
    const uint8_t fl_bri = app_settings_frontlight_brightness();
    const uint8_t fl_cct = app_settings_frontlight_cct();
    esp_err_t fl_err = frontlight_apply(fl_on, fl_bri, fl_cct);
    if (fl_err != ESP_OK && fl_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "frontlight restore %s", esp_err_to_name(fl_err));
    } else if (fl_err == ESP_OK) {
        ESP_LOGI(TAG, "frontlight on=%d brightness=%u cct=%u", (int)fl_on,
                 (unsigned)fl_bri, (unsigned)fl_cct);
    }
    rp_boot_mark("hw");
    if (hw.pmu_ready) restore_time_from_pmu();
    const bool vcom_ok = resolve_vcom_at_boot();
    rp_boot_mark("vcom");
    pmu_selftest_bind(hw.sensor);
    app_sleep_bind(hw.touch, hw.sensor);

#if CONFIG_KIMIKO_EPD_TEST_PATTERN
    // bring-up：先验证面板/总线/电源，再进正常 UI。
    // Bring-up: validate panel, bus and rails before entering the normal UI.
    epd_test_run(&hw.hl, hw.framebuffer);
#endif

    if (!images_match_panel()) return;

    EpdiyHighlevelState hl = hw.hl;
    uint8_t* framebuffer = hw.framebuffer;

    // 深睡保留屏幕已有画面：验证缓存后同时恢复前后帧，省去清白和开机图两次推屏。
    // Deep sleep retains the panel image: validate its cache and restore both references, skipping white erase and splash output.
    const bool resuming = pico_boot_has_resume();
    bool restored_lock = false;
    if(resuming && vcom_ok && !lock_pin_enabled() && pico_boot_resume_frame_cached()) {
        restored_lock=restore_retained_lock(&hl,framebuffer);
    }
    if(!restored_lock) {
        epd_poweron();
        if(!resuming)board_i2c_census_take();
        epd_clear();
        epd_hl_set_all_white(&hl);
        (void)ui_builtin_image_draw(framebuffer, UI_BUILTIN_IMAGE_LOADING);
        update_display_from_white(&hl);
    }
    rp_boot_mark("splash");

    ttf_font_set_failure_hook(glyph_failure_note);
    // 直接续读不先加载整份系统字体；密码验证仍先使用所选系统字体。
    // Direct reader resume skips loading the whole system face first; PIN authentication still uses the selected UI face.
    if(!resuming || lock_pin_enabled())app_font_activate_system();
    else (void)ttf_font_open_builtin();
    rp_boot_mark("font");

    if (!hw.touch_ready) {
        ESP_LOGE(TAG, "No touch controller, UI cannot run");
        return;
    }

    if (hw.pmu_ready && !vcom_ok) {
        vcom_setup_run(&hl, framebuffer, hw.touch);
    }

    // 硬件、显示、触摸与必要的出厂设置均成功后确认新槽。
    // Confirm a new slot only after hardware, display, touch, and required setup succeed.
    pico_ota_confirm_running();

    app_lock_boot_gate(&hl, hw.touch, hw.sensor);
    rp_boot_mark("lock");
    const app_desc_t *first = app_home_page();
    pico_resume_t resume = {0};
    if (pico_boot_take_resume(&resume)) {
        if (!resume.reader) first = app_at(resume.tab);
        else {
            // 挂载等待有界；恢复入口已消费，无卡或资源失败不会反复自动开书。
            // Bound mount waiting after consuming the checkpoint; absent media or failed resources cannot auto-open in a loop.
            if (!strncmp(resume.path, "/sdcard/", 8)) {
                board_sd_info_t info;
                for (unsigned wait = 0; wait < 150 && board_sd_get_info(&info) == ESP_ERR_NOT_FINISHED; ++wait)
                    vTaskDelay(pdMS_TO_TICKS(20));
            }
            if (!strncmp(resume.path, "/flash/books/", 13) && !book_store_flash_ready()) {
                book_store_root_t roots[BOOK_STORE_ROOT_MAX]; int count;
                (void)book_store_roots(roots, &count);
            }
            struct stat st;
            if (!stat(resume.path, &st) && S_ISREG(st.st_mode) &&
                app_book_request_resume(resume.path, resume.fullscreen)) first = app_at(1);
        }
    }
    char interrupted[PICO_BOOT_PATH_MAX];
    if (pico_boot_interrupted_book(interrupted, sizeof(interrupted))) first = &app_boot_recovery;
    if(resuming && !lock_pin_enabled()) {
        if(first==app_at(1) && resume.reader)app_font_activate_reading();
        else app_font_activate_system();
    }
    rp_boot_mark("resume");
    // 定时关机后的恢复也是解锁转场，首张目标页使用完整48相。
    // Resume after timed power-off is also an unlock transition; use complete 48-phase GC16 for its first target frame.
    if (resuming) display_request_full_refresh();
    app_loop_run(&(app_loop_config_t){
        .hl = &hl,
        .fb = framebuffer,
        .acc = hw.sensor,
        .tp = hw.touch,
        .sensor_ready = hw.sensor_ready,
        // 用户修订：深睡一次性恢复上次阅读或主页面；异常启动安全回首页。
        // User revision: resume the last reader/main tab once after deep sleep; abnormal starts safely enter Home.
        .first_app = first,
    });
}
