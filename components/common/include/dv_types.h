// DibaVault — common types and error codes
// Copyright (c) dibachain. GPL-3.0.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Error codes. 0 == success. Negative == failure. Never leak key material via
// error strings — codes are safe to surface over the network.
// ---------------------------------------------------------------------------
typedef enum {
    DV_OK = 0,
    DV_ERR = -1,
    DV_ERR_INVALID_ARG = -2,
    DV_ERR_NOT_PROVISIONED = -3,      // no seed yet
    DV_ERR_ALREADY_PROVISIONED = -4,  // seed exists; refuse to overwrite
    DV_ERR_LOCKED = -5,               // vault locked (needs unlock/PIN)
    DV_ERR_BAD_PIN = -6,
    DV_ERR_PIN_LOCKOUT = -7,          // too many attempts
    DV_ERR_USER_REJECTED = -8,        // physical confirmation denied
    DV_ERR_TIMEOUT = -9,              // confirmation timed out
    DV_ERR_UNSUPPORTED_CHAIN = -10,
    DV_ERR_BAD_TX = -11,              // malformed unsigned tx
    DV_ERR_STORAGE = -12,
    DV_ERR_CRYPTO = -13,
    DV_ERR_NO_MEM = -14,
    DV_ERR_BOUNDARY = -15,            // IPC boundary violation (key requested)
} dv_err_t;

// ---------------------------------------------------------------------------
// Supported chain families. secp256k1 curve for BTC/EVM/TRX; ed25519 for SOL.
// ---------------------------------------------------------------------------
typedef enum {
    DV_CHAIN_BITCOIN = 0,   // BIP44 coin 0,  secp256k1, P2WPKH default
    DV_CHAIN_EVM     = 1,   // BIP44 coin 60, secp256k1, keccak256 addressing
    DV_CHAIN_TRON    = 2,   // BIP44 coin 195, secp256k1, base58check(0x41)
    DV_CHAIN_SOLANA  = 3,   // BIP44 coin 501, ed25519, base58 pubkey
    DV_CHAIN__COUNT
} dv_chain_t;

// Address encodings we render for the UI.
typedef enum {
    DV_ADDR_P2WPKH,     // bc1... (native segwit)
    DV_ADDR_P2PKH,      // 1...
    DV_ADDR_EVM_HEX,    // 0x... (EIP-55 checksummed)
    DV_ADDR_TRON_B58,   // T...
    DV_ADDR_SOL_B58,    // base58 ed25519 pubkey
} dv_addr_kind_t;

// Sizes.
#define DV_SEED_BYTES        64      // BIP39 seed
#define DV_ENTROPY_BYTES_MAX 32      // up to 24 words
#define DV_PUBKEY_MAX        65      // uncompressed secp256k1
#define DV_SIG_MAX           72      // DER-ish upper bound; ed25519 uses 64
#define DV_ADDR_STR_MAX      96
#define DV_MNEMONIC_MAX      256     // 24 words + spaces
#define DV_DERIV_PATH_MAX    10      // hardened/non-hardened components

// A BIP32 derivation path (e.g. m/44'/60'/0'/0/0). Hardened bit = 0x80000000.
typedef struct {
    uint32_t index[DV_DERIV_PATH_MAX];
    uint8_t  depth;
} dv_path_t;

// A public account descriptor. Contains NO private material — safe to hand to
// the connectivity layer.
typedef struct {
    dv_chain_t     chain;
    dv_path_t      path;
    uint8_t        pubkey[DV_PUBKEY_MAX];
    size_t         pubkey_len;
    char           address[DV_ADDR_STR_MAX];
    uint64_t       evm_chain_id;   // only for DV_CHAIN_EVM (1 = mainnet, ...)
} dv_account_pub_t;

// ---------------------------------------------------------------------------
// Transaction types are declared here (shared header) so both chains.h and
// vault_ipc.h can use them without a circular include.
// ---------------------------------------------------------------------------

// A minimal, chain-agnostic description of an unsigned transfer. The web/RPC
// layer fills this in; the vault re-validates and recomputes everything from it.
typedef struct {
    dv_chain_t chain;
    uint64_t   evm_chain_id;
    char       to[DV_ADDR_STR_MAX];
    char       from[DV_ADDR_STR_MAX];   // sender address (public; set by connectivity
                                        // for nonce/UTXO lookup — vault re-derives the signer)
    uint8_t    value[32];       // big-endian amount, base units (wei/sat/sun/lamports)
    size_t     value_len;
    uint64_t   nonce;           // EVM / TRON
    uint64_t   gas_limit;       // EVM
    uint8_t    gas_price[32];   // EVM (big-endian wei) / maxFeePerGas for 1559
    uint8_t    priority_fee[32];
    size_t     gas_price_len;
    size_t     priority_fee_len;
    bool       eip1559;         // EVM tx type
    // Token transfer (ERC20 / TRC20 / SPL).
    char       token_contract[DV_ADDR_STR_MAX];
    uint8_t    token_decimals;
    // Bitcoin: PSBT-style inputs.
    const uint8_t *psbt;
    size_t         psbt_len;
    // Solana / Tron references.
    uint8_t    ref_blockhash[32];   // Solana recent blockhash
    uint8_t    tron_ref_block[16];
    uint64_t   tron_expiration;
    uint64_t   tron_timestamp;
} dv_unsigned_tx_t;

// A decoded, human-reviewable transaction shown on the confirmation screen.
typedef struct {
    dv_chain_t chain;
    dv_path_t  path;
    uint64_t   evm_chain_id;
    char       to[DV_ADDR_STR_MAX];
    char       amount[40];      // decimal string in display units
    char       symbol[12];      // "BTC","ETH","TRX","SOL","USDT"...
    char       fee[40];
    char       contract[DV_ADDR_STR_MAX];
    uint32_t   nonce;
    uint64_t   gas_limit;
    char       gas_price[40];
} dv_tx_review_t;

#ifdef __cplusplus
}
#endif
