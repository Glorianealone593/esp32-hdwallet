# connectivity — WiFi/HTTP/RPC/OTA. UNTRUSTED. Links vault_ipc.h only (never
# keystore.h/signer.h). Enforced by tools/check_boundary.sh.
idf_component_register(
    SRCS
        "net_config.c"
        "tokens_config.c"
        "wifi_mgr.c"
        "rpc_client.c"
        "ota.c"
        "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES common secure_core chains dv_hal
    PRIV_REQUIRES
        esp_http_server
        esp_http_client
        esp_https_ota
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        json
        app_update
        spiffs
        esp_app_format
        esp-tls
        mbedtls
)
