# hal — runtime-configurable display + buttons. No keys.
idf_component_register(
    SRCS "hal_config.c" "display.c" "button.c"
    INCLUDE_DIRS "include"
    REQUIRES common
    PRIV_REQUIRES nvs_flash driver esp_driver_i2c esp_driver_gpio
)
