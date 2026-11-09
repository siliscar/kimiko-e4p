#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 原厂共享总线速率与 BQ27220 拉伸窗口。/ Vendor shared-bus rate and BQ27220 stretching window.
#define BOARD_I2C_HZ 100000
#define BOARD_I2C_SCL_WAIT_US 10000

/// 总线事务锁可嵌套；不从中断调用。/ Recursive transaction lock; never call from an ISR.
esp_err_t board_i2c_lock(int timeout_ms);
void board_i2c_unlock(void);
/// 按原厂配置添加设备；所有权仍归调用驱动。/ Add a vendor-configured device, still owned by its driver.
esp_err_t board_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t address,
                               i2c_master_dev_handle_t* device);
/// 探测与传输共用事务锁。/ Probe under the same lock as transfers.
esp_err_t board_i2c_probe(i2c_master_bus_handle_t bus, uint8_t address, int timeout_ms);
/// 原厂电源读法：combined 优先，失败后 STOP 重试。/ Vendor power read: combined first, then a STOP retry.
esp_err_t board_i2c_read(i2c_master_dev_handle_t device, uint8_t reg,
                        uint8_t* data, size_t length, int timeout_ms);
/// 原厂 TCA/TPS 读法：指针写与读取之间保留 STOP。/ Vendor TCA/TPS read with STOP between pointer and data.
esp_err_t board_i2c_read_stop(i2c_master_dev_handle_t device, uint8_t reg,
                             uint8_t* data, size_t length, int timeout_ms);
/// 完整写事务，缓冲区含寄存器地址。/ Complete write transaction, including the register address.
esp_err_t board_i2c_transmit(i2c_master_dev_handle_t device, const uint8_t* data,
                            size_t length, int timeout_ms);

#ifdef __cplusplus
}
#endif
