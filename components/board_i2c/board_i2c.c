/*
 * 原厂 Metalio 总线事务的句柄适配；来源与 MIT 全文见 LICENSE.Metalio。
 * Handle adaptation of the vendor Metalio bus transactions; see LICENSE.Metalio.
 * 保留调用方设备生命周期，不创建额外设备缓存。
 * Retains driver-owned device lifetimes without another device cache.
 */
#include "board_i2c.h"

#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t board_i2c_lock(int timeout_ms) {
    portENTER_CRITICAL(&s_init_mux);
    if (s_lock == NULL) s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_storage);
    portEXIT_CRITICAL(&s_init_mux);
    if (s_lock == NULL) return ESP_ERR_NO_MEM;
    const TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lock, ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void board_i2c_unlock(void) { xSemaphoreGiveRecursive(s_lock); }

esp_err_t board_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t address,
                               i2c_master_dev_handle_t* device) {
    if (bus == NULL || device == NULL || address > 0x7f) return ESP_ERR_INVALID_ARG;
    *device = NULL;
    esp_err_t err = board_i2c_lock(100);
    if (err != ESP_OK) return err;
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = BOARD_I2C_HZ,
        .scl_wait_us = BOARD_I2C_SCL_WAIT_US,
    };
    err = i2c_master_bus_add_device(bus, &config, device);
    board_i2c_unlock();
    return err;
}

esp_err_t board_i2c_probe(i2c_master_bus_handle_t bus, uint8_t address, int timeout_ms) {
    if (bus == NULL || address > 0x7f) return ESP_ERR_INVALID_ARG;
    esp_err_t err = board_i2c_lock(timeout_ms);
    if (err != ESP_OK) return err;
    err = i2c_master_probe(bus, address, timeout_ms);
    board_i2c_unlock();
    return err;
}

esp_err_t board_i2c_read_stop(i2c_master_dev_handle_t device, uint8_t reg,
                             uint8_t* data, size_t length, int timeout_ms) {
    if (device == NULL || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = board_i2c_lock(timeout_ms);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit(device, &reg, 1, timeout_ms);
    if (err == ESP_OK) err = i2c_master_receive(device, data, length, timeout_ms);
    board_i2c_unlock();
    return err;
}

esp_err_t board_i2c_read(i2c_master_dev_handle_t device, uint8_t reg,
                        uint8_t* data, size_t length, int timeout_ms) {
    if (device == NULL || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = board_i2c_lock(timeout_ms);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit_receive(device, &reg, 1, data, length, timeout_ms);
    if (err != ESP_OK) {
        esp_rom_delay_us(100);
        err = i2c_master_transmit(device, &reg, 1, timeout_ms);
        if (err == ESP_OK) {
            esp_rom_delay_us(100);
            err = i2c_master_receive(device, data, length, timeout_ms);
        }
    }
    board_i2c_unlock();
    return err;
}

esp_err_t board_i2c_transmit(i2c_master_dev_handle_t device, const uint8_t* data,
                            size_t length, int timeout_ms) {
    if (device == NULL || data == NULL || length == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = board_i2c_lock(timeout_ms);
    if (err != ESP_OK) return err;
    err = i2c_master_transmit(device, data, length, timeout_ms);
    board_i2c_unlock();
    return err;
}
