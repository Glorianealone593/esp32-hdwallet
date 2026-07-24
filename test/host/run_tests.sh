#!/usr/bin/env bash
# DibaVault — host test runner (dibachain).
# Compiles the pure-C chain + crypto code natively and runs correctness checks.
# No ESP32, no toolchain, no hardware required — just a C compiler.
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
CR="$ROOT/components/trezor-crypto/lib/crypto"

if [ ! -f "$CR/bip32.c" ]; then
  echo "trezor-crypto submodule missing. Run: git submodule update --init --depth 1"
  exit 1
fi

CRYPTO_SRCS="
  bignum.c ecdsa.c curves.c secp256k1.c nist256p1.c rfc6979.c hmac.c hmac_drbg.c
  rand_insecure.c bip32.c bip39.c bip39_english.c pbkdf2.c sha2.c sha3.c ripemd160.c
  blake256.c blake2b.c groestl.c hasher.c base58.c base32.c segwit_addr.c address.c
  memzero.c consteq.c fault_handler_noop.c
  ed25519-donna/curve25519-donna-32bit.c ed25519-donna/curve25519-donna-helpers.c
  ed25519-donna/modm-donna-32bit.c ed25519-donna/ed25519-donna-basepoint-table.c
  ed25519-donna/ed25519-donna-32bit-tables.c ed25519-donna/ed25519-donna-impl-base.c
  ed25519-donna/curve25519-donna-scalarmult-base.c ed25519-donna/ed25519.c
  ed25519-donna/ed25519-sha3.c ed25519-donna/ed25519-keccak.c
"

SRCS=()
for s in $CRYPTO_SRCS; do SRCS+=("$CR/$s"); done
SRCS+=("$ROOT/components/chains/chains.c"
       "$ROOT/components/chains/chain_btc.c"
       "$ROOT/components/chains/chain_evm.c"
       "$ROOT/components/chains/chain_trx.c"
       "$ROOT/components/chains/chain_sol.c"
       "test_addresses.c")

echo "Compiling host test..."
cc -O2 -std=gnu11 -w \
   -DUSE_KECCAK=1 -DUSE_ETHEREUM=1 -DUSE_BIP39_CACHE=0 -DUSE_BIP32_CACHE=0 \
   -DUSE_MONERO=0 -DUSE_NEM=0 -DUSE_CARDANO=0 \
   -I"$CR" -I"$CR/ed25519-donna" \
   -I"$ROOT/components/common/include" \
   -I"$ROOT/components/chains/include" \
   "${SRCS[@]}" -o /tmp/dv_host_test

echo "Running..."
echo "----------------------------------------"
/tmp/dv_host_test
