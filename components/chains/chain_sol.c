// DibaVault — Solana chain. GPL-3.0. dibachain.
// Address: base58(ed25519 pubkey). Signing: ed25519 over a legacy message with
// a single SystemProgram::transfer instruction. SPL-token transfers are a TODO.
#include "chains.h"
#include <string.h>
#include <stdio.h>
#include "base58.h"
#include "memzero.h"

// b58enc / b58tobin are the raw (no-checksum) base58 codecs Solana uses.
extern bool b58enc(char *b58, size_t *b58sz, const void *data, size_t binsz);
extern bool b58tobin(void *bin, size_t *binszp, const char *b58);

static dv_err_t sol_format_address(const uint8_t *pub, size_t publen,
                                   uint64_t cid, char *out, size_t sz) {
    (void)cid;
    if (publen != 32) return DV_ERR_INVALID_ARG;
    size_t osz = sz;
    if (!b58enc(out, &osz, pub, 32)) return DV_ERR_CRYPTO;
    return DV_OK;
}

// shortvec (compact-u16) for small counts.
static void put_shortvec(uint8_t *b, size_t *p, uint16_t v) {
    do { uint8_t e=v&0x7f; v>>=7; if(v)e|=0x80; b[(*p)++]=e; } while(v);
}

// Build the legacy message that ed25519 signs.
static int build_message(const dv_unsigned_tx_t *tx, const uint8_t signer32[32],
                         uint8_t *msg, size_t cap) {
    uint8_t to32[64]; size_t tlen=sizeof(to32);
    if (!b58tobin(to32,&tlen,tx->to) || tlen!=32) return -1;
    // b58tobin writes right-aligned into the buffer end; normalize.
    uint8_t to[32]; memcpy(to,to32+(tlen>=32?tlen-32:0),32);
    uint8_t sys[32]; memset(sys,0,32);                 // System Program = all zeros
    uint64_t lamports=0; for(size_t i=0;i<tx->value_len&&i<8;i++) lamports=(lamports<<8)|tx->value[i];

    size_t p=0;
    msg[p++]=1; msg[p++]=0; msg[p++]=1;                // header
    put_shortvec(msg,&p,3);
    memcpy(msg+p,signer32,32); p+=32;
    memcpy(msg+p,to,32); p+=32;
    memcpy(msg+p,sys,32); p+=32;
    memcpy(msg+p,tx->ref_blockhash,32); p+=32;         // recent blockhash
    put_shortvec(msg,&p,1);                            // 1 instruction
    msg[p++]=2;                                        // program id index -> sys
    put_shortvec(msg,&p,2); msg[p++]=0; msg[p++]=1;    // account indices [from,to]
    put_shortvec(msg,&p,12);                           // data length
    msg[p++]=2; msg[p++]=0; msg[p++]=0; msg[p++]=0;    // SystemInstruction::Transfer = 2 (u32 LE)
    for(int i=0;i<8;i++){ msg[p++]=lamports&0xff; lamports>>=8; }  // u64 LE
    if (p>cap) return -1;
    return (int)p;
}

static dv_err_t sol_sighash(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                            size_t publen, uint8_t *out, size_t *out_len, bool *is_prehash) {
    if (publen!=32) return DV_ERR_INVALID_ARG;
    int n=build_message(tx,pub,out,*out_len);
    if (n<0) return DV_ERR_BAD_TX;
    *out_len=(size_t)n; *is_prehash=false;             // ed25519 signs the full message
    return DV_OK;
}

static dv_err_t sol_serialize(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                              size_t publen, const uint8_t *sig, size_t siglen,
                              uint8_t recid, uint8_t *out, size_t *out_len, size_t cap) {
    (void)recid;
    if (publen!=32 || siglen<64) return DV_ERR_CRYPTO;
    uint8_t msg[256]; int mlen=build_message(tx,pub,msg,sizeof(msg));
    if (mlen<0) return DV_ERR_BAD_TX;
    size_t p=0;
    put_shortvec(out,&p,1);                            // 1 signature
    memcpy(out+p,sig,64); p+=64;
    if (p+(size_t)mlen>cap) return DV_ERR_NO_MEM;
    memcpy(out+p,msg,mlen); p+=mlen;
    *out_len=p;
    memzero(msg,sizeof(msg));
    return DV_OK;
}

static dv_err_t sol_describe(const dv_unsigned_tx_t *tx, dv_tx_review_t *out) {
    strlcpy(out->to,tx->to,sizeof(out->to));
    strlcpy(out->symbol,"SOL",sizeof(out->symbol));
    uint64_t l=0; for(size_t i=0;i<tx->value_len&&i<8;i++)l=(l<<8)|tx->value[i];
    snprintf(out->amount,sizeof(out->amount),"%llu lamports",(unsigned long long)l);
    strlcpy(out->fee,"~5000 lamports",sizeof(out->fee));
    return DV_OK;
}

const dv_chain_ops_t dv_chain_ops_sol = {
    .chain=DV_CHAIN_SOLANA, .name="Solana", .bip44_coin=501, .hardened_change=true,
    .format_address=sol_format_address, .sighash=sol_sighash,
    .serialize_signed=sol_serialize, .describe=sol_describe,
};
