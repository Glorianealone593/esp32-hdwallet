// DibaVault — keystore (SECURE CORE). Copyright (c) dibachain. GPL-3.0.
#include "keystore.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/gcm.h"

// trezor-crypto
#include "bip39.h"
#include "bip32.h"
#include "pbkdf2.h"
#include "curves.h"
#include "memzero.h"
#include "sha3.h"

static const char *TAG = "keystore";

#define VAULT_PART      "nvs_secure"
#define VAULT_NS        "vault"
#define VAULT_KEY_BLOB  "blob"
#define KDF_ITERS       120000u
#define PIN_MAX_ATTEMPTS 8
#define VB_MAGIC        0xD1BA
#define VB_VERSION      1

// Combined plaintext layout: [1]=entropy_len [entropy..] [2]=pass_len [pass..]
#define ENTROPY_MAX     32
#define PASS_MAX        128
#define PT_MAX          (1 + ENTROPY_MAX + 2 + PASS_MAX)

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint16_t version;
    uint8_t  pin_attempts;      // failed attempts so far
    uint8_t  salt[16];
    uint8_t  nonce[12];
    uint8_t  tag[16];
    uint16_t ct_len;
    uint8_t  ct[PT_MAX];
} vault_blob_t;

// ---- session state (RAM only, zeroized on lock) --------------------------
static bool     s_provisioned = false;
static bool     s_unlocked    = false;
static uint8_t  s_seed[DV_SEED_BYTES];   // BIP39 seed for the session
static uint8_t  s_attempts    = 0;

// ---- NVS helpers ---------------------------------------------------------
static dv_err_t blob_load(vault_blob_t *b) {
    nvs_handle_t h;
    esp_err_t e = nvs_open_from_partition(VAULT_PART, VAULT_NS, NVS_READONLY, &h);
    if (e != ESP_OK) return DV_ERR_NOT_PROVISIONED;
    size_t len = sizeof(*b);
    e = nvs_get_blob(h, VAULT_KEY_BLOB, b, &len);
    nvs_close(h);
    if (e != ESP_OK || len != sizeof(*b) || b->magic != VB_MAGIC) return DV_ERR_NOT_PROVISIONED;
    return DV_OK;
}

static dv_err_t blob_store(const vault_blob_t *b) {
    nvs_handle_t h;
    if (nvs_open_from_partition(VAULT_PART, VAULT_NS, NVS_READWRITE, &h) != ESP_OK)
        return DV_ERR_STORAGE;
    esp_err_t e = nvs_set_blob(h, VAULT_KEY_BLOB, b, sizeof(*b));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return (e == ESP_OK) ? DV_OK : DV_ERR_STORAGE;
}

// ---- crypto helpers ------------------------------------------------------
static void derive_wrap_key(const char *pin, const uint8_t salt[16], uint8_t out[32]) {
    pbkdf2_hmac_sha256((const uint8_t *)pin, (int)strlen(pin), salt, 16,
                       KDF_ITERS, out, 32);
}

// AES-256-GCM. auth_decrypt returns nonzero when the tag/PIN is wrong.
static int gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]) {
    mbedtls_gcm_context g; mbedtls_gcm_init(&g);
    int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (r == 0)
        r = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, len, nonce, 12,
                                      NULL, 0, pt, ct, 16, tag);
    mbedtls_gcm_free(&g);
    return r;
}
static int gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *ct, size_t len, const uint8_t tag[16], uint8_t *pt) {
    mbedtls_gcm_context g; mbedtls_gcm_init(&g);
    int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (r == 0)
        r = mbedtls_gcm_auth_decrypt(&g, len, nonce, 12, NULL, 0, tag, 16, ct, pt);
    mbedtls_gcm_free(&g);
    return r;
}

// Whitened entropy: draw generously from the hardware TRNG (strong while the RF
// subsystem is active — the AP is up during provisioning) and run it through
// keccak so any structural bias in the raw source is removed before use.
static void gather_entropy(uint8_t *out, size_t len) {
    uint8_t pool[64];
    esp_fill_random(pool, sizeof(pool));       // HW RNG (RF entropy source)
    uint8_t extra[32];
    esp_fill_random(extra, sizeof(extra));
    SHA3_CTX ctx; keccak_256_Init(&ctx);
    keccak_Update(&ctx, pool, sizeof(pool));
    keccak_Update(&ctx, extra, sizeof(extra));
    uint8_t digest[32];
    keccak_Final(&ctx, digest);
    // Expand to requested length by re-hashing counter||digest.
    for (size_t off = 0; off < len; off += 32) {
        SHA3_CTX c2; keccak_256_Init(&c2);
        uint8_t ctr = (uint8_t)(off / 32);
        keccak_Update(&c2, &ctr, 1);
        keccak_Update(&c2, digest, 32);
        uint8_t block[32]; keccak_Final(&c2, block);
        size_t n = (len - off < 32) ? (len - off) : 32;
        memcpy(out + off, block, n);
        memzero(block, sizeof(block));
    }
    memzero(pool, sizeof(pool));
    memzero(extra, sizeof(extra));
    memzero(digest, sizeof(digest));
}

// ---- public API ----------------------------------------------------------
dv_err_t keystore_init(void) {
    // The secure vault lives in its own NVS partition; initialize it (separate
    // from the default `nvs` partition inited in app_main).
    esp_err_t e = nvs_flash_init_partition(VAULT_PART);
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(VAULT_PART);
        nvs_flash_init_partition(VAULT_PART);
    }
    vault_blob_t b;
    if (blob_load(&b) == DV_OK) {
        s_provisioned = true;
        s_attempts = b.pin_attempts;
        memzero(&b, sizeof(b));
    } else {
        s_provisioned = false;
    }
    s_unlocked = false;
    return DV_OK;
}

bool keystore_is_provisioned(void) { return s_provisioned; }
bool keystore_is_unlocked(void)    { return s_unlocked; }

uint8_t keystore_pin_attempts_left(void) {
    return (s_attempts >= PIN_MAX_ATTEMPTS) ? 0 : (PIN_MAX_ATTEMPTS - s_attempts);
}

// Persist entropy+passphrase encrypted under the PIN.
static dv_err_t seal(const uint8_t *entropy, uint8_t entropy_len,
                     const char *pin, const char *passphrase) {
    vault_blob_t b; memset(&b, 0, sizeof(b));
    b.magic = VB_MAGIC; b.version = VB_VERSION; b.pin_attempts = 0;
    esp_fill_random(b.salt, sizeof(b.salt));
    esp_fill_random(b.nonce, sizeof(b.nonce));

    uint8_t pt[PT_MAX]; size_t p = 0;
    pt[p++] = entropy_len;
    memcpy(pt + p, entropy, entropy_len); p += entropy_len;
    uint16_t pass_len = passphrase ? (uint16_t)strlen(passphrase) : 0;
    if (pass_len > PASS_MAX) { memzero(pt, sizeof(pt)); return DV_ERR_INVALID_ARG; }
    pt[p++] = (uint8_t)(pass_len & 0xff);
    pt[p++] = (uint8_t)(pass_len >> 8);
    if (pass_len) { memcpy(pt + p, passphrase, pass_len); p += pass_len; }
    b.ct_len = (uint16_t)p;

    uint8_t wrap[32]; derive_wrap_key(pin, b.salt, wrap);
    int r = gcm_encrypt(wrap, b.nonce, pt, p, b.ct, b.tag);
    memzero(wrap, sizeof(wrap));
    memzero(pt, sizeof(pt));
    if (r != 0) { memzero(&b, sizeof(b)); return DV_ERR_CRYPTO; }

    dv_err_t e = blob_store(&b);
    memzero(&b, sizeof(b));
    if (e == DV_OK) { s_provisioned = true; s_attempts = 0; }
    return e;
}

dv_err_t keystore_provision_new(uint8_t word_count, const char *pin,
                                const char *passphrase,
                                char *out_mnemonic, size_t out_sz) {
    if (s_provisioned) return DV_ERR_ALREADY_PROVISIONED;
    if (!pin || strlen(pin) < 4) return DV_ERR_INVALID_ARG;
    uint8_t elen;
    switch (word_count) {
        case 12: elen = 16; break;
        case 18: elen = 24; break;
        case 24: elen = 32; break;
        default: return DV_ERR_INVALID_ARG;
    }
    uint8_t entropy[ENTROPY_MAX];
    gather_entropy(entropy, elen);
    const char *m = mnemonic_from_data(entropy, elen);   // static internal buffer
    if (!m) { memzero(entropy, sizeof(entropy)); return DV_ERR_CRYPTO; }
    if (out_sz <= strlen(m)) { memzero(entropy, sizeof(entropy)); return DV_ERR_INVALID_ARG; }
    strlcpy(out_mnemonic, m, out_sz);
    mnemonic_clear();                                    // wipe trezor's static buffer

    dv_err_t e = seal(entropy, elen, pin, passphrase);
    memzero(entropy, sizeof(entropy));
    if (e != DV_OK) { memzero(out_mnemonic, out_sz); return e; }

    // Establish an unlocked session immediately so setup can derive addresses.
    return keystore_unlock(pin);
}

dv_err_t keystore_provision_import(const char *mnemonic, const char *pin,
                                   const char *passphrase) {
    if (s_provisioned) return DV_ERR_ALREADY_PROVISIONED;
    if (!pin || strlen(pin) < 4) return DV_ERR_INVALID_ARG;
    if (!mnemonic_check(mnemonic)) return DV_ERR_INVALID_ARG;
    // Recover the entropy from the mnemonic so we store the canonical form.
    uint8_t entropy[ENTROPY_MAX];
    int bits = mnemonic_to_bits(mnemonic, entropy);   // returns total bits incl checksum
    if (bits <= 0) return DV_ERR_INVALID_ARG;
    uint8_t elen = (uint8_t)((bits / 33) * 32 / 8);   // entropy bytes (11-bit words)
    if (elen == 0 || elen > ENTROPY_MAX) { memzero(entropy, sizeof(entropy)); return DV_ERR_INVALID_ARG; }
    dv_err_t e = seal(entropy, elen, pin, passphrase);
    memzero(entropy, sizeof(entropy));
    if (e != DV_OK) return e;
    return keystore_unlock(pin);
}

dv_err_t keystore_unlock(const char *pin) {
    if (!s_provisioned) return DV_ERR_NOT_PROVISIONED;
    if (keystore_pin_attempts_left() == 0) return DV_ERR_PIN_LOCKOUT;

    vault_blob_t b;
    if (blob_load(&b) != DV_OK) return DV_ERR_STORAGE;

    uint8_t wrap[32]; derive_wrap_key(pin, b.salt, wrap);
    uint8_t pt[PT_MAX];
    int r = gcm_decrypt(wrap, b.nonce, b.ct, b.ct_len, b.tag, pt);
    memzero(wrap, sizeof(wrap));

    if (r != 0) {
        // Wrong PIN: bump the persisted counter.
        b.pin_attempts = ++s_attempts;
        blob_store(&b);
        memzero(&b, sizeof(b));
        memzero(pt, sizeof(pt));
        return (keystore_pin_attempts_left() == 0) ? DV_ERR_PIN_LOCKOUT : DV_ERR_BAD_PIN;
    }

    // Correct PIN: reconstruct the seed.
    size_t p = 0;
    uint8_t elen = pt[p++];
    if (elen == 0 || elen > ENTROPY_MAX) { memzero(pt, sizeof(pt)); memzero(&b, sizeof(b)); return DV_ERR_STORAGE; }
    uint8_t entropy[ENTROPY_MAX];
    memcpy(entropy, pt + p, elen); p += elen;
    uint16_t pass_len = (uint16_t)pt[p] | ((uint16_t)pt[p + 1] << 8); p += 2;
    char passphrase[PASS_MAX + 1] = {0};
    if (pass_len && pass_len <= PASS_MAX) { memcpy(passphrase, pt + p, pass_len); passphrase[pass_len] = 0; }
    memzero(pt, sizeof(pt));

    const char *m = mnemonic_from_data(entropy, elen);
    memzero(entropy, sizeof(entropy));
    if (!m) { memzero(&b, sizeof(b)); return DV_ERR_CRYPTO; }
    mnemonic_to_seed(m, passphrase, s_seed, NULL);
    mnemonic_clear();
    memzero(passphrase, sizeof(passphrase));

    // Reset attempts on success.
    s_attempts = 0; b.pin_attempts = 0; blob_store(&b);
    memzero(&b, sizeof(b));
    s_unlocked = true;
    return DV_OK;
}

void keystore_lock(void) {
    memzero(s_seed, sizeof(s_seed));
    s_unlocked = false;
}

dv_err_t keystore_reveal_mnemonic(const char *pin, char *out, size_t out_sz) {
    if (!s_provisioned) return DV_ERR_NOT_PROVISIONED;
    if (keystore_pin_attempts_left() == 0) return DV_ERR_PIN_LOCKOUT;

    vault_blob_t b;
    if (blob_load(&b) != DV_OK) return DV_ERR_STORAGE;
    uint8_t wrap[32]; derive_wrap_key(pin, b.salt, wrap);
    uint8_t pt[PT_MAX];
    int r = gcm_decrypt(wrap, b.nonce, b.ct, b.ct_len, b.tag, pt);
    memzero(wrap, sizeof(wrap));
    if (r != 0) {
        b.pin_attempts = ++s_attempts; blob_store(&b); memzero(&b, sizeof(b));
        memzero(pt, sizeof(pt));
        return (keystore_pin_attempts_left() == 0) ? DV_ERR_PIN_LOCKOUT : DV_ERR_BAD_PIN;
    }
    s_attempts = 0; b.pin_attempts = 0; blob_store(&b); memzero(&b, sizeof(b));

    uint8_t elen = pt[0];
    if (elen == 0 || elen > ENTROPY_MAX) { memzero(pt, sizeof(pt)); return DV_ERR_STORAGE; }
    uint8_t entropy[ENTROPY_MAX];
    memcpy(entropy, pt + 1, elen);
    memzero(pt, sizeof(pt));
    const char *m = mnemonic_from_data(entropy, elen);
    memzero(entropy, sizeof(entropy));
    if (!m || out_sz <= strlen(m)) { mnemonic_clear(); return DV_ERR_CRYPTO; }
    strlcpy(out, m, out_sz);
    mnemonic_clear();
    return DV_OK;
}

dv_err_t keystore_wipe(void) {
    keystore_lock();
    nvs_handle_t h;
    if (nvs_open_from_partition(VAULT_PART, VAULT_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h); nvs_commit(h); nvs_close(h);
    }
    s_provisioned = false; s_attempts = 0;
    return DV_OK;
}

// ---- derivation & signing ------------------------------------------------
static dv_err_t make_node(dv_chain_t chain, const dv_path_t *path, HDNode *node) {
    if (!s_unlocked) return DV_ERR_LOCKED;
    const char *curve = (chain == DV_CHAIN_SOLANA) ? ED25519_NAME : SECP256K1_NAME;
    if (hdnode_from_seed(s_seed, DV_SEED_BYTES, curve, node) != 1) return DV_ERR_CRYPTO;
    for (uint8_t i = 0; i < path->depth; i++) {
        if (hdnode_private_ckd(node, path->index[i]) != 1) { memzero(node, sizeof(*node)); return DV_ERR_CRYPTO; }
    }
    hdnode_fill_public_key(node);
    return DV_OK;
}

dv_err_t keystore_derive_public(dv_chain_t chain, const dv_path_t *path,
                                uint64_t evm_chain_id, dv_account_pub_t *out) {
    HDNode node;
    dv_err_t e = make_node(chain, path, &node);
    if (e != DV_OK) return e;
    memset(out, 0, sizeof(*out));
    out->chain = chain;
    out->path = *path;
    out->evm_chain_id = evm_chain_id;
    if (chain == DV_CHAIN_SOLANA) {
        // ed25519 pubkey is node.public_key[1..33] (index 0 is a 0x00 prefix).
        memcpy(out->pubkey, node.public_key + 1, 32);
        out->pubkey_len = 32;
    } else {
        memcpy(out->pubkey, node.public_key, 33); // compressed secp256k1
        out->pubkey_len = 33;
    }
    memzero(&node, sizeof(node));
    // Address string is filled by the chain layer (chains_format_address) in signer.c/vault.
    return DV_OK;
}

dv_err_t keystore_sign_digest(dv_chain_t chain, const dv_path_t *path,
                              const uint8_t digest[32],
                              uint8_t *out_sig, size_t *out_sig_len,
                              uint8_t *out_recid) {
    if (chain == DV_CHAIN_SOLANA) return DV_ERR_UNSUPPORTED_CHAIN; // ed25519 signs full msg
    HDNode node;
    dv_err_t e = make_node(chain, path, &node);
    if (e != DV_OK) return e;
    uint8_t sig[64]; uint8_t pby = 0;
    int r = hdnode_sign_digest(&node, digest, sig, &pby, NULL);
    memzero(&node, sizeof(node));
    if (r != 0) { memzero(sig, sizeof(sig)); return DV_ERR_CRYPTO; }
    memcpy(out_sig, sig, 64);
    *out_sig_len = 64;
    if (out_recid) *out_recid = pby;
    memzero(sig, sizeof(sig));
    return DV_OK;
}

dv_err_t keystore_sign_ed25519(const dv_path_t *path,
                               const uint8_t *msg, size_t msg_len,
                               uint8_t out_sig[64]) {
    HDNode node;
    dv_err_t e = make_node(DV_CHAIN_SOLANA, path, &node);
    if (e != DV_OK) return e;
    int r = hdnode_sign(&node, msg, (uint32_t)msg_len, 0, out_sig, NULL, NULL);
    memzero(&node, sizeof(node));
    return (r == 0) ? DV_OK : DV_ERR_CRYPTO;
}
