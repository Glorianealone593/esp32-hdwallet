// DibaVault — Hardware Abstraction Layer configuration.
// Copyright (c) dibachain. GPL-3.0.
//
// The hardware layout (display type, button GPIOs, I2C pins) is chosen ONCE by
// the operator during first-boot provisioning and persisted in NVS. After that
// it is fixed for the device. This lets one firmware image run on any wiring /
// any of the three supported chips.
#pragma once

#include "dv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DV_DISPLAY_NONE     = 0,  // headless: review shown on web/console only
    DV_DISPLAY_SSD1306  = 1,  // 128x64 / 128x32 OLED over I2C
    DV_DISPLAY_SH1106   = 2,  // 1.3" OLED variant over I2C
    DV_DISPLAY_ST7789   = 3,  // color TFT over SPI (optional)
} dv_display_type_t;

typedef enum {
    DV_CONFIRM_NONE     = 0,  // software confirm only (weakest; warned in UI)
    DV_CONFIRM_1BUTTON  = 1,  // single button: long-press = confirm
    DV_CONFIRM_2BUTTON  = 2,  // OK / Cancel
    DV_CONFIRM_3BUTTON  = 3,  // Up / Down / OK (menu navigation)
} dv_confirm_mode_t;

typedef struct {
    // Schema/version so we can migrate the stored blob safely.
    uint16_t          magic;         // DV_HALCFG_MAGIC
    uint16_t          version;

    dv_display_type_t display;
    int8_t            i2c_sda;        // -1 = unused
    int8_t            i2c_scl;
    uint8_t           i2c_addr;       // e.g. 0x3C
    uint16_t          disp_width;     // 128
    uint16_t          disp_height;    // 64 / 32
    // SPI (ST7789)
    int8_t            spi_mosi, spi_sclk, spi_cs, spi_dc, spi_rst, spi_bl;

    dv_confirm_mode_t confirm_mode;
    int8_t            btn_ok;         // GPIO for OK/confirm (-1 unused)
    int8_t            btn_cancel;
    int8_t            btn_up;
    int8_t            btn_down;
    bool              btn_active_low; // true if pressed == GND

    // Optional status LED.
    int8_t            led_gpio;
    bool              led_active_low;

    uint32_t          confirm_timeout_ms; // how long the device waits for input
} dv_hal_config_t;

#define DV_HALCFG_MAGIC   0xD1BA
#define DV_HALCFG_VERSION 1

// Load persisted config; if none, returns a safe headless default.
dv_err_t hal_config_load(dv_hal_config_t *out);

// Persist config (called at provisioning). Validates GPIO ranges for the target.
dv_err_t hal_config_save(const dv_hal_config_t *cfg);

bool     hal_config_exists(void);

// A sane starting template for the given chip so the setup UI can prefill it.
void     hal_config_default_for_target(dv_hal_config_t *out);

// Validate a proposed config against the current chip (pin exists, not a
// strapping/flash pin, no duplicates). Returns DV_OK or DV_ERR_INVALID_ARG and
// fills `reason` (<=64 bytes) for the UI.
dv_err_t hal_config_validate(const dv_hal_config_t *cfg, char *reason, size_t sz);

#ifdef __cplusplus
}
#endif
