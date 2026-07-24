// DibaVault — serial/USB console UI. Copyright (c) dibachain. GPL-3.0.
//
// A LOCAL-origin interface (VORIGIN_LOCAL_CONSOLE). Because it is physically
// attached, it is allowed to do things the web UI may not — e.g. reveal the
// mnemonic during setup, run factory reset, and drive fully-offline signing.
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif
// Registers commands with esp_console and starts the REPL on UART/USB-Serial.
dv_err_t console_ui_start(void);
#ifdef __cplusplus
}
#endif
