// DibaVault — Vault IPC: the ONE narrow boundary between the untrusted
// connectivity layer and the secure core.
// Copyright (c) dibachain. GPL-3.0.
//
// SECURITY MODEL
// --------------
// The connectivity layer (WiFi / HTTP / RPC / OTA) runs untrusted. It NEVER
// links against keystore.h or signer.h and never sees seed/private keys.
// Everything it can ask the vault for goes through this header. By construction
// there is:
//   * NO function that returns a private key, seed, or mnemonic to a caller.
//   * NO function that signs arbitrary opaque bytes without a decoded,
//     human-reviewable summary AND physical confirmation.
//
// The vault runs on a dedicated FreeRTOS task pinned to its own core (where the
// chip has two). Requests arrive on a queue; results leave on a per-request
// reply queue. The connectivity task blocks on the reply, but the vault task
// shares no writable heap with it beyond the message payloads copied in/out.
#pragma once

#include "dv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Request kinds the untrusted side may issue --------------------------
typedef enum {
    VREQ_GET_STATUS,        // provisioned? locked? fw version
    VREQ_LIST_ACCOUNTS,     // public addresses only
    VREQ_DERIVE_ACCOUNT,    // derive a new public account (no private return)
    VREQ_SIGN_TX,           // sign a decoded transaction (needs confirmation)
    VREQ_SIGN_MESSAGE,      // sign a personal/typed message (needs confirmation)
    VREQ_UNLOCK,            // submit PIN to unlock the vault
    VREQ_LOCK,              // wipe session keys from RAM
    // Provisioning requests are only honored while unprovisioned OR from the
    // local console; the HTTP layer cannot trigger a wipe of an existing seed.
    VREQ_PROVISION_NEW,     // generate fresh entropy -> mnemonic (setup only)
    VREQ_PROVISION_IMPORT,  // import an existing mnemonic (setup only)
    VREQ_REVEAL_MNEMONIC,   // display words ON-DEVICE only (see notes below)
} vreq_kind_t;

// Origin of a request. The vault applies stricter policy to REMOTE requests.
typedef enum {
    VORIGIN_LOCAL_CONSOLE = 0,  // physical UART / USB console
    VORIGIN_LOCAL_DISPLAY = 1,  // on-device buttons/menu
    VORIGIN_REMOTE_HTTP   = 2,  // web UI over WiFi (least trusted)
} vreq_origin_t;

// dv_unsigned_tx_t and dv_tx_review_t are declared in dv_types.h.

typedef struct {
    vreq_kind_t   kind;
    vreq_origin_t origin;
    dv_chain_t    chain;
    dv_path_t     path;
    uint64_t      evm_chain_id;
    // Payload union (only the field for `kind` is valid).
    union {
        dv_unsigned_tx_t tx;           // VREQ_SIGN_TX
        struct {                        // VREQ_SIGN_MESSAGE
            uint8_t data[256];
            size_t  len;
            bool    is_personal;        // EIP-191 personal_sign framing
        } msg;
        struct {                        // VREQ_UNLOCK / VREQ_REVEAL_MNEMONIC
            char pin[16];
        } unlock;
        struct {                        // VREQ_PROVISION_IMPORT
            char     mnemonic[DV_MNEMONIC_MAX];
            char     passphrase[128];   // optional BIP39 passphrase
            char     pin[16];
        } import;
        struct {                        // VREQ_PROVISION_NEW
            uint8_t  word_count;        // 12 | 18 | 24
            char     passphrase[128];
            char     pin[16];
        } newseed;
    } u;
} vault_req_t;

typedef struct {
    dv_err_t err;
    union {
        struct {
            bool     provisioned;
            bool     locked;
            char     version[24];
            uint8_t  pin_attempts_left;
        } status;
        dv_account_pub_t account;                 // DERIVE / one entry of LIST
        struct {
            dv_account_pub_t items[8];
            size_t           count;
        } list;
        struct {
            uint8_t  sig[DV_SIG_MAX];              // signature only, no key
            size_t   sig_len;
            uint8_t  signed_tx[1024];             // fully-serialized broadcastable tx
            size_t   signed_tx_len;
        } signed_result;
        struct {
            // REVEAL is only ever delivered to a LOCAL origin. For REMOTE it
            // returns DV_ERR_BOUNDARY. The words are shown on-device.
            char     mnemonic[DV_MNEMONIC_MAX];
        } reveal;
    } u;
} vault_resp_t;

// ---- API used by the connectivity/console layers -------------------------

// Start the vault task. Call once at boot before any request.
dv_err_t vault_ipc_start(void);

// Blocking request. Copies req in, waits up to timeout_ms for a reply, copies
// resp out. Thread-safe; may be called from any task. The vault serializes
// requests internally so only one signing/confirmation runs at a time.
dv_err_t vault_ipc_request(const vault_req_t *req, vault_resp_t *resp,
                           uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
