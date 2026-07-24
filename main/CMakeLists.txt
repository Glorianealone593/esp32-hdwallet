idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        common
        secure_core
        hal
        connectivity
        console
    PRIV_REQUIRES
        nvs_flash
        spiffs
)

# Embed the web UI (web/) into the `storage` SPIFFS partition, flashed with the
# project. Assets are served by components/connectivity/web_server.c.
spiffs_create_partition_image(storage "${CMAKE_CURRENT_LIST_DIR}/../web" FLASH_IN_PROJECT)
