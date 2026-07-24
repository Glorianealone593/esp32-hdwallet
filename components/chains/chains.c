// DibaVault — chain registry + shared helpers. GPL-3.0. dibachain.
#include "chains.h"
#include <string.h>

// Per-chain implementations register their ops here.
extern const dv_chain_ops_t dv_chain_ops_btc;
extern const dv_chain_ops_t dv_chain_ops_evm;
extern const dv_chain_ops_t dv_chain_ops_trx;
extern const dv_chain_ops_t dv_chain_ops_sol;

static const dv_chain_ops_t *const REGISTRY[DV_CHAIN__COUNT] = {
    [DV_CHAIN_BITCOIN] = &dv_chain_ops_btc,
    [DV_CHAIN_EVM]     = &dv_chain_ops_evm,
    [DV_CHAIN_TRON]    = &dv_chain_ops_trx,
    [DV_CHAIN_SOLANA]  = &dv_chain_ops_sol,
};

const dv_chain_ops_t *chains_get(dv_chain_t chain) {
    if (chain < 0 || chain >= DV_CHAIN__COUNT) return NULL;
    return REGISTRY[chain];
}

#define H(i) (0x80000000u | (uint32_t)(i))

dv_err_t chains_default_path(dv_chain_t chain, uint32_t account,
                             uint32_t index, dv_path_t *out) {
    memset(out, 0, sizeof(*out));
    switch (chain) {
    case DV_CHAIN_BITCOIN: // BIP84 native segwit  m/84'/0'/acct'/0/idx
        out->index[0] = H(84); out->index[1] = H(0); out->index[2] = H(account);
        out->index[3] = 0;     out->index[4] = index; out->depth = 5;
        return DV_OK;
    case DV_CHAIN_EVM:     // m/44'/60'/acct'/0/idx
        out->index[0] = H(44); out->index[1] = H(60); out->index[2] = H(account);
        out->index[3] = 0;     out->index[4] = index; out->depth = 5;
        return DV_OK;
    case DV_CHAIN_TRON:    // m/44'/195'/acct'/0/idx
        out->index[0] = H(44); out->index[1] = H(195); out->index[2] = H(account);
        out->index[3] = 0;     out->index[4] = index;  out->depth = 5;
        return DV_OK;
    case DV_CHAIN_SOLANA:  // m/44'/501'/acct'/0'  (all hardened, SLIP-0010)
        out->index[0] = H(44); out->index[1] = H(501); out->index[2] = H(account);
        out->index[3] = H(0);  out->depth = 4;
        return DV_OK;
    default:
        return DV_ERR_UNSUPPORTED_CHAIN;
    }
}
