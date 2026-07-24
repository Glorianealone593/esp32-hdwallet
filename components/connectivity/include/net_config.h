// DibaVault — user-managed network / RPC registry. Copyright (c) dibachain. GPL-3.0.
//
// Networks and their RPC endpoints are NON-sensitive config. They live in the
// normal `nvs` partition and are fully managed from the web UI / console. This
// is the "add your own chains and RPCs" feature.
#pragma once
#include "dv_types.h"
#ifdef __cplusplus
extern "C" {
#endif

#define DV_NET_MAX        32
#define DV_RPC_URL_MAX    160
#define DV_NET_NAME_MAX   40

typedef struct {
    bool       used;
    dv_chain_t family;              // which signing family this network belongs to
    uint64_t   evm_chain_id;        // for EVM networks (1, 56, 137, 42161, ...)
    char       name[DV_NET_NAME_MAX];  // "Ethereum", "BSC", "Polygon", "My Bitcoin node"
    char       rpc_url[DV_RPC_URL_MAX];
    char       symbol[12];          // "ETH","BNB","MATIC","BTC","TRX","SOL"
    uint8_t    decimals;            // 18 / 8 / 6 / 9
    char       explorer[DV_RPC_URL_MAX]; // optional
} dv_network_t;

dv_err_t net_config_init(void);
size_t   net_config_count(void);
dv_err_t net_config_get(size_t idx, dv_network_t *out);
dv_err_t net_config_add(const dv_network_t *n);
dv_err_t net_config_update(size_t idx, const dv_network_t *n);
dv_err_t net_config_remove(size_t idx);
// Seed the registry with sensible mainnet defaults on first boot.
dv_err_t net_config_seed_defaults(void);

#ifdef __cplusplus
}
#endif
