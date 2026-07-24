// DibaVault — RPC client (read + broadcast, no keys). GPL-3.0. dibachain.
// EVM is implemented end-to-end (balance/nonce/gas/broadcast). Bitcoin, Tron and
// Solana balance reads are implemented; their prepare/broadcast helpers are
// best-effort and flagged where a fuller node integration is still needed.
#include "rpc_client.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

static const char *TAG="rpc";

typedef struct { char *buf; size_t len, cap; } resp_t;
static esp_err_t on_data(esp_http_client_event_t *e){
    if(e->event_id==HTTP_EVENT_ON_DATA){
        resp_t *r=e->user_data;
        if(r->len+e->data_len < r->cap){ memcpy(r->buf+r->len,e->data,e->data_len); r->len+=e->data_len; r->buf[r->len]=0; }
    }
    return ESP_OK;
}

// Generic HTTP request. method 0=GET,1=POST. Returns bytes or -1.
static int http_req(const char *url,int method,const char *body,char *out,size_t outsz){
    resp_t r={out,0,outsz}; out[0]=0;
    esp_http_client_config_t c={ .url=url, .event_handler=on_data, .user_data=&r,
        .timeout_ms=12000, .crt_bundle_attach=esp_crt_bundle_attach };
    // TLS server certs are verified against the built-in ESP-IDF root CA bundle.
    esp_http_client_handle_t h=esp_http_client_init(&c);
    if(!h) return -1;
    if(method==1){ esp_http_client_set_method(h,HTTP_METHOD_POST);
        esp_http_client_set_header(h,"Content-Type","application/json");
        esp_http_client_set_post_field(h,body,strlen(body)); }
    esp_err_t e=esp_http_client_perform(h);
    int status=esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if(e!=ESP_OK||status<200||status>=300){ ESP_LOGW(TAG,"http %s status=%d err=%d",url,status,e); return -1; }
    return (int)r.len;
}

// POST a raw (non-JSON) text body; capture the text response. Returns len or -1.
static int http_req_raw(const char *url,const char *body,char *out,size_t outsz){
    resp_t r={out,0,outsz}; out[0]=0;
    esp_http_client_config_t c={ .url=url, .event_handler=on_data, .user_data=&r,
        .timeout_ms=12000, .crt_bundle_attach=esp_crt_bundle_attach,
        .transport_type=HTTP_TRANSPORT_OVER_SSL };
    esp_http_client_handle_t h=esp_http_client_init(&c); if(!h) return -1;
    esp_http_client_set_method(h,HTTP_METHOD_POST);
    esp_http_client_set_header(h,"Content-Type","text/plain");
    esp_http_client_set_post_field(h,body,strlen(body));
    esp_err_t e=esp_http_client_perform(h);
    int status=esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    if(e!=ESP_OK||status<200||status>=300){ ESP_LOGW(TAG,"raw http %s status=%d",url,status); return -1; }
    return (int)r.len;
}

// ---- hex-wei (0x...) -> decimal string ----
static void hexwei_to_dec(const char *hex,char *out,size_t outsz){
    if(hex[0]=='0'&&(hex[1]=='x'||hex[1]=='X')) hex+=2;
    uint8_t dec[80]; int dl=1; dec[0]=0;
    for(const char *p=hex;*p;p++){
        int v = (*p>='0'&&*p<='9')?*p-'0':(*p>='a'&&*p<='f')?*p-'a'+10:(*p>='A'&&*p<='F')?*p-'A'+10:-1;
        if(v<0) continue;
        int carry=v;
        for(int i=0;i<dl;i++){ int x=dec[i]*16+carry; dec[i]=x%10; carry=x/10; }
        while(carry){ dec[dl++]=carry%10; carry/=10; }
    }
    size_t o=0; for(int i=dl-1;i>=0&&o<outsz-1;i--) out[o++]='0'+dec[i]; out[o]=0;
    if(o==0) strlcpy(out,"0",outsz);
}

static dv_err_t evm_rpc(const char *url,const char *method,const char *params,char *result,size_t rsz){
    char body[512]; snprintf(body,sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}",method,params?params:"[]");
    char resp[2048]; if(http_req(url,1,body,resp,sizeof(resp))<0) return DV_ERR;
    cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
    cJSON *r=cJSON_GetObjectItem(j,"result");
    dv_err_t rc=DV_ERR;
    if(cJSON_IsString(r)){ strlcpy(result,r->valuestring,rsz); rc=DV_OK; }
    cJSON_Delete(j);
    return rc;
}

dv_err_t rpc_get_balance(const dv_network_t *n,const char *address,char *out,size_t outsz){
    switch(n->family){
    case DV_CHAIN_EVM:{
        char params[128]; snprintf(params,sizeof(params),"[\"%s\",\"latest\"]",address);
        char hex[80]; if(evm_rpc(n->rpc_url,"eth_getBalance",params,hex,sizeof(hex))!=DV_OK) return DV_ERR;
        hexwei_to_dec(hex,out,outsz); return DV_OK; }
    case DV_CHAIN_SOLANA:{
        char params[128]; snprintf(params,sizeof(params),"[\"%s\"]",address);
        char body[256]; snprintf(body,sizeof(body),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getBalance\",\"params\":%s}",params);
        char resp[1024]; if(http_req(n->rpc_url,1,body,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *v=cJSON_GetObjectItem(cJSON_GetObjectItem(j,"result"),"value");
        if(cJSON_IsNumber(v)) snprintf(out,outsz,"%llu",(unsigned long long)v->valuedouble);
        cJSON_Delete(j); return v?DV_OK:DV_ERR; }
    case DV_CHAIN_BITCOIN:{
        char url[256]; snprintf(url,sizeof(url),"%s/address/%s",n->rpc_url,address);
        char resp[2048]; if(http_req(url,0,NULL,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *cs=cJSON_GetObjectItem(j,"chain_stats");
        long long funded=0,spent=0;
        if(cs){ funded=(long long)cJSON_GetObjectItem(cs,"funded_txo_sum")->valuedouble;
                spent=(long long)cJSON_GetObjectItem(cs,"spent_txo_sum")->valuedouble; }
        snprintf(out,outsz,"%lld",funded-spent); cJSON_Delete(j); return DV_OK; }
    case DV_CHAIN_TRON:{
        char url[256]; snprintf(url,sizeof(url),"%s/v1/accounts/%s",n->rpc_url,address);
        char resp[2048]; if(http_req(url,0,NULL,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *data=cJSON_GetObjectItem(j,"data");
        long long bal=0;
        if(cJSON_IsArray(data)&&cJSON_GetArraySize(data)>0){
            cJSON *b=cJSON_GetObjectItem(cJSON_GetArrayItem(data,0),"balance");
            if(cJSON_IsNumber(b)) bal=(long long)b->valuedouble; }
        snprintf(out,outsz,"%lld",bal); cJSON_Delete(j); return DV_OK; }
    default: return DV_ERR_UNSUPPORTED_CHAIN;
    }
}

dv_err_t rpc_get_token_balance(const dv_network_t *n,const char *contract,
                               const char *address,uint8_t decimals,char *out,size_t outsz){
    (void)decimals;
    strlcpy(out,"0",outsz);
    switch(n->family){
    case DV_CHAIN_EVM:{
        // ERC20 balanceOf(address): selector 70a08231 + 32-byte left-zero-padded address.
        char a[65]; memset(a,'0',64); a[64]=0;
        const char *s=address; if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
        size_t al=strlen(s); if(al>40)al=40;
        memcpy(a+64-al,s,al);
        char data[80]; snprintf(data,sizeof(data),"0x70a08231%s",a);
        char params[256]; snprintf(params,sizeof(params),
            "[{\"to\":\"%s\",\"data\":\"%s\"},\"latest\"]",contract,data);
        char hex[80]; if(evm_rpc(n->rpc_url,"eth_call",params,hex,sizeof(hex))!=DV_OK) return DV_ERR;
        hexwei_to_dec(hex,out,outsz); return DV_OK; }
    case DV_CHAIN_TRON:{
        // TronGrid account endpoint returns a trc20 map [{contract:balance}].
        char url[256]; snprintf(url,sizeof(url),"%s/v1/accounts/%s",n->rpc_url,address);
        char resp[4096]; if(http_req(url,0,NULL,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *data=cJSON_GetObjectItem(j,"data");
        if(cJSON_IsArray(data)&&cJSON_GetArraySize(data)>0){
            cJSON *trc20=cJSON_GetObjectItem(cJSON_GetArrayItem(data,0),"trc20");
            if(cJSON_IsArray(trc20)){
                cJSON *entry; cJSON_ArrayForEach(entry,trc20){
                    cJSON *kv=entry->child; // {contract: "balance"}
                    if(kv&&kv->string&&!strcmp(kv->string,contract)&&cJSON_IsString(kv))
                        strlcpy(out,kv->valuestring,outsz);
                }
            }
        }
        cJSON_Delete(j); return DV_OK; }
    case DV_CHAIN_SOLANA:
        // SPL getTokenAccountsByOwner parsing: TODO. Returns "0" gracefully so
        // the UI still renders the token row.
        return DV_OK;
    default: return DV_ERR_UNSUPPORTED_CHAIN;
    }
}

// hex-encode into out (no 0x prefix). Returns chars written.
static size_t to_hex(const uint8_t *in,size_t len,char *out,size_t cap){
    static const char *d="0123456789abcdef"; size_t o=0;
    for(size_t i=0;i<len && o+2<cap;i++){ out[o++]=d[in[i]>>4]; out[o++]=d[in[i]&0xf]; }
    out[o]=0; return o;
}

dv_err_t rpc_prepare_tx(const dv_network_t *n,dv_unsigned_tx_t *tx){
    switch(n->family){
    case DV_CHAIN_EVM:{
        tx->evm_chain_id=n->evm_chain_id;
        // nonce = eth_getTransactionCount(from,"pending")
        if(tx->from[0]){
            char params[128]; snprintf(params,sizeof(params),"[\"%s\",\"pending\"]",tx->from);
            char hx[40]; if(evm_rpc(n->rpc_url,"eth_getTransactionCount",params,hx,sizeof(hx))==DV_OK)
                tx->nonce=strtoull((hx[0]=='0'&&hx[1]=='x')?hx+2:hx,NULL,16);
        }
        char gp[80]; if(evm_rpc(n->rpc_url,"eth_gasPrice",NULL,gp,sizeof(gp))==DV_OK){
            unsigned long long g=strtoull(gp+2,NULL,16);
            int len=0; for(int i=31;i>=0;i--){ tx->gas_price[i]=g&0xff; g>>=8; if(tx->gas_price[i])len=32-i; }
            tx->gas_price_len=len?len:1;
        }
        if(tx->gas_limit==0) tx->gas_limit = tx->token_contract[0]?60000:21000;
        return DV_OK; }
    case DV_CHAIN_SOLANA:{
        // recent blockhash for the transfer message
        char body[160]; snprintf(body,sizeof(body),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getLatestBlockhash\",\"params\":[{\"commitment\":\"finalized\"}]}");
        char resp[1024]; if(http_req(n->rpc_url,1,body,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *bh=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(j,"result"),"value"),"blockhash");
        dv_err_t rc=DV_ERR;
        if(cJSON_IsString(bh)){ size_t bl=sizeof(tx->ref_blockhash);
            extern bool b58tobin(void*,size_t*,const char*);
            uint8_t tmp[64]; size_t tl=sizeof(tmp);
            if(b58tobin(tmp,&tl,bh->valuestring)&&tl>=32){ memcpy(tx->ref_blockhash,tmp+tl-32,32); rc=DV_OK; } }
        cJSON_Delete(j); return rc; }
    case DV_CHAIN_TRON:{
        // reference block from getnowblock: ref_block_bytes = bytes 6-8 of height,
        // ref_block_hash = bytes 8-16 of blockID; expiration/timestamp from header.
        char body[80]="{}";
        char url[200]; snprintf(url,sizeof(url),"%s/wallet/getnowblock",n->rpc_url);
        char resp[4096]; if(http_req(url,1,body,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *hdr=cJSON_GetObjectItem(cJSON_GetObjectItem(j,"block_header"),"raw_data");
        cJSON *bid=cJSON_GetObjectItem(j,"blockID");
        dv_err_t rc=DV_ERR;
        if(cJSON_IsString(bid)&&hdr){
            // decode blockID hex (32 bytes)
            const char *h=bid->valuestring; uint8_t id[32];
            for(int i=0;i<32;i++){ int hi=h[2*i],lo=h[2*i+1];
                #define HV(c) ((c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:0)
                id[i]=(HV(hi)<<4)|HV(lo); }
            memcpy(tx->tron_ref_block, id+6, 2);      // ref_block_bytes
            memcpy(tx->tron_ref_block+2, id+8, 8);    // ref_block_hash
            long long ts=(long long)cJSON_GetObjectItem(hdr,"timestamp")->valuedouble;
            tx->tron_timestamp=(uint64_t)ts;
            tx->tron_expiration=(uint64_t)ts+60000;   // +60s
            rc=DV_OK;
        }
        cJSON_Delete(j); return rc; }
    case DV_CHAIN_BITCOIN:{
        // pick one confirmed UTXO large enough and pack the compact input the
        // BTC signer expects (txid[32] LE, vout[4] LE, amount[8] LE, fee[8] LE).
        if(!tx->from[0]) return DV_ERR_BAD_TX;
        char url[256]; snprintf(url,sizeof(url),"%s/address/%s/utxo",n->rpc_url,tx->from);
        char resp[4096]; if(http_req(url,0,NULL,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *arr=cJSON_Parse(resp); if(!cJSON_IsArray(arr)){ if(arr)cJSON_Delete(arr); return DV_ERR; }
        uint64_t need=0; for(size_t i=0;i<tx->value_len&&i<8;i++) need=(need<<8)|tx->value[i];
        uint64_t fee=2000; // sat, simple flat fee (single input P2WPKH)
        static uint8_t inbuf[52]; dv_err_t rc=DV_ERR;
        cJSON *u; cJSON_ArrayForEach(u,arr){
            cJSON *st=cJSON_GetObjectItem(u,"status");
            if(st && !cJSON_IsTrue(cJSON_GetObjectItem(st,"confirmed"))) continue;
            uint64_t val=(uint64_t)cJSON_GetObjectItem(u,"value")->valuedouble;
            if(val < need+fee) continue;
            const char *txh=cJSON_GetObjectItem(u,"txid")->valuestring;
            uint32_t vout=(uint32_t)cJSON_GetObjectItem(u,"vout")->valuedouble;
            // txid hex is big-endian display order; the wire wants internal (LE) order.
            for(int i=0;i<32;i++){ int hi=txh[2*(31-i)],lo=txh[2*(31-i)+1];
                inbuf[i]=(HV(hi)<<4)|HV(lo); }
            inbuf[32]=vout&0xff; inbuf[33]=(vout>>8)&0xff; inbuf[34]=(vout>>16)&0xff; inbuf[35]=(vout>>24)&0xff;
            for(int i=0;i<8;i++) inbuf[36+i]=(val>>(8*i))&0xff;
            for(int i=0;i<8;i++) inbuf[44+i]=(fee>>(8*i))&0xff;
            tx->psbt=inbuf; tx->psbt_len=52; rc=DV_OK; break;
        }
        cJSON_Delete(arr); return rc; }
    default: return DV_OK;
    }
    #undef HV
}

dv_err_t rpc_broadcast(const dv_network_t *n,const uint8_t *tx,size_t len,char *txid,size_t sz){
    switch(n->family){
    case DV_CHAIN_EVM:{
        char hex[2100]; hex[0]='0'; hex[1]='x'; to_hex(tx,len,hex+2,sizeof(hex)-2);
        char params[2200]; snprintf(params,sizeof(params),"[\"%s\"]",hex);
        return evm_rpc(n->rpc_url,"eth_sendRawTransaction",params,txid,sz); }
    case DV_CHAIN_BITCOIN:{
        // Blockstream/Esplora: POST raw tx hex to /tx, returns the txid.
        char hex[2100]; to_hex(tx,len,hex,sizeof(hex));
        char url[200]; snprintf(url,sizeof(url),"%s/tx",n->rpc_url);
        // reuse http_req with a raw (non-JSON) body via POST
        if(http_req_raw(url,hex,txid,sz)<0) return DV_ERR;
        return DV_OK; }
    case DV_CHAIN_SOLANA:{
        // sendTransaction with base64-encoded wire transaction.
        unsigned char b64[1400]; size_t bl=0;
        mbedtls_base64_encode(b64,sizeof(b64),&bl,tx,len); b64[bl]=0;
        char body[1700]; snprintf(body,sizeof(body),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"sendTransaction\",\"params\":[\"%s\",{\"encoding\":\"base64\"}]}",b64);
        char resp[1024]; if(http_req(n->rpc_url,1,body,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); if(!j) return DV_ERR;
        cJSON *r=cJSON_GetObjectItem(j,"result"); dv_err_t rc=DV_ERR;
        if(cJSON_IsString(r)){ strlcpy(txid,r->valuestring,sz); rc=DV_OK; }
        cJSON_Delete(j); return rc; }
    case DV_CHAIN_TRON:{
        // Our serialized form is [2-byte raw_len][raw_data][64-byte sig][1-byte v].
        if(len<67) return DV_ERR_BAD_TX;
        size_t rawlen=((size_t)tx[0]<<8)|tx[1];
        if(rawlen+67>len+2 || rawlen+3>len) return DV_ERR_BAD_TX;
        const uint8_t *raw=tx+2; const uint8_t *sig=tx+2+rawlen;
        char rawhex[1200]; to_hex(raw,rawlen,rawhex,sizeof(rawhex));
        char sighex[140]; to_hex(sig,65,sighex,sizeof(sighex));   // 64 sig + v
        // txID = sha256(raw_data)
        extern void sha256_Raw(const uint8_t*,size_t,uint8_t*);
        uint8_t id[32]; sha256_Raw(raw,rawlen,id); char idhex[70]; to_hex(id,32,idhex,sizeof(idhex));
        char body[1500]; snprintf(body,sizeof(body),
            "{\"txID\":\"%s\",\"raw_data_hex\":\"%s\",\"signature\":[\"%s\"]}",idhex,rawhex,sighex);
        char url[200]; snprintf(url,sizeof(url),"%s/wallet/broadcasttransaction",n->rpc_url);
        char resp[1024]; if(http_req(url,1,body,resp,sizeof(resp))<0) return DV_ERR;
        cJSON *j=cJSON_Parse(resp); dv_err_t rc=DV_ERR;
        if(j){ cJSON *ok=cJSON_GetObjectItem(j,"result");
            if(cJSON_IsTrue(ok)||cJSON_GetObjectItem(j,"txid")){ strlcpy(txid,idhex,sz); rc=DV_OK; }
            cJSON_Delete(j); }
        return rc; }
    default:
        strlcpy(txid,"unsupported-chain",sz); return DV_ERR_UNSUPPORTED_CHAIN;
    }
}
