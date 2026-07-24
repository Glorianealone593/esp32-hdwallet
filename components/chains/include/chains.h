// DibaVault — chain abstraction (address encoding, sighash, tx serialization)
// Copyright (c) dibachain. GPL-3.0.
//
// These functions are PURE: no private keys, no network, no storage. They are
// safe to call from either the vault (to recompute the canonical sighash and to
// serialize the final signed tx) or the connectivity layer (address display).
//
// Each chain implements a vtable. Adding a new chain family = one new file that
// registers a `dv_chain_ops_t`.
#pragma once

#include "dv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// dv_unsigned_tx_t and dv_tx_review_t are declared in dv_types.h.

// Per-chain operations.
typedef struct {
    dv_chain_t chain;
    const char *name;
    uint32_t   bip44_coin;     // e.g. 0, 60, 195, 501
    bool       hardened_change; // path shape hints

    // Encode a public key into the chain's address string.
    dv_err_t (*format_address)(const uint8_t *pubkey, size_t pubkey_len,
                               uint64_t evm_chain_id,
                               char *out, size_t out_sz);

    // Produce the canonical signing digest (32 bytes) OR, for ed25519 chains,
    // the full message to sign (set *is_prehash=false, message in out).
    // Returns the message/digest and its length; caller signs it.
    dv_err_t (*sighash)(const dv_unsigned_tx_t *tx,
                        const uint8_t *signer_pubkey, size_t pubkey_len,
                        uint8_t *out, size_t *out_len, bool *is_prehash);

    // Given the unsigned tx + signature, serialize a broadcastable transaction.
    dv_err_t (*serialize_signed)(const dv_unsigned_tx_t *tx,
                                 const uint8_t *signer_pubkey, size_t pubkey_len,
                                 const uint8_t *sig, size_t sig_len,
                                 uint8_t recid,
                                 uint8_t *out, size_t *out_len, size_t out_cap);

    // Human-readable review lines for the confirmation screen/console.
    dv_err_t (*describe)(const dv_unsigned_tx_t *tx, dv_tx_review_t *out);
} dv_chain_ops_t;

// Registry.
const dv_chain_ops_t *chains_get(dv_chain_t chain);
dv_err_t              chains_default_path(dv_chain_t chain, uint32_t account,
                                          uint32_t index, dv_path_t *out);

#ifdef __cplusplus
}
#endif
