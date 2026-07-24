// DibaVault — token registry (NVS, non-sensitive). GPL-3.0. dibachain.
#include "tokens_config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG="tokens";
#define NS "tokens"
#define KEY "list"

static dv_token_t s_tok[DV_TOKEN_MAX];
static bool s_loaded=false;

static void persist(void){
    nvs_handle_t h;
    if(nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK) return;
    nvs_set_blob(h,KEY,s_tok,sizeof(s_tok)); nvs_commit(h); nvs_close(h);
}

dv_err_t tokens_config_init(void){
    if(s_loaded) return DV_OK;
    memset(s_tok,0,sizeof(s_tok));
    nvs_handle_t h;
    if(nvs_open(NS,NVS_READONLY,&h)==ESP_OK){ size_t sz=sizeof(s_tok); nvs_get_blob(h,KEY,s_tok,&sz); nvs_close(h); }
    s_loaded=true;
    bool any=false; for(int i=0;i<DV_TOKEN_MAX;i++) if(s_tok[i].used){any=true;break;}
    if(!any) tokens_config_seed_defaults();
    return DV_OK;
}

size_t tokens_config_count(void){ size_t n=0; for(int i=0;i<DV_TOKEN_MAX;i++) if(s_tok[i].used)n++; return n; }

dv_err_t tokens_config_get(size_t idx,dv_token_t *out){
    if(idx>=DV_TOKEN_MAX||!s_tok[idx].used) return DV_ERR_INVALID_ARG;
    *out=s_tok[idx]; return DV_OK;
}
dv_err_t tokens_config_add(const dv_token_t *t){
    for(int i=0;i<DV_TOKEN_MAX;i++) if(!s_tok[i].used){ s_tok[i]=*t; s_tok[i].used=true; persist(); return DV_OK; }
    return DV_ERR_NO_MEM;
}
dv_err_t tokens_config_remove(size_t idx){
    if(idx>=DV_TOKEN_MAX||!s_tok[idx].used) return DV_ERR_INVALID_ARG;
    memset(&s_tok[idx],0,sizeof(dv_token_t)); persist(); return DV_OK;
}

dv_err_t tokens_config_seed_defaults(void){
    // A small starter set of widely-used stablecoins. Users add any others in
    // the UI. Contracts are mainnet addresses / mints for each network.
    const dv_token_t defs[]={
        // Ethereum mainnet
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=1,.symbol="USDT",.name="Tether USD",
         .contract="0xdAC17F958D2ee523a2206206994597C13D831ec7",.decimals=6},
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=1,.symbol="USDC",.name="USD Coin",
         .contract="0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",.decimals=6},
        // BNB Smart Chain
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=56,.symbol="USDT",.name="Tether USD (BSC)",
         .contract="0x55d398326f99059fF775485246999027B3197955",.decimals=18},
        // Polygon
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=137,.symbol="USDC",.name="USD Coin (Polygon)",
         .contract="0x3c499c542cEF5E3811e1192ce70d8cC03d5c3359",.decimals=6},
        // Tron TRC20
        {.used=1,.family=DV_CHAIN_TRON,.evm_chain_id=0,.symbol="USDT",.name="Tether USD (TRC20)",
         .contract="TR7NHqjeKQxGTCi8q8ZY4pL8otSzgjLj6t",.decimals=6},
        // Solana SPL
        {.used=1,.family=DV_CHAIN_SOLANA,.evm_chain_id=0,.symbol="USDC",.name="USD Coin (SPL)",
         .contract="EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v",.decimals=6},
    };
    for(size_t i=0;i<sizeof(defs)/sizeof(defs[0]) && i<DV_TOKEN_MAX;i++) s_tok[i]=defs[i];
    persist();
    ESP_LOGI(TAG,"seeded %d default tokens",(int)(sizeof(defs)/sizeof(defs[0])));
    return DV_OK;
}
