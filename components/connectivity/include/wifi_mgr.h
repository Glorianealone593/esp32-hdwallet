// DibaVault — WiFi manager. Copyright (c) dibachain. GPL-3.0.
//
// Two modes, deliberately separated by policy:
//   * AP  (hotspot): the device's own network for the local web UI. Watch-only
//     features (balances, address display, tx building) are available here.
//     Signing still requires physical confirmation. The AP has a WPA2 password
//     set at provisioning and is NOT bridged to the internet.
//   * STA (join a router): used to reach RPC endpoints and to pull signed OTA
//     updates. Enabling STA is an explicit, user-initiated action and can be
//     restricted to "updates + read-only" per the security policy.
//
// The private keys are unreachable from either mode: the WiFi/HTTP stack links
// only against vault_ipc.h, never keystore.h/signer.h.
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { DV_WIFI_OFF, DV_WIFI_AP, DV_WIFI_STA, DV_WIFI_APSTA } dv_wifi_mode_t;

typedef struct {
    char ap_ssid[33];
    char ap_pass[65];      // >=8 chars; generated at provisioning if empty
    char sta_ssid[33];
    char sta_pass[65];
    bool sta_updates_only; // policy flag surfaced to the UI
} dv_wifi_cfg_t;

dv_err_t       wifi_mgr_init(void);
dv_err_t       wifi_mgr_load_cfg(dv_wifi_cfg_t *out);
dv_err_t       wifi_mgr_save_cfg(const dv_wifi_cfg_t *cfg);
dv_err_t       wifi_mgr_set_mode(dv_wifi_mode_t mode);
dv_wifi_mode_t wifi_mgr_get_mode(void);
bool           wifi_mgr_sta_connected(void);
// True when the radio is fully off — the "air-gapped" state for offline signing.
bool           wifi_mgr_is_airgapped(void);

#ifdef __cplusplus
}
#endif
