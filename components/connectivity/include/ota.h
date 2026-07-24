// DibaVault — signed OTA updates. Copyright (c) dibachain. GPL-3.0.
//
// Firmware is the ONLY thing WiFi may change on the device. Every image is
// verified against a pinned dibachain public key (ECDSA over the image hash)
// before it is written to the inactive OTA slot and booted. A rejected image is
// discarded. Keys cannot leak via updates because the running key domain is not
// exposed to the OTA path and the new image must itself be signed by dibachain.
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char     current_version[24];
    char     available_version[24];
    bool     update_available;
} dv_ota_status_t;

dv_err_t ota_init(void);
// Check the dibachain release channel (or a user mirror URL) for a newer image.
dv_err_t ota_check(const char *manifest_url, dv_ota_status_t *out);
// Download + verify signature + write to inactive slot. Does NOT reboot.
dv_err_t ota_apply(const char *image_url, void (*progress)(int pct));
// Mark the new slot valid and reboot into it.
dv_err_t ota_activate_and_reboot(void);

#ifdef __cplusplus
}
#endif
