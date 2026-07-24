// DibaVault — signing orchestration (SECURE CORE ONLY). GPL-3.0. dibachain.
#pragma once
#include "dv_types.h"
#include "chains.h"
#include "vault_ipc.h"
#ifdef __cplusplus
extern "C" {
#endif

// Full transaction-signing flow: derive signer pubkey, build a human review,
// require physical confirmation, recompute the canonical sighash from the
// reviewed fields (never trusts a raw blob), sign, and serialize a broadcastable
// transaction into `resp`. `remote` tightens the confirmation policy.
dv_err_t signer_sign_tx(const dv_unsigned_tx_t *tx, const dv_path_t *path,
                        bool remote, vault_resp_t *resp);

// Sign a personal/typed message (EIP-191, etc.) after confirmation.
dv_err_t signer_sign_message(dv_chain_t chain, const dv_path_t *path,
                             const uint8_t *msg, size_t len, bool is_personal,
                             bool remote, vault_resp_t *resp);

#ifdef __cplusplus
}
#endif
