// DibaVault — display abstraction. Copyright (c) dibachain. GPL-3.0.
#pragma once
#include "dv_types.h"
#include "hal_config.h"
#ifdef __cplusplus
extern "C" {
#endif

dv_err_t display_init(const dv_hal_config_t *cfg);
void     display_clear(void);
void     display_text(int line, const char *str);   // simple line-based text
void     display_title(const char *str);
// Render a transaction confirmation screen; returns immediately (input handled
// by the confirm gate). Shows to/amount/fee, paginated for small OLEDs.
void     display_confirm_screen(const dv_tx_review_t *tx, int page, int pages);
void     display_show_qr(const uint8_t *data, size_t len);  // for addresses/offline sig
bool     display_available(void);

#ifdef __cplusplus
}
#endif
