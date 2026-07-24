// DibaVault — EVM chain (Ethereum + any EVM network by chain-id). GPL-3.0.
// secp256k1 keys, keccak256 addressing, RLP legacy + EIP-1559, native + ERC20.
#include "chains.h"
#include <string.h>
#include <stdio.h>
#include "sha3.h"
#include "ecdsa.h"
#include "secp256k1.h"
#include "memzero.h"

// ---------------- helpers ----------------
static void keccak256(const uint8_t *d, size_t n, uint8_t out[32]) {
    SHA3_CTX c; keccak_256_Init(&c); keccak_Update(&c, d, n); keccak_Final(&c, out);
}

static int hexval(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
// Parse "0x..." 20-byte address. Returns 0 on success.
static int parse_addr20(const char *s, uint8_t out[20]) {
    if (s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    if (strlen(s) != 40) return -1;
    for (int i=0;i<20;i++){int h=hexval(s[2*i]),l=hexval(s[2*i+1]); if(h<0||l<0)return -1; out[i]=(uint8_t)(h<<4|l);}
    return 0;
}

// Minimal big-endian: strip leading zero bytes. Returns significant length.
static size_t be_minimal(const uint8_t *in, size_t len, const uint8_t **start) {
    size_t i=0; while(i<len && in[i]==0) i++; *start=in+i; return len-i;
}
static size_t u64_be(uint64_t v, uint8_t out[8]) {
    for(int i=7;i>=0;i--){out[i]=v&0xff;v>>=8;} size_t i=0;while(i<8&&out[i]==0)i++; if(i==8)i=7; memmove(out,out+i,8-i); return 8-i;
}

// ---------------- RLP ----------------
static size_t rlp_len_prefix(uint8_t *out, size_t len, uint8_t base) {
    if (len <= 55) { out[0]=base+len; return 1; }
    uint8_t tmp[8]; size_t n=u64_be(len,tmp);
    out[0]=base+55+n; memcpy(out+1,tmp,n); return 1+n;
}
// Encode a byte string (already minimal for integers) as RLP into out.
static size_t rlp_str(uint8_t *out, const uint8_t *data, size_t len) {
    if (len==1 && data[0]<0x80){ out[0]=data[0]; return 1; }
    size_t p = rlp_len_prefix(out, len, 0x80);
    memcpy(out+p, data, len); return p+len;
}
static size_t rlp_uint_be(uint8_t *out, const uint8_t *be, size_t len) {
    const uint8_t *s; size_t n=be_minimal(be,len,&s);
    if (n==0){ out[0]=0x80; return 1; }           // zero -> empty string
    return rlp_str(out, s, n);
}
static size_t rlp_u64(uint8_t *out, uint64_t v) {
    if (v==0){ out[0]=0x80; return 1; }
    uint8_t tmp[8]; size_t n=u64_be(v,tmp); return rlp_str(out,tmp,n);
}

// Build the ERC20 transfer() calldata: a9059cbb + pad(to) + pad(amount).
static size_t erc20_data(const uint8_t to[20], const uint8_t *amt, size_t amt_len, uint8_t out[68]) {
    static const uint8_t sel[4]={0xa9,0x05,0x9c,0xbb};
    memcpy(out,sel,4); memset(out+4,0,64);
    memcpy(out+4+12,to,20);
    const uint8_t *s; size_t n=be_minimal(amt,amt_len,&s);
    if(n>32)n=32; memcpy(out+4+32+(32-n),s,n);
    return 68;
}

// Assemble the field list (without the outer list header). `for_signing`
// controls the legacy EIP-155 trailer (chainId,0,0) vs the final (v,r,s).
static size_t evm_fields(const dv_unsigned_tx_t *tx, uint8_t *buf,
                         bool signing, const uint8_t *r, size_t rlen,
                         const uint8_t *s, size_t slen, uint64_t vval) {
    size_t p=0;
    uint8_t to20[20]; bool have_to = (parse_addr20(tx->token_contract[0]?tx->token_contract:tx->to,to20)==0);
    uint8_t data[68]; size_t data_len=0;
    uint8_t val_be[32]; size_t val_len;
    if (tx->token_contract[0]) {
        uint8_t rcpt[20]; parse_addr20(tx->to,rcpt);
        data_len = erc20_data(rcpt, tx->value, tx->value_len, data);
        memset(val_be,0,sizeof(val_be)); val_len=0;             // value=0 for token tx
    } else {
        val_len = tx->value_len; memcpy(val_be,tx->value,val_len?val_len:0);
    }

    if (tx->eip1559) {
        p += rlp_u64(buf+p, tx->evm_chain_id);
        p += rlp_u64(buf+p, tx->nonce);
        p += rlp_uint_be(buf+p, tx->priority_fee, tx->priority_fee_len);
        p += rlp_uint_be(buf+p, tx->gas_price, tx->gas_price_len);   // maxFeePerGas
        p += rlp_u64(buf+p, tx->gas_limit);
        p += rlp_str(buf+p, to20, have_to?20:0);
        p += rlp_uint_be(buf+p, val_be, val_len);
        p += rlp_str(buf+p, data, data_len);
        // empty access list
        buf[p++]=0xc0;
        if (!signing) { p += rlp_u64(buf+p, vval); p += rlp_str(buf+p,r,rlen); p += rlp_str(buf+p,s,slen); }
    } else {
        p += rlp_u64(buf+p, tx->nonce);
        p += rlp_uint_be(buf+p, tx->gas_price, tx->gas_price_len);
        p += rlp_u64(buf+p, tx->gas_limit);
        p += rlp_str(buf+p, to20, have_to?20:0);
        p += rlp_uint_be(buf+p, val_be, val_len);
        p += rlp_str(buf+p, data, data_len);
        if (signing) { p += rlp_u64(buf+p, tx->evm_chain_id); buf[p++]=0x80; buf[p++]=0x80; }
        else { p += rlp_u64(buf+p, vval); p += rlp_str(buf+p,r,rlen); p += rlp_str(buf+p,s,slen); }
    }
    return p;
}

// Wrap fields into a list, prepend 0x02 for typed txs. Returns total length.
static size_t evm_envelope(const dv_unsigned_tx_t *tx, const uint8_t *fields,
                           size_t flen, uint8_t *out) {
    size_t p=0;
    if (tx->eip1559) out[p++]=0x02;
    uint8_t hdr[9]; size_t h=rlp_len_prefix(hdr, flen, 0xc0);
    memcpy(out+p,hdr,h); p+=h;
    memcpy(out+p,fields,flen); p+=flen;
    return p;
}

// ---------------- ops ----------------
static dv_err_t evm_format_address(const uint8_t *pub, size_t publen,
                                   uint64_t chain_id, char *out, size_t sz) {
    (void)chain_id;
    if (sz < 43) return DV_ERR_INVALID_ARG;
    uint8_t u[65];
    if (publen==33) { if(ecdsa_uncompress_pubkey(&secp256k1, pub, u)!=1) return DV_ERR_CRYPTO; }
    else if (publen==65) memcpy(u,pub,65);
    else return DV_ERR_INVALID_ARG;
    uint8_t h[32]; keccak256(u+1,64,h);
    // EIP-55 checksum
    char hex[41];
    static const char *dig="0123456789abcdef";
    for(int i=0;i<20;i++){hex[2*i]=dig[h[12+i]>>4];hex[2*i+1]=dig[h[12+i]&0xf];}
    hex[40]=0;
    uint8_t hh[32]; keccak256((const uint8_t*)hex,40,hh);
    out[0]='0'; out[1]='x';
    for(int i=0;i<40;i++){
        char c=hex[i];
        if(c>='a'&&c<='f'){ int nib=(hh[i/2]>>(4*(1-(i%2))))&0xf; if(nib>=8)c=c-'a'+'A'; }
        out[2+i]=c;
    }
    out[42]=0;
    return DV_OK;
}

static dv_err_t evm_sighash(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                            size_t publen, uint8_t *out, size_t *out_len, bool *is_prehash) {
    (void)pub;(void)publen;
    uint8_t fields[900]; size_t flen=evm_fields(tx,fields,true,NULL,0,NULL,0,0);
    uint8_t env[912]; size_t elen=evm_envelope(tx,fields,flen,env);
    keccak256(env,elen,out);
    *out_len=32; *is_prehash=true;
    memzero(fields,sizeof(fields));
    return DV_OK;
}

static dv_err_t evm_serialize(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                              size_t publen, const uint8_t *sig, size_t siglen,
                              uint8_t recid, uint8_t *out, size_t *out_len, size_t cap) {
    (void)pub;(void)publen;
    if (siglen<64) return DV_ERR_CRYPTO;
    const uint8_t *r=sig, *s=sig+32;
    uint64_t v;
    if (tx->eip1559) v = recid;                       // 0/1 for typed tx
    else             v = (uint64_t)tx->evm_chain_id*2 + 35 + recid;  // EIP-155
    uint8_t fields[960]; size_t flen=evm_fields(tx,fields,false,r,32,s,32,v);
    uint8_t env[980]; size_t elen=evm_envelope(tx,fields,flen,env);
    if (elen>cap) return DV_ERR_NO_MEM;
    memcpy(out,env,elen); *out_len=elen;
    memzero(fields,sizeof(fields));
    return DV_OK;
}

static dv_err_t evm_describe(const dv_unsigned_tx_t *tx, dv_tx_review_t *out) {
    strlcpy(out->to, tx->to, sizeof(out->to));
    strlcpy(out->symbol, tx->token_contract[0] ? "TOKEN" : "ETH", sizeof(out->symbol));
    if (tx->token_contract[0]) strlcpy(out->contract, tx->token_contract, sizeof(out->contract));
    // amount as raw big-endian hex (UI formats to decimal with decimals).
    char *p=out->amount; *p++='0'; *p++='x';
    const uint8_t *st; size_t n=be_minimal(tx->value,tx->value_len,&st);
    static const char *dg="0123456789abcdef";
    if(n==0){*p++='0';} for(size_t i=0;i<n && (size_t)(p-out->amount)<36;i++){*p++=dg[st[i]>>4];*p++=dg[st[i]&0xf];}
    *p=0;
    out->nonce=(uint32_t)tx->nonce; out->gas_limit=tx->gas_limit;
    snprintf(out->fee,sizeof(out->fee),"gas %llu", (unsigned long long)tx->gas_limit);
    return DV_OK;
}

const dv_chain_ops_t dv_chain_ops_evm = {
    .chain=DV_CHAIN_EVM, .name="EVM", .bip44_coin=60, .hardened_change=false,
    .format_address=evm_format_address, .sighash=evm_sighash,
    .serialize_signed=evm_serialize, .describe=evm_describe,
};
