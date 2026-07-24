// DibaVault — signing orchestration (SECURE CORE). GPL-3.0. dibachain.
#include "signer.h"
#include "keystore.h"
#include "confirm.h"
#include <string.h>
#include "esp_log.h"
#include "memzero.h"
#include "sha3.h"
#include "sha2.h"

static const char *TAG = "signer";

dv_err_t signer_sign_tx(const dv_unsigned_tx_t *tx, const dv_path_t *path,
                        bool remote, vault_resp_t *resp) {
    if (!tx || !path || !resp) return DV_ERR_INVALID_ARG;
    if (!keystore_is_unlocked()) return DV_ERR_LOCKED;

    const dv_chain_ops_t *ops = chains_get(tx->chain);
    if (!ops) return DV_ERR_UNSUPPORTED_CHAIN;

    // 1) Derive the signing pubkey (needed both for the sighash and serialize).
    dv_account_pub_t acc;
    dv_err_t e = keystore_derive_public(tx->chain, path, tx->evm_chain_id, &acc);
    if (e != DV_OK) return e;

    // 2) Build a human-readable review and require physical confirmation.
    dv_tx_review_t review;
    memset(&review, 0, sizeof(review));
    e = ops->describe(tx, &review);
    if (e != DV_OK) { memzero(&acc, sizeof(acc)); return e; }
    review.chain = tx->chain;
    review.path = *path;
    review.evm_chain_id = tx->evm_chain_id;

    e = confirm_transaction(&review, remote);
    if (e != DV_OK) { memzero(&acc, sizeof(acc)); return e; } // rejected/timeout

    // 3) Recompute the canonical signing material from the REVIEWED fields.
    uint8_t sigmsg[512]; size_t sigmsg_len = sizeof(sigmsg); bool is_prehash = true;
    e = ops->sighash(tx, acc.pubkey, acc.pubkey_len, sigmsg, &sigmsg_len, &is_prehash);
    if (e != DV_OK) { memzero(&acc, sizeof(acc)); return e; }

    // 4) Sign.
    uint8_t sig[64]; size_t sig_len = 0; uint8_t recid = 0;
    if (tx->chain == DV_CHAIN_SOLANA) {
        e = keystore_sign_ed25519(path, sigmsg, sigmsg_len, sig);
        sig_len = 64;
    } else {
        uint8_t digest[32];
        if (is_prehash && sigmsg_len == 32) {
            memcpy(digest, sigmsg, 32);
        } else {
            // Defensive: if the chain returned a preimage, hash it here. EVM/TRX
            // use keccak256; BTC uses double-SHA256 (handled inside chain).
            memzero(digest, sizeof(digest));
            memcpy(digest, sigmsg, sigmsg_len < 32 ? sigmsg_len : 32);
        }
        e = keystore_sign_digest(tx->chain, path, digest, sig, &sig_len, &recid);
        memzero(digest, sizeof(digest));
    }
    memzero(sigmsg, sizeof(sigmsg));
    if (e != DV_OK) { memzero(&acc, sizeof(acc)); memzero(sig, sizeof(sig)); return e; }

    // 5) Serialize the broadcastable transaction.
    memset(resp, 0, sizeof(*resp));
    size_t out_len = sizeof(resp->u.signed_result.signed_tx);
    e = ops->serialize_signed(tx, acc.pubkey, acc.pubkey_len, sig, sig_len, recid,
                              resp->u.signed_result.signed_tx, &out_len,
                              sizeof(resp->u.signed_result.signed_tx));
    memzero(&acc, sizeof(acc));
    if (e != DV_OK) { memzero(sig, sizeof(sig)); return e; }

    memcpy(resp->u.signed_result.sig, sig, sig_len);
    resp->u.signed_result.sig_len = sig_len;
    resp->u.signed_result.signed_tx_len = out_len;
    memzero(sig, sizeof(sig));
    resp->err = DV_OK;
    return DV_OK;
}

dv_err_t signer_sign_message(dv_chain_t chain, const dv_path_t *path,
                             const uint8_t *msg, size_t len, bool is_personal,
                             bool remote, vault_resp_t *resp) {
    if (!keystore_is_unlocked()) return DV_ERR_LOCKED;
    if (len > 256) return DV_ERR_INVALID_ARG;

    // Confirmation: show a short preview of the message.
    char preview[80];
    size_t n = len < 32 ? len : 32;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = msg[i];
        preview[i] = (c >= 32 && c < 127) ? (char)c : '.';
    }
    preview[n] = 0;
    const char *lines[2] = { "Sign message?", preview };
    dv_err_t e = confirm_prompt("Message", lines, 2, remote);
    if (e != DV_OK) return e;

    memset(resp, 0, sizeof(*resp));
    if (chain == DV_CHAIN_SOLANA) {
        e = keystore_sign_ed25519(path, msg, len, resp->u.signed_result.sig);
        resp->u.signed_result.sig_len = 64;
        return e;
    }

    // EVM personal_sign: keccak256("\x19Ethereum Signed Message:\n" + len + msg)
    uint8_t digest[32];
    if (is_personal && chain == DV_CHAIN_EVM) {
        char hdr[42];
        int hl = snprintf(hdr, sizeof(hdr), "\x19""Ethereum Signed Message:\n%u", (unsigned)len);
        SHA3_CTX c; keccak_256_Init(&c);
        keccak_Update(&c, (const uint8_t *)hdr, (size_t)hl);
        keccak_Update(&c, msg, len);
        keccak_Final(&c, digest);
    } else {
        SHA3_CTX c; keccak_256_Init(&c);
        keccak_Update(&c, msg, len);
        keccak_Final(&c, digest);
    }
    size_t sl = 0; uint8_t recid = 0;
    e = keystore_sign_digest(chain, path, digest, resp->u.signed_result.sig, &sl, &recid);
    memzero(digest, sizeof(digest));
    resp->u.signed_result.sig_len = sl;
    // Append recovery id (Ethereum v) as the 65th byte for convenience.
    if (e == DV_OK && chain == DV_CHAIN_EVM) {
        resp->u.signed_result.sig[64] = recid + 27;
        resp->u.signed_result.sig_len = 65;
    }
    return e;
}
