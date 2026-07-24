// DibaVault — HTTP server (untrusted). Copyright (c) dibachain. GPL-3.0.
//
// Serves the embedded web UI and a small JSON API. Every endpoint that touches
// keys goes through vault_ipc with VORIGIN_REMOTE_HTTP, so the vault applies
// remote policy (physical confirmation, no mnemonic reveal over HTTP, etc.).
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif
dv_err_t web_server_start(void);
dv_err_t web_server_stop(void);
#ifdef __cplusplus
}
#endif
