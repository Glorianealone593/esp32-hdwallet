// DibaVault — WiFi manager (AP hotspot + STA). GPL-3.0. dibachain.
// No keys here. AP = local watch-only UI; STA = reach RPCs / OTA. Radio can be
// fully powered down for air-gapped offline signing.
#include "wifi_mgr.h"
#include <string.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG="wifi";
#define NS "wifi"
#define KEY "cfg"
static dv_wifi_cfg_t s_cfg;
static dv_wifi_mode_t s_mode=DV_WIFI_OFF;
static bool s_sta_conn=false;
static bool s_inited=false;

static void on_event(void *arg,esp_event_base_t base,int32_t id,void *data){
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){ s_sta_conn=false; esp_wifi_connect(); }
    else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){ s_sta_conn=true; ESP_LOGI(TAG,"STA got IP"); }
}

dv_err_t wifi_mgr_init(void){
    if(s_inited) return DV_OK;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t c=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&c));
    esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,on_event,NULL,NULL);
    esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,on_event,NULL,NULL);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_mgr_load_cfg(&s_cfg);
    if(s_cfg.ap_ssid[0]==0){
        uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);
        snprintf(s_cfg.ap_ssid,sizeof(s_cfg.ap_ssid),"DibaVault-%02X%02X",mac[4],mac[5]);
    }
    if(s_cfg.ap_pass[0]==0){
        // Generate a random 12-char AP password on first boot.
        static const char cs[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        for(int i=0;i<12;i++) s_cfg.ap_pass[i]=cs[esp_random()%(sizeof(cs)-1)];
        s_cfg.ap_pass[12]=0; wifi_mgr_save_cfg(&s_cfg);
        ESP_LOGW(TAG,"Generated AP password: %s (change it in Settings)",s_cfg.ap_pass);
    }
    s_inited=true;
    return DV_OK;
}

dv_err_t wifi_mgr_load_cfg(dv_wifi_cfg_t *out){
    memset(out,0,sizeof(*out));
    nvs_handle_t h;
    if(nvs_open(NS,NVS_READONLY,&h)==ESP_OK){ size_t sz=sizeof(*out); nvs_get_blob(h,KEY,out,&sz); nvs_close(h); }
    return DV_OK;
}
dv_err_t wifi_mgr_save_cfg(const dv_wifi_cfg_t *cfg){
    s_cfg=*cfg; nvs_handle_t h;
    if(nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK) return DV_ERR_STORAGE;
    nvs_set_blob(h,KEY,&s_cfg,sizeof(s_cfg)); nvs_commit(h); nvs_close(h);
    return DV_OK;
}

dv_err_t wifi_mgr_set_mode(dv_wifi_mode_t mode){
    if(!s_inited) wifi_mgr_init();
    if(mode==DV_WIFI_OFF){ esp_wifi_stop(); s_mode=DV_WIFI_OFF; s_sta_conn=false; ESP_LOGI(TAG,"radio OFF (air-gapped)"); return DV_OK; }

    wifi_mode_t wm = (mode==DV_WIFI_AP)?WIFI_MODE_AP : (mode==DV_WIFI_STA)?WIFI_MODE_STA : WIFI_MODE_APSTA;
    ESP_ERROR_CHECK(esp_wifi_set_mode(wm));

    if(mode==DV_WIFI_AP||mode==DV_WIFI_APSTA){
        wifi_config_t ap={0};
        strlcpy((char*)ap.ap.ssid,s_cfg.ap_ssid,sizeof(ap.ap.ssid));
        ap.ap.ssid_len=strlen(s_cfg.ap_ssid);
        strlcpy((char*)ap.ap.password,s_cfg.ap_pass,sizeof(ap.ap.password));
        ap.ap.max_connection=4;
        ap.ap.authmode = (strlen(s_cfg.ap_pass)>=8)?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_AP,&ap);
    }
    if(mode==DV_WIFI_STA||mode==DV_WIFI_APSTA){
        wifi_config_t sta={0};
        strlcpy((char*)sta.sta.ssid,s_cfg.sta_ssid,sizeof(sta.sta.ssid));
        strlcpy((char*)sta.sta.password,s_cfg.sta_pass,sizeof(sta.sta.password));
        esp_wifi_set_config(WIFI_IF_STA,&sta);
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    if(mode==DV_WIFI_STA||mode==DV_WIFI_APSTA) esp_wifi_connect();
    s_mode=mode;
    ESP_LOGI(TAG,"wifi mode=%d ap=%s",mode,s_cfg.ap_ssid);
    return DV_OK;
}

dv_wifi_mode_t wifi_mgr_get_mode(void){ return s_mode; }
bool wifi_mgr_sta_connected(void){ return s_sta_conn; }
bool wifi_mgr_is_airgapped(void){ return s_mode==DV_WIFI_OFF; }
