// DibaVault — token registry (ERC20 / TRC20 / SPL). GPL-3.0. dibachain.
//
// Non-sensitive config (like net_config): the operator can register ANY token
// contract on ANY supported network and the wallet will show its balance and
// let it be sent. Tokens are matched to a network by (family, evm_chain_id).
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define DV_TOKEN_MAX        64
#define DV_TOKEN_SYM_MAX    16
#define DV_TOKEN_NAME_MAX   40

typedef struct {
    bool       used;
    dv_chain_t family;         // owning chain family
    uint64_t   evm_chain_id;   // owning EVM network (0 for non-EVM)
    char       symbol[DV_TOKEN_SYM_MAX];
    char       name[DV_TOKEN_NAME_MAX];
    char       contract[DV_ADDR_STR_MAX];  // ERC20/TRC20 address or SPL mint
    uint8_t    decimals;
} dv_token_t;

dv_err_t tokens_config_init(void);
size_t   tokens_config_count(void);
dv_err_t tokens_config_get(size_t idx, dv_token_t *out);
dv_err_t tokens_config_add(const dv_token_t *t);
dv_err_t tokens_config_remove(size_t idx);
// Seed a few well-known stablecoins (USDT/USDC) on major networks.
dv_err_t tokens_config_seed_defaults(void);

#ifdef __cplusplus
}
#endif
