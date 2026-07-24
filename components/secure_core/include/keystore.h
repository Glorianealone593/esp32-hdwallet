// DibaVault — keystore (SECURE CORE ONLY)
// Copyright (c) dibachain. GPL-3.0.
//
// This header MUST NOT be included by the connectivity layer. The build fails
// the boundary check (tools/check_boundary.sh) if it is.
//
// Storage model
// -------------
//  * The BIP39 mnemonic entropy is encrypted at rest in the `nvs_secure`
//    partition (NVS encryption backed by flash-encryption key in eFuse).
//  * At rest, entropy is additionally wrapped with a key derived from the
//    user PIN via PBKDF2-HMAC-SHA256 (high iteration count). So a flash dump
//    without the PIN yields only ciphertext.
//  * On unlock, the BIP39 seed (64 bytes) is derived and held in a locked RAM
//    buffer for the session only; lock() zeroizes it (memzero, not memset).
#pragma once

#include "dv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Session state.
typedef struct keystore_ctx keystore_ctx_t;

dv_err_t keystore_init(void);                 // open NVS, load metadata
bool     keystore_is_provisioned(void);

// Provisioning. `word_count` in {12,18,24}. Generates entropy from the hardware
// TRNG, stores it (PIN-wrapped), and returns the mnemonic to `out_mnemonic`
// (caller must present it to a LOCAL origin only, then zeroize).
dv_err_t keystore_provision_new(uint8_t word_count, const char *pin,
                                const char *passphrase,
                                char *out_mnemonic, size_t out_sz);

// Import an existing mnemonic (validated against BIP39 wordlist + checksum).
dv_err_t keystore_provision_import(const char *mnemonic, const char *pin,
                                   const char *passphrase);

// Unlock derives the session seed. Enforces attempt counter + backoff lockout.
dv_err_t keystore_unlock(const char *pin);
void     keystore_lock(void);                 // zeroize session seed
bool     keystore_is_unlocked(void);
uint8_t  keystore_pin_attempts_left(void);

// Reveal the mnemonic. Always re-authenticates with the PIN (defense in depth:
// even an unlocked session cannot dump the words without the PIN). The vault
// additionally restricts this to LOCAL origins. Caller must zeroize `out`.
dv_err_t keystore_reveal_mnemonic(const char *pin, char *out, size_t out_sz);

// Derive a public account (pubkey + chain address). Requires unlock. Returns
// NO private material. The private key exists only transiently inside signer.c.
dv_err_t keystore_derive_public(dv_chain_t chain, const dv_path_t *path,
                                uint64_t evm_chain_id, dv_account_pub_t *out);

// Sign a 32-byte digest with the key at `path`. Used ONLY by signer.c, which
// has already produced the digest from a reviewed, re-encoded transaction.
// The private key is materialized in a stack buffer and zeroized before return.
dv_err_t keystore_sign_digest(dv_chain_t chain, const dv_path_t *path,
                              const uint8_t digest[32],
                              uint8_t *out_sig, size_t *out_sig_len,
                              uint8_t *out_recid /* secp256k1 only, may be NULL */);

// ed25519 signs the full message (not a pre-hash). Solana path.
dv_err_t keystore_sign_ed25519(const dv_path_t *path,
                               const uint8_t *msg, size_t msg_len,
                               uint8_t out_sig[64]);

// Factory reset: zeroize + erase the secure partition. Requires LOCAL origin
// (enforced by vault). Irreversible.
dv_err_t keystore_wipe(void);

#ifdef __cplusplus
}
#endif
