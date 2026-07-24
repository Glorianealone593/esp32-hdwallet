// DibaVault — HAL runtime config persistence. GPL-3.0. dibachain.
#include "hal_config.h"
#include <string.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/gpio_num.h"
#include "esp_log.h"

static const char *TAG="halcfg";
#define NS "halcfg"
#define KEY "cfg"

void hal_config_default_for_target(dv_hal_config_t *o) {
    memset(o,0,sizeof(*o));
    o->magic=DV_HALCFG_MAGIC; o->version=DV_HALCFG_VERSION;
    o->display=DV_DISPLAY_NONE;
    o->i2c_sda=-1; o->i2c_scl=-1; o->i2c_addr=0x3C;
    o->disp_width=128; o->disp_height=64;
    o->spi_mosi=o->spi_sclk=o->spi_cs=o->spi_dc=o->spi_rst=o->spi_bl=-1;
    o->confirm_mode=DV_CONFIRM_NONE;
    o->btn_ok=o->btn_cancel=o->btn_up=o->btn_down=-1;
    o->btn_active_low=true;
    o->led_gpio=-1; o->led_active_low=false;
    o->confirm_timeout_ms=60000;
    // Sensible prefill hints per target (operator can override in the UI).
#if CONFIG_IDF_TARGET_ESP32
    o->i2c_sda=21; o->i2c_scl=22; o->btn_ok=0; o->btn_cancel=35;
#elif CONFIG_IDF_TARGET_ESP32S3
    o->i2c_sda=8;  o->i2c_scl=9;  o->btn_ok=0; o->btn_cancel=14;
#elif CONFIG_IDF_TARGET_ESP32C3
    o->i2c_sda=5;  o->i2c_scl=6;  o->btn_ok=9; o->btn_cancel=3;
#endif
}

bool hal_config_exists(void) {
    nvs_handle_t h;
    if (nvs_open(NS,NVS_READONLY,&h)!=ESP_OK) return false;
    size_t sz=0; esp_err_t e=nvs_get_blob(h,KEY,NULL,&sz);
    nvs_close(h);
    return e==ESP_OK && sz==sizeof(dv_hal_config_t);
}

dv_err_t hal_config_load(dv_hal_config_t *out) {
    nvs_handle_t h;
    if (nvs_open(NS,NVS_READONLY,&h)==ESP_OK) {
        size_t sz=sizeof(*out);
        esp_err_t e=nvs_get_blob(h,KEY,out,&sz);
        nvs_close(h);
        if (e==ESP_OK && sz==sizeof(*out) && out->magic==DV_HALCFG_MAGIC) return DV_OK;
    }
    hal_config_default_for_target(out);   // safe headless default
    return DV_OK;
}

static bool gpio_ok(int8_t g) {
    if (g<0) return true;                  // -1 = unused
    return g>=0 && g<GPIO_NUM_MAX;
}

dv_err_t hal_config_validate(const dv_hal_config_t *c, char *reason, size_t sz) {
    #define FAIL(msg) do{ if(reason)strlcpy(reason,msg,sz); return DV_ERR_INVALID_ARG;}while(0)
    if (c->display!=DV_DISPLAY_NONE) {
        if (!gpio_ok(c->i2c_sda)||!gpio_ok(c->i2c_scl)) FAIL("bad I2C pin");
        if (c->display!=DV_DISPLAY_ST7789 && (c->i2c_sda<0||c->i2c_scl<0)) FAIL("I2C pins required");
    }
    int8_t btns[4]={c->btn_ok,c->btn_cancel,c->btn_up,c->btn_down};
    for(int i=0;i<4;i++){ if(!gpio_ok(btns[i])) FAIL("bad button GPIO");
        for(int j=i+1;j<4;j++) if(btns[i]>=0 && btns[i]==btns[j]) FAIL("duplicate button GPIO"); }
    if (c->confirm_mode==DV_CONFIRM_1BUTTON && c->btn_ok<0) FAIL("OK button required");
    if (c->confirm_mode==DV_CONFIRM_2BUTTON && (c->btn_ok<0||c->btn_cancel<0)) FAIL("OK+Cancel required");
    return DV_OK;
    #undef FAIL
}

dv_err_t hal_config_save(const dv_hal_config_t *cfg) {
    char reason[64];
    dv_hal_config_t c=*cfg; c.magic=DV_HALCFG_MAGIC; c.version=DV_HALCFG_VERSION;
    dv_err_t v=hal_config_validate(&c,reason,sizeof(reason));
    if (v!=DV_OK){ ESP_LOGE(TAG,"invalid hal cfg: %s",reason); return v; }
    nvs_handle_t h;
    if (nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK) return DV_ERR_STORAGE;
    esp_err_t e=nvs_set_blob(h,KEY,&c,sizeof(c));
    if (e==ESP_OK) e=nvs_commit(h);
    nvs_close(h);
    return e==ESP_OK?DV_OK:DV_ERR_STORAGE;
}
