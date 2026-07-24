// DibaVault — network/RPC registry (NVS, non-sensitive). GPL-3.0. dibachain.
#include "net_config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG="netcfg";
#define NS "networks"
#define KEY "list"

static dv_network_t s_nets[DV_NET_MAX];
static bool s_loaded=false;

static void persist(void){
    nvs_handle_t h;
    if(nvs_open(NS,NVS_READWRITE,&h)!=ESP_OK) return;
    nvs_set_blob(h,KEY,s_nets,sizeof(s_nets)); nvs_commit(h); nvs_close(h);
}

dv_err_t net_config_init(void){
    if(s_loaded) return DV_OK;
    memset(s_nets,0,sizeof(s_nets));
    nvs_handle_t h;
    if(nvs_open(NS,NVS_READONLY,&h)==ESP_OK){
        size_t sz=sizeof(s_nets); nvs_get_blob(h,KEY,s_nets,&sz); nvs_close(h);
    }
    s_loaded=true;
    bool any=false; for(int i=0;i<DV_NET_MAX;i++) if(s_nets[i].used){any=true;break;}
    if(!any) net_config_seed_defaults();
    return DV_OK;
}

size_t net_config_count(void){ size_t n=0; for(int i=0;i<DV_NET_MAX;i++) if(s_nets[i].used)n++; return n; }

dv_err_t net_config_get(size_t idx,dv_network_t *out){
    if(idx>=DV_NET_MAX||!s_nets[idx].used) return DV_ERR_INVALID_ARG;
    *out=s_nets[idx]; return DV_OK;
}

dv_err_t net_config_add(const dv_network_t *n){
    for(int i=0;i<DV_NET_MAX;i++) if(!s_nets[i].used){ s_nets[i]=*n; s_nets[i].used=true; persist(); return DV_OK; }
    return DV_ERR_NO_MEM;
}
dv_err_t net_config_update(size_t idx,const dv_network_t *n){
    if(idx>=DV_NET_MAX) return DV_ERR_INVALID_ARG;
    s_nets[idx]=*n; s_nets[idx].used=true; persist(); return DV_OK;
}
dv_err_t net_config_remove(size_t idx){
    if(idx>=DV_NET_MAX||!s_nets[idx].used) return DV_ERR_INVALID_ARG;
    memset(&s_nets[idx],0,sizeof(dv_network_t)); persist(); return DV_OK;
}

dv_err_t net_config_seed_defaults(void){
    const dv_network_t defs[]={
        {.used=1,.family=DV_CHAIN_BITCOIN,.evm_chain_id=0,.name="Bitcoin",
         .rpc_url="https://blockstream.info/api",.symbol="BTC",.decimals=8,.explorer="https://blockstream.info"},
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=1,.name="Ethereum",
         .rpc_url="https://ethereum-rpc.publicnode.com",.symbol="ETH",.decimals=18,.explorer="https://etherscan.io"},
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=56,.name="BNB Smart Chain",
         .rpc_url="https://bsc-rpc.publicnode.com",.symbol="BNB",.decimals=18,.explorer="https://bscscan.com"},
        {.used=1,.family=DV_CHAIN_EVM,.evm_chain_id=137,.name="Polygon",
         .rpc_url="https://polygon-bor-rpc.publicnode.com",.symbol="POL",.decimals=18,.explorer="https://polygonscan.com"},
        {.used=1,.family=DV_CHAIN_TRON,.evm_chain_id=0,.name="Tron",
         .rpc_url="https://api.trongrid.io",.symbol="TRX",.decimals=6,.explorer="https://tronscan.org"},
        {.used=1,.family=DV_CHAIN_SOLANA,.evm_chain_id=0,.name="Solana",
         .rpc_url="https://api.mainnet-beta.solana.com",.symbol="SOL",.decimals=9,.explorer="https://solscan.io"},
    };
    memset(s_nets,0,sizeof(s_nets));
    for(size_t i=0;i<sizeof(defs)/sizeof(defs[0]) && i<DV_NET_MAX;i++) s_nets[i]=defs[i];
    persist();
    ESP_LOGI(TAG,"seeded %d default networks",(int)(sizeof(defs)/sizeof(defs[0])));
    return DV_OK;
}
