// DibaVault — host correctness test for chain address derivation. GPL-3.0.
// Runs on a normal PC (the chains/ and trezor-crypto code is pure C). Derives
// addresses for the canonical BIP39 test mnemonic and checks them against
// known-good values, so wallet correctness can be validated without hardware.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bip39.h"
#include "bip32.h"
#include "curves.h"
#include "chains.h"

// Test-only RNG (the real firmware uses the ESP32 hardware TRNG).
uint32_t random32(void) { return (uint32_t)rand(); }
void random_buffer(uint8_t *buf, size_t len) { for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)rand(); }

extern const dv_chain_ops_t dv_chain_ops_btc, dv_chain_ops_evm, dv_chain_ops_trx, dv_chain_ops_sol;

#define H(i) (0x80000000u | (uint32_t)(i))

static void derive(const uint8_t *seed, const char *curve,
                   const uint32_t *idx, int depth, HDNode *node) {
    hdnode_from_seed(seed, 64, curve, node);
    for (int i = 0; i < depth; i++) hdnode_private_ckd(node, idx[i]);
    hdnode_fill_public_key(node);
}

static int check(const char *label, const char *got, const char *expect) {
    if (expect && strcmp(got, expect) != 0) {
        printf("  %-4s FAIL: got %s\n       expected %s\n", label, got, expect);
        return 1;
    }
    printf("  %-4s %s%s\n", label, got, expect ? "  (matches vector)" : "");
    return 0;
}

int main(void) {
    // The canonical all-"abandon" BIP39 test mnemonic.
    const char *m = "abandon abandon abandon abandon abandon abandon "
                    "abandon abandon abandon abandon abandon about";
    if (!mnemonic_check(m)) { printf("mnemonic checksum invalid\n"); return 2; }

    uint8_t seed[64];
    mnemonic_to_seed(m, "", seed, NULL);
    char addr[128];
    HDNode n;
    int fails = 0;

    printf("DibaVault host address-derivation test\n");
    printf("mnemonic: abandon x11 about\n\n");

    // EVM  m/44'/60'/0'/0/0  — well-known vector.
    { uint32_t p[] = { H(44), H(60), H(0), 0, 0 }; derive(seed, SECP256K1_NAME, p, 5, &n);
      dv_chain_ops_evm.format_address(n.public_key, 33, 1, addr, sizeof(addr));
      fails += check("EVM", addr, "0x9858EfFD232B4033E47d90003D41EC34EcaEda94"); }

    // BTC  m/84'/0'/0'/0/0  (BIP84 native segwit) — well-known vector.
    { uint32_t p[] = { H(84), H(0), H(0), 0, 0 }; derive(seed, SECP256K1_NAME, p, 5, &n);
      dv_chain_ops_btc.format_address(n.public_key, 33, 0, addr, sizeof(addr));
      fails += check("BTC", addr, "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"); }

    // TRX  m/44'/195'/0'/0/0 — well-known vector.
    { uint32_t p[] = { H(44), H(195), H(0), 0, 0 }; derive(seed, SECP256K1_NAME, p, 5, &n);
      dv_chain_ops_trx.format_address(n.public_key, 33, 0, addr, sizeof(addr));
      fails += check("TRX", addr, "TUEZSdKsoDHQMeZwihtdoBiN46zxhGWYdH"); }

    // SOL  m/44'/501'/0'/0' (ed25519, all hardened) — well-known vector.
    { uint32_t p[] = { H(44), H(501), H(0), H(0) }; derive(seed, ED25519_NAME, p, 4, &n);
      dv_chain_ops_sol.format_address(n.public_key + 1, 32, 0, addr, sizeof(addr));
      fails += check("SOL", addr, "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk"); }

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", fails);
    return fails ? 1 : 0;
}
