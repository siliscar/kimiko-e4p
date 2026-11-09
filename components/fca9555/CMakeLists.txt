idf_component_register(
    SRCS "fca9555.c"
    INCLUDE_DIRS "include"
    REQUIRES board_i2c esp_driver_gpio esp_driver_i2c
)
