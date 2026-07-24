// DibaVault — RPC client (untrusted, read + broadcast only). GPL-3.0. dibachain.
//
// Talks to user-configured RPC endpoints to: read balances, read nonces/fees/
// UTXOs/blockhashes needed to BUILD an unsigned tx, and BROADCAST a signed tx.
// It never has access to keys. All results are treated as untrusted input and
// re-validated before being shown or signed.
#pragma once
#include "dv_types.h"
#include "net_config.h"
#include "chains.h"
#ifdef __cplusplus
extern "C" {
#endif

// Fetch native balance for `address` on `net`. Returns decimal string.
dv_err_t rpc_get_balance(const dv_network_t *net, const char *address,
                         char *out, size_t out_sz);
// Fetch a token balance (ERC20/TRC20/SPL) for `address`.
dv_err_t rpc_get_token_balance(const dv_network_t *net, const char *contract,
                               const char *address, uint8_t decimals,
                               char *out, size_t out_sz);
// Populate the chain-specific fields (nonce, gas, blockhash, UTXOs) needed to
// build `tx`. Fills the fields left blank by the UI.
dv_err_t rpc_prepare_tx(const dv_network_t *net, dv_unsigned_tx_t *tx);
// Broadcast a serialized signed transaction. Returns the txid/hash.
dv_err_t rpc_broadcast(const dv_network_t *net, const uint8_t *signed_tx,
                       size_t len, char *out_txid, size_t out_sz);

#ifdef __cplusplus
}
#endif
