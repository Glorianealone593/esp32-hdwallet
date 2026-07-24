// DibaVault — physical confirmation gate (SECURE CORE). GPL-3.0. dibachain.
#include "confirm.h"
#include "hal_config.h"
#include "display.h"
#include "button.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "confirm";

static dv_hal_config_t s_cfg;
static bool s_loaded = false;

static const dv_hal_config_t *cfg(void) {
    if (!s_loaded) { hal_config_load(&s_cfg); s_loaded = true; }
    return &s_cfg;
}

// Wait for an explicit OK/long-press. Returns DV_OK / DV_ERR_USER_REJECTED /
// DV_ERR_TIMEOUT depending on the button event.
static dv_err_t wait_button_decision(void) {
    const dv_hal_config_t *c = cfg();
    uint32_t timeout = c->confirm_timeout_ms ? c->confirm_timeout_ms : 60000;
    for (;;) {
        dv_btn_event_t ev = button_wait(timeout);
        switch (ev) {
            case DV_BTN_OK:
            case DV_BTN_OK_LONG:
                return DV_OK;
            case DV_BTN_CANCEL:
                return DV_ERR_USER_REJECTED;
            case DV_BTN_NONE:
                return DV_ERR_TIMEOUT;
            default:
                break; // up/down: keep waiting (paging handled by caller)
        }
    }
}

dv_err_t confirm_transaction(const dv_tx_review_t *tx, bool remote) {
    const dv_hal_config_t *c = cfg();

    // Log a redacted summary (never secrets) so the console operator sees it too.
    ESP_LOGI(TAG, "CONFIRM %s -> %s  amount=%s %s  fee=%s  (%s)",
             tx->symbol, tx->to, tx->amount, tx->symbol, tx->fee,
             remote ? "remote" : "local");

    if (display_available()) {
        // Paginate the review onto the OLED and require a button confirm.
        int pages = 3;
        for (int p = 0; p < pages; p++) display_confirm_screen(tx, p, pages);
        if (button_available()) {
            return wait_button_decision();
        }
        // Display but no button: fall through to policy below.
    }

    if (button_available()) {
        // Button but no display: the summary is shown on web/console; the human
        // still presses the physical button to authorize.
        return wait_button_decision();
    }

    // Headless (no display, no button). This is the weakest posture. A REMOTE
    // request must not be silently auto-approved. We refuse remote signing on a
    // headless device unless the operator explicitly configured CONFIRM_NONE.
    if (c->confirm_mode == DV_CONFIRM_NONE) {
        ESP_LOGW(TAG, "Headless software-only confirm (weak). Approving %s tx.",
                 tx->symbol);
        return DV_OK;
    }
    ESP_LOGE(TAG, "No input device but confirm_mode requires one -> rejecting");
    return DV_ERR_USER_REJECTED;
}

dv_err_t confirm_prompt(const char *title, const char *const *lines,
                        size_t n_lines, bool remote) {
    (void)remote;
    const dv_hal_config_t *c = cfg();
    ESP_LOGI(TAG, "PROMPT: %s", title);
    for (size_t i = 0; i < n_lines; i++) ESP_LOGI(TAG, "  %s", lines[i]);

    if (display_available()) {
        display_clear();
        display_title(title);
        for (size_t i = 0; i < n_lines && i < 5; i++)
            display_text((int)i + 1, lines[i]);
    }
    if (button_available()) return wait_button_decision();
    if (c->confirm_mode == DV_CONFIRM_NONE) return DV_OK;
    return DV_ERR_USER_REJECTED;
}
