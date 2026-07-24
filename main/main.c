// DibaVault — application entry point. GPL-3.0. dibachain.
//
// Boot sequence:
//   1) init NVS (normal + encrypted secure partition)
//   2) start the secure vault task (keystore lives here, on its own core)
//   3) init HAL from the persisted (or default) hardware config
//   4) mount the web UI filesystem
//   5) bring up connectivity: AP hotspot (+ optional STA) and the web server
//   6) start the local serial console
//
// If the device is unprovisioned it comes up in AP-only setup mode so the first
// run can happen over the hotspot at http://192.168.4.1 or over serial.
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"

#include "vault_ipc.h"
#include "hal_config.h"
#include "display.h"
#include "button.h"
#include "net_config.h"
#include "tokens_config.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "ota.h"
#include "console_ui.h"

static const char *TAG="dibavault";

static void mount_web_fs(void){
    esp_vfs_spiffs_conf_t c={ .base_path="/spiffs", .partition_label="storage",
        .max_files=8, .format_if_mount_failed=false };
    esp_err_t e=esp_vfs_spiffs_register(&c);
    if(e!=ESP_OK) ESP_LOGW(TAG,"web fs mount failed (%d) — UI unavailable, console still works",e);
}

void app_main(void){
    ESP_LOGI(TAG,"DibaVault %s — dibachain HD wallet firmware",DIBAVAULT_VERSION);

    // 1) NVS
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){
        nvs_flash_erase(); nvs_flash_init();
    }

    // 2) Secure vault task (owns the keys). Everything else talks to it via IPC.
    if(vault_ipc_start()!=DV_OK){ ESP_LOGE(TAG,"vault failed to start"); return; }
    ota_init();

    // 3) HAL from persisted config (chosen at first-boot provisioning).
    dv_hal_config_t hal; hal_config_load(&hal);
    display_init(&hal);
    button_init(&hal);
    if(display_available()){ display_title("DibaVault"); display_text(1,"dibachain"); display_text(2,DIBAVAULT_VERSION); }

    // 4) Web assets
    mount_web_fs();

    // 5) Connectivity: AP hotspot up so the local UI is reachable. STA is opt-in
    //    from Settings. Keys are unreachable from either — see vault_ipc.h.
    net_config_init();
    tokens_config_init();
    wifi_mgr_init();
    wifi_mgr_set_mode(DV_WIFI_AP);
    web_server_start();
    ESP_LOGI(TAG,"Web UI: connect to the AP and open http://192.168.4.1");

    // 6) Local serial console (LOCAL origin: reveal seed / factory reset).
    console_ui_start();

    // Idle; all work happens in the vault, web-server, and console tasks.
    for(;;) vTaskDelay(pdMS_TO_TICKS(10000));
}
