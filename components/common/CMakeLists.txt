idf_component_register(
    SRCS
    "gpio_util.cpp"
    "mem_util.c"
    "string_util.cpp"
    INCLUDE_DIRS "."
    REQUIRES
    esp_driver_gpio
    log
)
