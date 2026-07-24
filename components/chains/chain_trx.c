// DibaVault — Tron (TRX) chain. GPL-3.0. dibachain.
// Address: base58check(0x41 || keccak256(pub)[12:]) .
// Signing: protobuf raw_data (native TransferContract), txID = sha256(raw_data),
//          recoverable 65-byte signature (r||s||v).
// TRC20 (TriggerSmartContract) is a documented TODO.
#include "chains.h"
#include <string.h>
#include <stdio.h>
#include "ecdsa.h"
#include "secp256k1.h"
#include "sha3.h"
#include "sha2.h"
#include "base58.h"
#include "hasher.h"
#include "memzero.h"

static void keccak256(const uint8_t*d,size_t n,uint8_t o[32]){SHA3_CTX c;keccak_256_Init(&c);keccak_Update(&c,d,n);keccak_Final(&c,o);}

// ---- minimal protobuf writer ----
static void pb_varint(uint8_t *b, size_t *p, uint64_t v){ while(v>=0x80){b[(*p)++]=(v&0x7f)|0x80;v>>=7;} b[(*p)++]=v&0x7f; }
static void pb_tag(uint8_t *b,size_t *p,uint32_t f,uint32_t wt){ pb_varint(b,p,(f<<3)|wt); }
static void pb_bytes(uint8_t *b,size_t *p,uint32_t f,const uint8_t *d,size_t n){ pb_tag(b,p,f,2); pb_varint(b,p,n); memcpy(b+*p,d,n); *p+=n; }
static void pb_i64(uint8_t *b,size_t *p,uint32_t f,uint64_t v){ pb_tag(b,p,f,0); pb_varint(b,p,v); }

static dv_err_t tron_addr21(const uint8_t *pub, size_t publen, uint8_t out[21]) {
    uint8_t u[65];
    if (publen==33){ if(ecdsa_uncompress_pubkey(&secp256k1,pub,u)!=1) return DV_ERR_CRYPTO; }
    else if (publen==65) memcpy(u,pub,65);
    else return DV_ERR_INVALID_ARG;
    uint8_t h[32]; keccak256(u+1,64,h);
    out[0]=0x41; memcpy(out+1,h+12,20);
    return DV_OK;
}

static dv_err_t trx_format_address(const uint8_t *pub, size_t publen,
                                   uint64_t cid, char *out, size_t sz) {
    (void)cid;
    uint8_t a21[21]; dv_err_t e=tron_addr21(pub,publen,a21); if(e) return e;
    int n=base58_encode_check(a21,21,HASHER_SHA2D,out,sz);
    return (n>0)?DV_OK:DV_ERR_CRYPTO;
}

// Build the TransferContract raw_data protobuf into `raw` (returns length).
static int build_raw(const dv_unsigned_tx_t *tx, const uint8_t owner[21], uint8_t *raw) {
    // decode recipient base58 -> 21 bytes
    uint8_t to21[64]; int tl=base58_decode_check(tx->to,HASHER_SHA2D,to21,sizeof(to21));
    if (tl!=21) return -1;
    uint64_t amount=0; for(size_t i=0;i<tx->value_len && i<8;i++) amount=(amount<<8)|tx->value[i];

    // inner TransferContract
    uint8_t inner[128]; size_t ip=0;
    pb_bytes(inner,&ip,1,owner,21);
    pb_bytes(inner,&ip,2,to21,21);
    pb_i64(inner,&ip,3,amount);

    // Any { type_url(1), value(2) }
    const char *turl="type.googleapis.com/protocol.TransferContract";
    uint8_t any[256]; size_t ap=0;
    pb_bytes(any,&ap,1,(const uint8_t*)turl,strlen(turl));
    pb_bytes(any,&ap,2,inner,ip);

    // Contract { type(1)=1, parameter(2)=Any }
    uint8_t contract[300]; size_t cp=0;
    pb_i64(contract,&cp,1,1); // TransferContract
    pb_bytes(contract,&cp,2,any,ap);

    // raw_data
    size_t p=0;
    // ref_block_bytes (field 1) + ref_block_hash (field 4): packed in tron_ref_block
    // layout: [0..2]=ref_block_bytes(2), [2..10]=ref_block_hash(8)
    pb_bytes(raw,&p,1,tx->tron_ref_block,2);
    pb_bytes(raw,&p,4,tx->tron_ref_block+2,8);
    pb_i64(raw,&p,8,tx->tron_expiration);
    pb_bytes(raw,&p,11,contract,cp);
    pb_i64(raw,&p,14,tx->tron_timestamp);
    return (int)p;
}

static dv_err_t trx_sighash(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                            size_t publen, uint8_t *out, size_t *out_len, bool *is_prehash) {
    uint8_t owner[21]; if(tron_addr21(pub,publen,owner)) return DV_ERR_CRYPTO;
    uint8_t raw[512]; int rl=build_raw(tx,owner,raw);
    if (rl<0) return DV_ERR_BAD_TX;
    sha256_Raw(raw,rl,out);      // txID
    *out_len=32; *is_prehash=true;
    memzero(raw,sizeof(raw));
    return DV_OK;
}

static dv_err_t trx_serialize(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                              size_t publen, const uint8_t *sig, size_t siglen,
                              uint8_t recid, uint8_t *out, size_t *out_len, size_t cap) {
    if (siglen<64) return DV_ERR_CRYPTO;
    uint8_t owner[21]; if(tron_addr21(pub,publen,owner)) return DV_ERR_CRYPTO;
    uint8_t raw[512]; int rl=build_raw(tx,owner,raw);
    if (rl<0) return DV_ERR_BAD_TX;
    // Output = [2-byte raw_len][raw_data][65-byte signature]. The broadcast
    // layer wraps this into the node's JSON {raw_data_hex, signature}.
    if ((size_t)(rl+67) > cap) return DV_ERR_NO_MEM;
    size_t p=0;
    out[p++]=(rl>>8)&0xff; out[p++]=rl&0xff;
    memcpy(out+p,raw,rl); p+=rl;
    memcpy(out+p,sig,64); p+=64;
    out[p++]=recid;                          // v
    *out_len=p;
    memzero(raw,sizeof(raw));
    return DV_OK;
}

static dv_err_t trx_describe(const dv_unsigned_tx_t *tx, dv_tx_review_t *out) {
    strlcpy(out->to,tx->to,sizeof(out->to));
    strlcpy(out->symbol,"TRX",sizeof(out->symbol));
    uint64_t amt=0; for(size_t i=0;i<tx->value_len&&i<8;i++)amt=(amt<<8)|tx->value[i];
    snprintf(out->amount,sizeof(out->amount),"%llu sun",(unsigned long long)amt);
    strlcpy(out->fee,"bandwidth/energy",sizeof(out->fee));
    return DV_OK;
}

const dv_chain_ops_t dv_chain_ops_trx = {
    .chain=DV_CHAIN_TRON, .name="Tron", .bip44_coin=195, .hardened_change=false,
    .format_address=trx_format_address, .sighash=trx_sighash,
    .serialize_signed=trx_serialize, .describe=trx_describe,
};
