// DibaVault — button/input driver. GPL-3.0. dibachain.
// Polling + debounce. In 1-button mode a long press (>=800ms) = confirm, a short
// press = cancel, so a single GPIO can both approve and reject.
#include "button.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static dv_hal_config_t s_cfg;
static bool s_ready=false;

static void cfg_pin(int8_t g){
    if(g<0) return;
    gpio_config_t io={ .pin_bit_mask=1ULL<<g, .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE };
    gpio_config(&io);
}

dv_err_t button_init(const dv_hal_config_t *cfg){
    s_cfg=*cfg;
    if (cfg->confirm_mode==DV_CONFIRM_NONE) { s_ready=false; return DV_OK; }
    cfg_pin(cfg->btn_ok); cfg_pin(cfg->btn_cancel); cfg_pin(cfg->btn_up); cfg_pin(cfg->btn_down);
    s_ready = (cfg->btn_ok>=0 || cfg->btn_cancel>=0);
    return DV_OK;
}

bool button_available(void){ return s_ready; }

static bool pressed(int8_t g){
    if(g<0) return false;
    int lvl=gpio_get_level(g);
    return s_cfg.btn_active_low ? (lvl==0) : (lvl==1);
}

dv_btn_event_t button_wait(uint32_t timeout_ms){
    if(!s_ready) return DV_BTN_NONE;
    uint32_t elapsed=0; const uint32_t step=10;
    while(elapsed<timeout_ms){
        // OK button (with long-press detection)
        if(pressed(s_cfg.btn_ok)){
            uint32_t held=0;
            while(pressed(s_cfg.btn_ok) && held<3000){ vTaskDelay(pdMS_TO_TICKS(step)); held+=step; }
            if(s_cfg.confirm_mode==DV_CONFIRM_1BUTTON)
                return (held>=800)?DV_BTN_OK_LONG:DV_BTN_CANCEL;
            return DV_BTN_OK;
        }
        if(pressed(s_cfg.btn_cancel)){ while(pressed(s_cfg.btn_cancel))vTaskDelay(pdMS_TO_TICKS(step)); return DV_BTN_CANCEL; }
        if(pressed(s_cfg.btn_up))    { while(pressed(s_cfg.btn_up))vTaskDelay(pdMS_TO_TICKS(step));     return DV_BTN_UP; }
        if(pressed(s_cfg.btn_down))  { while(pressed(s_cfg.btn_down))vTaskDelay(pdMS_TO_TICKS(step));   return DV_BTN_DOWN; }
        vTaskDelay(pdMS_TO_TICKS(step)); elapsed+=step;
    }
    return DV_BTN_NONE;
}
