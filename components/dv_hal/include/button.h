// DibaVault — button/input abstraction. Copyright (c) dibachain. GPL-3.0.
#pragma once
#include "dv_types.h"
#include "hal_config.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DV_BTN_NONE = 0,
    DV_BTN_OK,
    DV_BTN_CANCEL,
    DV_BTN_UP,
    DV_BTN_DOWN,
    DV_BTN_OK_LONG,     // long-press confirm (1-button mode)
} dv_btn_event_t;

dv_err_t       button_init(const dv_hal_config_t *cfg);
// Blocks up to timeout_ms for a debounced event. DV_BTN_NONE on timeout.
dv_btn_event_t button_wait(uint32_t timeout_ms);
bool           button_available(void);

#ifdef __cplusplus
}
#endif
