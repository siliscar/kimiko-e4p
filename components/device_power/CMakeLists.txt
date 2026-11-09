idf_component_register(
    SRCS "device_power_metalio.c" "battery_log.c"
    INCLUDE_DIRS "include"
    REQUIRES board_i2c esp_driver_i2c fca9555
    PRIV_REQUIRES freertos esp_rom esp_timer nvs_flash esp_app_format esp_system esp_hw_support
)
