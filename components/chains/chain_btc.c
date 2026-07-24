// DibaVault — Bitcoin chain. GPL-3.0. dibachain.
// Address: BIP84 native SegWit (bech32 / P2WPKH).
// Signing: BIP143 sighash. The MVP path supports a single P2WPKH input with an
// optional change output back to the signer; multi-input / full PSBT is a
// documented TODO (see docs/ARCHITECTURE.md "Bitcoin").
#include "chains.h"
#include <string.h>
#include <stdio.h>
#include "ecdsa.h"
#include "secp256k1.h"
#include "hasher.h"
#include "segwit_addr.h"
#include "sha2.h"
#include "memzero.h"

// Compact single-input descriptor packed by the connectivity layer into psbt[]:
//   off 0  : [32] prevout txid (internal/little-endian byte order)
//   off 32 : [4]  prevout vout (LE)
//   off 36 : [8]  input amount in sats (LE)
//   off 44 : [8]  fee in sats (LE)
// The recipient/value come from tx->to / tx->value. Change (input-value-fee)
// returns to the signer's own address.
#define BTC_IN_LEN 52

static void dsha256(const uint8_t *d, size_t n, uint8_t out[32]) {
    uint8_t t[32]; sha256_Raw(d,n,t); sha256_Raw(t,32,out);
}
static uint64_t rd_le64(const uint8_t *p){uint64_t v=0;for(int i=7;i>=0;i--)v=(v<<8)|p[i];return v;}
static uint32_t rd_le32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static void wr_le64(uint8_t *p,uint64_t v){for(int i=0;i<8;i++){p[i]=v&0xff;v>>=8;}}
static void wr_le32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++){p[i]=v&0xff;v>>=8;}}

static dv_err_t pubkey_hash160(const uint8_t *pub, size_t publen, uint8_t out[20]) {
    uint8_t comp[33];
    if (publen==33) memcpy(comp,pub,33);
    else return DV_ERR_INVALID_ARG;
    ecdsa_get_pubkeyhash(comp, HASHER_SHA2_RIPEMD, out);
    return DV_OK;
}

static dv_err_t btc_format_address(const uint8_t *pub, size_t publen,
                                   uint64_t cid, char *out, size_t sz) {
    (void)cid;
    if (sz < 43) return DV_ERR_INVALID_ARG;
    uint8_t h160[20];
    if (pubkey_hash160(pub,publen,h160)!=DV_OK) return DV_ERR_INVALID_ARG;
    if (segwit_addr_encode(out, "bc", 0, h160, 20) != 1) return DV_ERR_CRYPTO;
    return DV_OK;
}

// Serialize a P2WPKH scriptPubKey (0x0014 || 20-byte program) for `addr`.
static int addr_to_spk(const char *addr, uint8_t *spk /* >=22 */) {
    int witver; uint8_t prog[40]; size_t plen=0;
    if (segwit_addr_decode(&witver,prog,&plen,"bc",addr)!=1) return -1;
    if (witver!=0 || plen!=20) return -1;   // only P2WPKH supported here
    spk[0]=0x00; spk[1]=0x14; memcpy(spk+2,prog,20); return 22;
}

static dv_err_t btc_sighash(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                            size_t publen, uint8_t *out, size_t *out_len, bool *is_prehash) {
    if (!tx->psbt || tx->psbt_len < BTC_IN_LEN) return DV_ERR_BAD_TX;
    const uint8_t *in = tx->psbt;
    const uint8_t *txid = in;               // 32
    uint32_t vout = rd_le32(in+32);
    uint64_t in_amt = rd_le64(in+36);
    uint64_t fee    = rd_le64(in+44);

    uint64_t send=0; for(size_t i=0;i<tx->value_len&&i<8;i++) send=(send<<8)|tx->value[i]; // big-endian sats
    if (tx->value_len==0) return DV_ERR_BAD_TX;
    if (in_amt < send + fee) return DV_ERR_BAD_TX;
    uint64_t change = in_amt - send - fee;

    uint8_t h160[20]; if (pubkey_hash160(pub,publen,h160)!=DV_OK) return DV_ERR_INVALID_ARG;

    // hashPrevouts = dSHA256(outpoint) ; hashSequence = dSHA256(0xffffffff)
    uint8_t outpoint[36]; memcpy(outpoint,txid,32); wr_le32(outpoint+32,vout);
    uint8_t hashPrevouts[32]; dsha256(outpoint,36,hashPrevouts);
    uint8_t seq[4]={0xff,0xff,0xff,0xff};
    uint8_t hashSequence[32]; dsha256(seq,4,hashSequence);

    // outputs: recipient + change(back to signer)
    uint8_t outs[2*(8+1+22)]; size_t op=0;
    uint8_t rspk[22]; if (addr_to_spk(tx->to,rspk)<0) return DV_ERR_BAD_TX;
    wr_le64(outs+op,send); op+=8; outs[op++]=22; memcpy(outs+op,rspk,22); op+=22;
    if (change>0) {
        wr_le64(outs+op,change); op+=8; outs[op++]=22;
        outs[op++]=0x00; outs[op++]=0x14; memcpy(outs+op,h160,20); op+=20;
    }
    uint8_t hashOutputs[32]; dsha256(outs,op,hashOutputs);

    // scriptCode = 0x1976a914{h160}88ac
    uint8_t scriptCode[26]={0x19,0x76,0xa9,0x14};
    memcpy(scriptCode+4,h160,20); scriptCode[24]=0x88; scriptCode[25]=0xac;

    uint8_t pre[4+32+32+36+26+8+4+32+4+4]; size_t p=0;
    wr_le32(pre+p,2); p+=4;                           // version 2
    memcpy(pre+p,hashPrevouts,32); p+=32;
    memcpy(pre+p,hashSequence,32); p+=32;
    memcpy(pre+p,outpoint,36); p+=36;
    memcpy(pre+p,scriptCode,26); p+=26;
    wr_le64(pre+p,in_amt); p+=8;
    memcpy(pre+p,seq,4); p+=4;
    memcpy(pre+p,hashOutputs,32); p+=32;
    wr_le32(pre+p,0); p+=4;                            // locktime
    wr_le32(pre+p,1); p+=4;                            // SIGHASH_ALL
    dsha256(pre,p,out);
    *out_len=32; *is_prehash=true;
    memzero(pre,sizeof(pre));
    return DV_OK;
}

static dv_err_t btc_serialize(const dv_unsigned_tx_t *tx, const uint8_t *pub,
                              size_t publen, const uint8_t *sig, size_t siglen,
                              uint8_t recid, uint8_t *out, size_t *out_len, size_t cap) {
    (void)recid;
    if (!tx->psbt || tx->psbt_len<BTC_IN_LEN || siglen<64) return DV_ERR_BAD_TX;
    const uint8_t *in=tx->psbt; const uint8_t *txid=in; uint32_t vout=rd_le32(in+32);
    uint64_t in_amt=rd_le64(in+36), fee=rd_le64(in+44);
    uint64_t send=0; for(size_t i=0;i<tx->value_len&&i<8;i++) send=(send<<8)|tx->value[i];
    uint64_t change=in_amt-send-fee;
    uint8_t h160[20]; pubkey_hash160(pub,publen,h160);

    // DER-encode signature (minimal), append SIGHASH_ALL(0x01).
    uint8_t der[72]; size_t dl=ecdsa_sig_to_der(sig,der); der[dl++]=0x01;

    size_t p=0;
    #define PUT(b) do{ if(p>=cap) return DV_ERR_NO_MEM; out[p++]=(uint8_t)(b);}while(0)
    // version
    wr_le32(out+p,2); p+=4;
    PUT(0x00); PUT(0x01);                       // segwit marker+flag
    PUT(0x01);                                   // 1 input
    memcpy(out+p,txid,32); p+=32; wr_le32(out+p,vout); p+=4;
    PUT(0x00);                                    // empty scriptSig
    out[p++]=0xff;out[p++]=0xff;out[p++]=0xff;out[p++]=0xff; // sequence
    // outputs
    uint8_t nout = change>0?2:1; PUT(nout);
    uint8_t rspk[22]; if(addr_to_spk(tx->to,rspk)<0) return DV_ERR_BAD_TX;
    wr_le64(out+p,send); p+=8; PUT(22); memcpy(out+p,rspk,22); p+=22;
    if (change>0){ wr_le64(out+p,change); p+=8; PUT(22); PUT(0x00);PUT(0x14); memcpy(out+p,h160,20); p+=20; }
    // witness: 2 items (sig, pubkey)
    PUT(0x02);
    PUT((uint8_t)dl); memcpy(out+p,der,dl); p+=dl;
    PUT(33); memcpy(out+p,pub,33); p+=33;
    // locktime
    wr_le32(out+p,0); p+=4;
    #undef PUT
    *out_len=p;
    return DV_OK;
}

static dv_err_t btc_describe(const dv_unsigned_tx_t *tx, dv_tx_review_t *out) {
    strlcpy(out->to, tx->to, sizeof(out->to));
    strlcpy(out->symbol, "BTC", sizeof(out->symbol));
    uint64_t sats=0; for(size_t i=0;i<tx->value_len&&i<8;i++) sats=(sats<<8)|tx->value[i];
    snprintf(out->amount,sizeof(out->amount),"%llu sat",(unsigned long long)sats);
    if (tx->psbt && tx->psbt_len>=BTC_IN_LEN)
        snprintf(out->fee,sizeof(out->fee),"%llu sat",(unsigned long long)rd_le64(tx->psbt+44));
    return DV_OK;
}

const dv_chain_ops_t dv_chain_ops_btc = {
    .chain=DV_CHAIN_BITCOIN, .name="Bitcoin", .bip44_coin=0, .hardened_change=false,
    .format_address=btc_format_address, .sighash=btc_sighash,
    .serialize_signed=btc_serialize, .describe=btc_describe,
};
