# 双路冷/暖前光：只依赖 LEDC 与 GPIO。/ Dual cold/warm frontlight: LEDC and GPIO only.
idf_component_register(
    SRCS "frontlight.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_ledc esp_driver_gpio log
)
