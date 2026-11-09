/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * CST836U 主机：I2C 读点、动态上报 / 深睡、RST 唤醒。
 *
 * CST836U host: I2C point read, dynamic report / deep sleep, RST wake.
 *
 * S31 触摸复位、模式与坐标参考 Metalio 原厂 display/metalio_touch.c。
 * S31 touch reset, mode and coordinates follow Metalio vendor display/metalio_touch.c.
 * 原厂来源版本和 MIT 许可见 components/board_i2c/README.md 与 LICENSE.Metalio。
 * Vendor revision and MIT notice: components/board_i2c/README.md and LICENSE.Metalio.
 */

#include "cst836u.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "board_i2c.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define CST836U_REG_TOUCH_DATA 0x00
#define CST836U_REG_INFO 0xA6

#define CST836U_CMD_NORMAL 0xFE00
#define CST836U_CMD_DEEPSLEEP 0xA503

#if defined(CONFIG_IDF_TARGET_ESP32S31)
// FT6336U：0x02 起 1 字节状态 + 两个触点各 6 字节。
// FT6336U: status byte at 0x02 followed by 6 bytes per touch point.
#define FT6336U_REG_TD_STATUS 0x02
#define FT6336U_REG_CHIP_ID 0xA3
#define FT6336U_REG_FW_VER 0xA6
#define FT6336U_REG_INT_MODE 0xA4
#define FT6336U_CHIP_ID 0x64
#define FT6336U_FRAME_LEN 13

#ifndef CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT
#define CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT 100
#endif

// 盖板三键在屏外：FT 原始 y≈1600，x≈100/200/300。
// Cover keys sit outside the panel: raw y≈1600, x≈100/200/300.
#define FT6336U_VK_RAW_Y 1600
#define FT6336U_VK_TOL 30
#define FT6336U_VK_X_BASE 100
#define FT6336U_VK_PITCH 100
#define FT6336U_LOGICAL_W 684
#define FT6336U_LOGICAL_H 1216
#endif

#define I2C_TIMEOUT_MS 100
#define RST_HOLD_MS 10
#define RST_BOOT_MS 50

static const char* TAG = "cst836u";

struct cst836u_dev_t {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    gpio_num_t int_gpio;
    gpio_num_t rst_gpio;
    esp_err_t (*reset_fn)(void);
    cst836u_info_t info;
    cst836u_mode_t mode;
    bool info_ready;
};

static bool gpio_ok(gpio_num_t gpio) {
    return gpio != GPIO_NUM_NC && GPIO_IS_VALID_GPIO(gpio);
}

static esp_err_t read_regs(cst836u_handle_t h, uint8_t reg, uint8_t* data, size_t length) {
    return board_i2c_read(h->dev, reg, data, length, I2C_TIMEOUT_MS);
}

#if !defined(CONFIG_IDF_TARGET_ESP32S31)
static esp_err_t write_cmd(cst836u_handle_t h, uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return board_i2c_transmit(h->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}
#endif

static esp_err_t int_as_input(cst836u_handle_t h) {
    if (!gpio_ok(h->int_gpio)) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->int_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static esp_err_t rst_as_output(cst836u_handle_t h) {
    if (!gpio_ok(h->rst_gpio)) return ESP_ERR_INVALID_ARG;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << h->rst_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    return gpio_set_level(h->rst_gpio, 1);
}

static esp_err_t load_info(cst836u_handle_t h) {
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    uint8_t chip = 0;
    esp_err_t err = read_regs(h, FT6336U_REG_CHIP_ID, &chip, sizeof(chip));
    if (err != ESP_OK) return err;
    if (chip != FT6336U_CHIP_ID) {
        ESP_LOGW(TAG, "unexpected FT6336U chip id 0x%02X (want 0x%02X)", chip, FT6336U_CHIP_ID);
    }
    uint8_t fw = 0;
    if (read_regs(h, FT6336U_REG_FW_VER, &fw, sizeof(fw)) != ESP_OK) fw = 0;
    h->info.fw = fw;
    h->info.id = chip;
    h->info.type = 0x6336;
    h->info_ready = true;
    return ESP_OK;
#else
    uint8_t info[6] = { 0 };
    esp_err_t err = read_regs(h, CST836U_REG_INFO, info, sizeof(info));
    if (err != ESP_OK) return err;
    h->info.fw = info[0];
    h->info.id = info[1];
    h->info.module = info[2];
    h->info.project = info[3];
    h->info.type = (uint16_t)info[5] << 8 | info[4];
    h->info_ready = true;
    return ESP_OK;
#endif
}

#if defined(CONFIG_IDF_TARGET_ESP32S31)
static esp_err_t ft6336u_apply_threshold(cst836u_handle_t h) {
#if CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT < 100
    // 仅调正常模式 0x80；0xA9 属于量产测试，不是三键日常配置。
    // Tune only normal-mode 0x80; 0xA9 is a production-test register, not a runtime key setting.
    uint8_t mode = 0, chip_mid = 0, chip_low = 0;
    if (h->info.id != FT6336U_CHIP_ID ||
        read_regs(h, 0x00, &mode, 1) != ESP_OK || (mode & 0x70) != 0 ||
        read_regs(h, 0x9F, &chip_mid, 1) != ESP_OK || chip_mid != 0x26 ||
        read_regs(h, 0xA0, &chip_low, 1) != ESP_OK || chip_low != 0x02) {
        ESP_LOGW(TAG, "FT6336U threshold tuning skipped: chip/mode not confirmed");
        return ESP_OK;
    }

    uint8_t original = 0;
    esp_err_t err = read_regs(h, 0x80, &original, 1);
    if (err != ESP_OK || original == 0) {
        ESP_LOGW(TAG, "FT6336U threshold tuning skipped: reset threshold unavailable");
        return ESP_OK;
    }
    // 每次紧随硬复位，用模组实际值向上取整，避免唤醒叠加或写零。
    // Always follow a hardware reset; round the module value up to avoid compounded wakes or zero.
    const uint8_t tuned = (original * CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT + 99) / 100;
    if (tuned == original) return ESP_OK;

    uint8_t command[] = {0x80, tuned};
    err = board_i2c_transmit(h->dev, command, sizeof(command), I2C_TIMEOUT_MS);
    uint8_t observed = 0;
    esp_err_t check = read_regs(h, 0x80, &observed, 1);
    if (err == ESP_OK && check == ESP_OK && observed == tuned) {
        ESP_LOGI(TAG, "FT6336U threshold 0x80: %u -> %u (%d%%, experimental)",
                 original, tuned, CONFIG_TOUCH_FT6336U_THRESHOLD_PERCENT);
        return ESP_OK;
    }

    // I2C 写失败也可能已生效；恢复必须读回确认，不能只看写返回值。
    // A failed I2C write may take effect; verify rollback by reading, not just its write result.
    command[1] = original;
    err = board_i2c_transmit(h->dev, command, sizeof(command), I2C_TIMEOUT_MS);
    check = read_regs(h, 0x80, &observed, 1);
    if (check != ESP_OK || observed != original) {
        ESP_LOGE(TAG, "FT6336U threshold rollback unconfirmed: write=%s read=%s value=%u want=%u",
                 esp_err_to_name(err), esp_err_to_name(check), observed, original);
        return check != ESP_OK ? check : ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGW(TAG, "FT6336U threshold tuning rejected; restored %u", original);
#else
    (void)h;
#endif
    return ESP_OK;
}
#endif

esp_err_t cst836u_init(i2c_master_bus_handle_t bus_handle, const cst836u_config_t* config,
                       cst836u_handle_t* handle) {
    if (bus_handle == NULL || config == NULL || handle == NULL) return ESP_ERR_INVALID_ARG;

    cst836u_handle_t h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;

    h->bus = bus_handle;
    h->int_gpio = config->int_gpio;
    h->rst_gpio = config->rst_gpio;
    h->reset_fn = config->reset_fn;
    h->mode = CST836U_MODE_NORMAL;

    uint8_t addr = config->i2c_addr ? config->i2c_addr : CST836U_ADDR_DEFAULT;
    esp_err_t err = board_i2c_add_device(bus_handle, addr, &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C device add failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    if (gpio_ok(h->rst_gpio)) {
        err = rst_as_output(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "RST GPIO%d failed: %s", (int)h->rst_gpio, esp_err_to_name(err));
        }
    }

#if defined(CONFIG_IDF_TARGET_ESP32S31)
    err = cst836u_reset(h);
    if (err == ESP_OK) {
        const uint8_t command[] = {FT6336U_REG_INT_MODE, 0};
        err = board_i2c_transmit(h->dev, command, sizeof(command), I2C_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return err;
    }
#endif
    err = load_info(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Chip information read failed: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return err;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S31)
    err = ft6336u_apply_threshold(h);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(h->dev);
        free(h);
        return err;
    }
#endif

    if (gpio_ok(h->int_gpio)) {
        err = int_as_input(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INT GPIO%d input failed: %s", (int)h->int_gpio, esp_err_to_name(err));
        }
    }

    ESP_LOGI(
        TAG,
        "Ready: FW=0x%02X, ID=0x%02X, module=0x%02X, project=0x%02X, type=0x%04X",
        h->info.fw, h->info.id, h->info.module, h->info.project, h->info.type
    );
    *handle = h;
    return ESP_OK;
}

esp_err_t cst836u_deinit(cst836u_handle_t h) {
    if (h == NULL) return ESP_OK;
    if (h->dev) {
        i2c_master_bus_rm_device(h->dev);
    }
    free(h);
    return ESP_OK;
}

esp_err_t cst836u_get_info(cst836u_handle_t h, cst836u_info_t* info) {
    if (h == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    if (!h->info_ready) return ESP_ERR_INVALID_STATE;
    *info = h->info;
    return ESP_OK;
}

#if defined(CONFIG_IDF_TARGET_ESP32S31)
// FT 原始坐标 → Kimiko 逻辑坐标；盖板三键归一化到 x=80/240/400、y=1500。
// FT raw coordinates -> Kimiko logical coordinates; cover keys normalise to x=80/240/400, y=1500.
static bool ft6336u_map_point(int raw_x, int raw_y, uint16_t* lx, uint16_t* ly) {
    if (raw_y >= FT6336U_VK_RAW_Y - FT6336U_VK_TOL &&
        raw_y <= FT6336U_VK_RAW_Y + FT6336U_VK_TOL) {
        static const uint16_t key_x[] = {240, 80, 400};
        for (int i = 0; i < 3; ++i) {
            const int center = FT6336U_VK_X_BASE + i * FT6336U_VK_PITCH;
            if (raw_x >= center - FT6336U_VK_TOL && raw_x <= center + FT6336U_VK_TOL) {
                *lx = key_x[i];
                *ly = 1500;
                return true;
            }
        }
        return false;
    }
    int x = raw_x, y = raw_y;
    // 原厂横屏坐标转换；屏外非盖板区域不生成触摸。
    // Vendor landscape-coordinate mapping; off-panel non-key samples produce no touch.
    if (raw_x >= FT6336U_LOGICAL_W && raw_x < FT6336U_LOGICAL_H &&
        raw_y < FT6336U_LOGICAL_W) {
        x = FT6336U_LOGICAL_W - 1 - raw_y;
        y = raw_x;
    }
    if (x < 0 || x >= FT6336U_LOGICAL_W || y < 0 || y >= FT6336U_LOGICAL_H) return false;
    *lx = (uint16_t)x;
    *ly = (uint16_t)y;
    return true;
}

static esp_err_t ft6336u_read(cst836u_handle_t h, cst836u_touch_t* touch) {
    uint8_t data[FT6336U_FRAME_LEN] = { 0 };
    esp_err_t err = read_regs(h, FT6336U_REG_TD_STATUS, data, sizeof(data));
    if (err != ESP_OK) {
        static unsigned s_err_n;
        if ((s_err_n++ % 200) == 0) {
            ESP_LOGW(TAG, "FT6336U read failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    memset(touch, 0, sizeof(*touch));
    memcpy(touch->raw, data, CST836U_RAW_LEN < sizeof(data) ? CST836U_RAW_LEN : sizeof(data));
    uint8_t n = data[0] & 0x0F;
    if (n > CST836U_MAX_POINTS) return ESP_OK;
    touch->count = n;

    // 触摸诊断：按下/移动/抬起时打一行原始与映射坐标。
    // Touch diagnostics: one line on down/move/up with raw and mapped coordinates.
    static uint8_t s_dbg_n;
    static int s_dbg_x = -1;
    static int s_dbg_y = -1;
    const int dbg_raw_x = ((data[1] & 0x0F) << 8) | data[2];
    const int dbg_raw_y = ((data[3] & 0x0F) << 8) | data[4];
    if (n > 0) {
        if (n != s_dbg_n || dbg_raw_x != s_dbg_x || dbg_raw_y != s_dbg_y) {
            uint16_t lx = 0;
            uint16_t ly = 0;
            ft6336u_map_point(dbg_raw_x, dbg_raw_y, &lx, &ly);
            ESP_LOGI(TAG, "FT n=%u ev=%u raw=(%d,%d) -> logical=(%u,%u) regs=%02X %02X %02X %02X",
                     n, (data[1] >> 6) & 3, dbg_raw_x, dbg_raw_y, lx, ly, data[0], data[1],
                     data[2], data[3]);
        }
        s_dbg_n = n;
        s_dbg_x = dbg_raw_x;
        s_dbg_y = dbg_raw_y;
    } else if (s_dbg_n > 0) {
        ESP_LOGI(TAG, "FT up (TD=0x%02X)", data[0]);
        s_dbg_n = 0;
        s_dbg_x = -1;
        s_dbg_y = -1;
    }

    uint8_t active_n = 0;
    bool have_primary = false;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        cst836u_point_t* pt = &touch->points[i];
        const uint8_t* src = &data[1 + i * 6];
        pt->id = (uint8_t)(src[2] >> 4);
        pt->event = (uint8_t)((src[0] >> 6) & 0x03);
        const int raw_x = ((src[0] & 0x0F) << 8) | src[1];
        const int raw_y = ((src[2] & 0x0F) << 8) | src[3];
        if ((uint8_t)i >= n || pt->event == 3) {
            pt->active = false;
            continue;
        }
        const bool mapped = ft6336u_map_point(raw_x, raw_y, &pt->x, &pt->y);
        pt->active = mapped && pt->event != 1;
        if (!mapped) continue;
        if (!have_primary) {
            touch->x = pt->x;
            touch->y = pt->y;
            have_primary = true;
        }
        if (pt->active) active_n++;
    }
    touch->touched = active_n != 0;
    return ESP_OK;
}
#endif

esp_err_t cst836u_read(cst836u_handle_t h, cst836u_touch_t* touch) {
    if (h == NULL || touch == NULL) return ESP_ERR_INVALID_ARG;
    if (h->dev == NULL) return ESP_ERR_INVALID_STATE;
    // 深睡后不再应答 I2C。这里直接回空点，主循环才能继续走 on_tick 做超时复位。
    // / Deep sleep ignores I2C. Return empty points so the loop can still
    // run on_tick and time out a reset.
    if (h->mode == CST836U_MODE_DEEPSLEEP) {
        memset(touch, 0, sizeof(*touch));
        return ESP_OK;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S31)
    return ft6336u_read(h, touch);
#else

    uint8_t data[CST836U_RAW_LEN] = { 0 };
    esp_err_t err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
        err = read_regs(h, CST836U_REG_TOUCH_DATA, data, sizeof(data));
        if (err != ESP_OK) return err;
        if (data[2] < 3) {
            err = ESP_OK;
            break;
        }
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) {
#if CONFIG_TOUCH_DIAG
        // 计数字节连续 3 次无效：留下原始寄存器，判断是噪声还是固件异常。
        // Count byte invalid three times in a row: keep raw regs to tell noise from firmware faults.
        ESP_LOGI(TAG, "TD RAW_INVALID n=0x%02X raw=%02X %02X %02X %02X %02X %02X %02X %02X",
                 data[2], data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
#endif
        return err;
    }

    memset(touch, 0, sizeof(*touch));
    memcpy(touch->raw, data, sizeof(data));
    touch->count = data[2] & 0x0F;

    uint8_t active_n = 0;
    bool have_primary = false;
    for (int i = 0; i < CST836U_MAX_POINTS; i++) {
        const uint8_t* src = &data[3 + i * 6];
        cst836u_point_t* pt = &touch->points[i];
        uint8_t event = src[0] >> 6;
        uint8_t id = src[2] >> 4;
        pt->event = event;
        pt->id = id;
        pt->x = (uint16_t)(src[0] & 0x0F) << 8 | src[1];
        pt->y = (uint16_t)(src[2] & 0x0F) << 8 | src[3];
        if (id > 1 || event == 3) {
            pt->active = false;
            continue;
        }
        pt->active = event != 1;
        if (!have_primary) {
            touch->x = pt->x;
            touch->y = pt->y;
            have_primary = true;
        }
        if (pt->active) active_n++;
    }
    touch->touched = active_n != 0;
    return ESP_OK;
#endif
}

esp_err_t cst836u_set_mode(cst836u_handle_t h, cst836u_mode_t mode) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    // 首版保持 FT6336U 供电，用 RST（TCA P12）唤醒；只记录软件模式。
    // Keep the FT6336U powered for now and wake it through RST (TCA P12); track the mode in software.
    if (mode != CST836U_MODE_NORMAL && mode != CST836U_MODE_DEEPSLEEP) {
        return ESP_ERR_INVALID_ARG;
    }
    h->mode = mode;
    ESP_LOGI(TAG, "Work mode %s (S31 keeps touch powered)", cst836u_mode_name(mode));
    return ESP_OK;
#else
    uint16_t cmd;
    switch (mode) {
        case CST836U_MODE_NORMAL: cmd = CST836U_CMD_NORMAL; break;
        case CST836U_MODE_DEEPSLEEP: cmd = CST836U_CMD_DEEPSLEEP; break;
        default: return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = write_cmd(h, cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mode %d command failed: %s", mode, esp_err_to_name(err));
        return err;
    }
    h->mode = mode;
    ESP_LOGI(TAG, "Work mode %s (command 0x%04X)", cst836u_mode_name(mode), cmd);
    return ESP_OK;
#endif
}

cst836u_mode_t cst836u_get_mode(cst836u_handle_t h) {
    return h == NULL ? CST836U_MODE_NORMAL : h->mode;
}

esp_err_t cst836u_reset(cst836u_handle_t h) {
    if (h == NULL) return ESP_ERR_INVALID_ARG;
    if (gpio_ok(h->rst_gpio)) {
        gpio_set_level(h->rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(RST_HOLD_MS));
        gpio_set_level(h->rst_gpio, 1);
#if defined(CONFIG_IDF_TARGET_ESP32S31)
        vTaskDelay(pdMS_TO_TICKS(300));
#endif
        return ESP_OK;
    }
    if (h->reset_fn != NULL) return h->reset_fn();
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t cst836u_wake(cst836u_handle_t h) {
    if (h == NULL || h->dev == NULL) return ESP_ERR_INVALID_STATE;
    esp_err_t err = cst836u_reset(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch reset failed: %s", esp_err_to_name(err));
        return err;
    }
#if defined(CONFIG_IDF_TARGET_ESP32S31)
    const uint8_t command[] = {FT6336U_REG_INT_MODE, 0};
    err = board_i2c_transmit(h->dev, command, sizeof(command), I2C_TIMEOUT_MS);
    if (err != ESP_OK) return err;
    err = load_info(h);
    if (err != ESP_OK) return err;
    err = ft6336u_apply_threshold(h);
    if (err != ESP_OK) {
        h->mode = CST836U_MODE_DEEPSLEEP;
        return err;
    }
#else
    vTaskDelay(pdMS_TO_TICKS(RST_BOOT_MS));
#endif
    h->mode = CST836U_MODE_NORMAL;
    return cst836u_set_mode(h, CST836U_MODE_NORMAL);
}

bool cst836u_int_asserted(cst836u_handle_t h) {
    return h != NULL && gpio_ok(h->int_gpio) && gpio_get_level(h->int_gpio) == 0;
}

int cst836u_int_level(cst836u_handle_t h) {
    if (h == NULL || !gpio_ok(h->int_gpio)) return 1;
    return gpio_get_level(h->int_gpio);
}

gpio_num_t cst836u_int_gpio(cst836u_handle_t h) {
    return h == NULL ? GPIO_NUM_NC : h->int_gpio;
}

const char* cst836u_mode_name(cst836u_mode_t mode) {
    switch (mode) {
        case CST836U_MODE_NORMAL: return "normal";
        case CST836U_MODE_DEEPSLEEP: return "deepsleep";
        default: return "unknown";
    }
}

const char* cst836u_event_name(uint8_t event) {
    switch (event) {
        case 0: return "down";
        case 1: return "up";
        case 2: return "move";
        default: return "---";
    }
}
