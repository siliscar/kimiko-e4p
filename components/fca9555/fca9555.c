/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * TCA9555 主机适配：锁内 STOP 分段读取；两口 Input 分别清中断。
 *
 * TCA9555 host adaptation: locked STOP-separated reads; clear each port through its Input.
 *
 * S31 使用 TCA9555；事务参考 Metalio 原厂 bus/pca9555.c。
 * S31 uses TCA9555; transactions follow Metalio vendor bus/pca9555.c.
 * 原厂来源版本和 MIT 许可见 components/board_i2c/README.md 与 LICENSE.Metalio。
 * Vendor revision and MIT notice: components/board_i2c/README.md and LICENSE.Metalio.
 */

#include "fca9555.h"

#include <stdlib.h>

#include "esp_log.h"
#include "board_i2c.h"
#include "sdkconfig.h"

#ifndef CONFIG_FCA9555_I2C_TIMEOUT_MS
#define CONFIG_FCA9555_I2C_TIMEOUT_MS FCA9555_I2C_TIMEOUT_MS_DEFAULT
#endif

static const char* TAG = "fca9555";

struct fca9555_dev_t {
    i2c_master_dev_handle_t dev;
    gpio_num_t int_gpio;
    int timeout_ms;
    uint16_t invert;
    uint16_t config;
    bool dir_cached;
};

static bool gpio_ok(gpio_num_t gpio) {
    return gpio != GPIO_NUM_NC && GPIO_IS_VALID_GPIO(gpio);
}

static bool port_ok(int port) {
    return port == 0 || port == 1;
}

static void cache_dir(fca9555_handle_t h, uint16_t invert, uint16_t config) {
    h->invert = invert;
    h->config = config;
    h->dir_cached = true;
}

static void cache_port(uint16_t* word, int port, uint8_t value) {
    if (port) {
        *word = (uint16_t)((*word & 0x00FF) | ((uint16_t)value << 8));
    } else {
        *word = (uint16_t)((*word & 0xFF00) | value);
    }
}

static esp_err_t cfg_int_gpio(gpio_num_t gpio) {
    if (!gpio_ok(gpio)) return ESP_OK;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static esp_err_t xfer_read(fca9555_handle_t h, uint8_t start, uint8_t* data, size_t n) {
    return board_i2c_read_stop(h->dev, start, data, n, h->timeout_ms);
}

static esp_err_t xfer_write(fca9555_handle_t h, uint8_t reg, uint8_t value) {
    uint8_t data[2] = { reg, value };
    return board_i2c_transmit(h->dev, data, sizeof(data), h->timeout_ms);
}

esp_err_t fca9555_init(i2c_master_bus_handle_t bus_handle, const fca9555_config_t* config,
                       fca9555_handle_t* handle) {
    if (bus_handle == NULL || config == NULL || handle == NULL) return ESP_ERR_INVALID_ARG;

    fca9555_handle_t h = calloc(1, sizeof(*h));
    if (h == NULL) return ESP_ERR_NO_MEM;

    h->int_gpio = config->int_gpio;
    h->timeout_ms = config->i2c_timeout_ms ? (int)config->i2c_timeout_ms
                                       : CONFIG_FCA9555_I2C_TIMEOUT_MS;
    h->config = 0xFFFF;
    h->invert = 0;

    esp_err_t err = cfg_int_gpio(h->int_gpio);
    if (err != ESP_OK) {
        free(h);
        return err;
    }

    err = board_i2c_add_device(bus_handle,
                              config->i2c_addr ? config->i2c_addr : FCA9555_ADDR_DEFAULT,
                              &h->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C add failed: %s", esp_err_to_name(err));
        free(h);
        return err;
    }

    *handle = h;
    return ESP_OK;
}

esp_err_t fca9555_deinit(fca9555_handle_t h) {
    if (h == NULL) return ESP_OK;
    if (h->dev) i2c_master_bus_rm_device(h->dev);
    free(h);
    return ESP_OK;
}

esp_err_t fca9555_read_reg(fca9555_handle_t h, uint8_t reg, uint8_t* value) {
    if (h == NULL || value == NULL || reg > FCA9555_REG_CFG1) {
        return ESP_ERR_INVALID_ARG;
    }
    return xfer_read(h, reg, value, 1);
}

esp_err_t fca9555_read_n(fca9555_handle_t h, uint8_t start, uint8_t* data, size_t n) {
    if (h == NULL || data == NULL || n == 0 || start > FCA9555_REG_CFG1 ||
        n > (size_t)(FCA9555_REG_CFG1 - start + 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t snapshot[8];
    esp_err_t err = board_i2c_lock(h->timeout_ms);
    if (err != ESP_OK) return err;
    for (size_t i = 0; i < n; ++i) {
        err = xfer_read(h, (uint8_t)(start + i), &snapshot[i], 1);
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) for (size_t i = 0; i < n; ++i) data[i] = snapshot[i];
    board_i2c_unlock();
    return err;
}

esp_err_t fca9555_read_all(fca9555_handle_t h, fca9555_map_t* map) {
    if (h == NULL || map == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t data[8];
    esp_err_t err = board_i2c_lock(h->timeout_ms);
    if (err != ESP_OK) return err;
    // TCA 连读仅在寄存器对内翻转；逐寄存器读取才是完整映射。
    // TCA sequential reads alternate within a pair; address each register for the full map.
    for (uint8_t reg = 0; reg < sizeof(data); ++reg) {
        err = xfer_read(h, reg, &data[reg], 1);
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) {
        map->input = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
        map->output = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
        map->invert = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
        map->config = (uint16_t)(data[6] | ((uint16_t)data[7] << 8));
        cache_dir(h, map->invert, map->config);
    }
    board_i2c_unlock();
    return err;
}

esp_err_t fca9555_read_io(fca9555_handle_t h, fca9555_map_t* map) {
    if (h == NULL || map == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t data[4];
    esp_err_t err = board_i2c_lock(h->timeout_ms);
    if (err != ESP_OK) return err;
    for (uint8_t reg = 0; reg < sizeof(data); ++reg) {
        err = xfer_read(h, reg, &data[reg], 1);
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) {
        map->input = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
        map->output = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
        map->invert = h->invert;
        map->config = h->config;
    }
    board_i2c_unlock();
    return err;
}

esp_err_t fca9555_read_input(fca9555_handle_t h, int port, uint8_t* value) {
    if (h == NULL || value == NULL || !port_ok(port)) return ESP_ERR_INVALID_ARG;
    return xfer_read(h, (uint8_t)(FCA9555_REG_IN0 + port), value, 1);
}

esp_err_t fca9555_set_output(fca9555_handle_t h, int port, uint8_t value) {
    if (h == NULL || !port_ok(port)) return ESP_ERR_INVALID_ARG;
    return xfer_write(h, (uint8_t)(FCA9555_REG_OUT0 + port), value);
}

esp_err_t fca9555_clear_int(fca9555_handle_t h) {
    uint8_t value = 0;
    esp_err_t first = fca9555_read_input(h, 0, &value);
    esp_err_t second = fca9555_read_input(h, 1, &value);
    return first != ESP_OK ? first : second;
}

int fca9555_int_level(fca9555_handle_t h) {
    if (h == NULL || !gpio_ok(h->int_gpio)) return 1;
    return gpio_get_level(h->int_gpio);
}

bool fca9555_int_asserted(fca9555_handle_t h) {
    return fca9555_int_level(h) == 0;
}

gpio_num_t fca9555_int_gpio(fca9555_handle_t h) {
    return h ? h->int_gpio : GPIO_NUM_NC;
}

bool fca9555_dir_cached(fca9555_handle_t h) {
    return h != NULL && h->dir_cached;
}

void fca9555_get_dir_cache(fca9555_handle_t h, uint16_t* invert, uint16_t* config) {
    if (h == NULL) return;
    if (invert) *invert = h->invert;
    if (config) *config = h->config;
}

esp_err_t fca9555_set_config(fca9555_handle_t h, int port, uint8_t value) {
    if (h == NULL || !port_ok(port)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = xfer_write(h, (uint8_t)(FCA9555_REG_CFG0 + port), value);
    if (err == ESP_OK) {
        cache_port(&h->config, port, value);
        h->dir_cached = true;
    }
    return err;
}

esp_err_t fca9555_set_inversion(fca9555_handle_t h, int port, uint8_t value) {
    if (h == NULL || !port_ok(port)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = xfer_write(h, (uint8_t)(FCA9555_REG_INV0 + port), value);
    if (err == ESP_OK) {
        cache_port(&h->invert, port, value);
        h->dir_cached = true;
    }
    return err;
}
