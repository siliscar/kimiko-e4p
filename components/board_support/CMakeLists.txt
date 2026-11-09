set(board_support_requires board_i2c epdiy e0470_epaper_waveform sc7a20h cst836u device_power fatfs fca9555 frontlight
                   esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_sdmmc)

set(board_support_sources
    "board_init.c"
    "board_epd_timing.c"
    "board_sd.c"
    "board_crashlog.c"
    "board_buzzer_stub.c"
    "board_metalio_e4p.c"
)

idf_component_register(
    SRCS ${board_support_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${board_support_requires}
    PRIV_REQUIRES bootloader_support spi_flash freertos esp_timer esp_mm
)
