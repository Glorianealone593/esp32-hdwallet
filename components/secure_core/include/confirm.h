// DibaVault — physical confirmation gate. Copyright (c) dibachain. GPL-3.0.
//
// The single choke point that turns "the software wants to sign" into "a human
// approved it". Behavior adapts to the provisioned HAL:
//   * OLED + button(s): render the tx, require an explicit button confirm.
//   * button only:       show summary on web/console, require button confirm.
//   * headless:          require the software confirmation token (weaker; the
//                        UI and README loudly warn that this is not air-gapped).
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif

// Present `tx` for approval. Blocks until the user confirms, rejects, or the
// timeout elapses. `remote` = true tightens policy (e.g. always require the
// physical button when one is configured, regardless of confirm token).
dv_err_t confirm_transaction(const dv_tx_review_t *tx, bool remote);

// Present arbitrary review lines (message signing, mnemonic reveal warnings).
dv_err_t confirm_prompt(const char *title, const char *const *lines,
                        size_t n_lines, bool remote);

#ifdef __cplusplus
}
#endif
