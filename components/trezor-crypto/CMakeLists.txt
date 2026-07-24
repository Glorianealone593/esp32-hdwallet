# Wrapper component that builds trezor-crypto for ESP-IDF.
# Upstream is the actively-maintained trezor-firmware monorepo (the standalone
# trezor-crypto repo was archived in 2019 with known security bugs). The crypto
# library lives under crypto/ in that submodule:
#   git submodule update --init --depth 1
#
# IMPORTANT SECURITY NOTE:
# We deliberately DO NOT compile upstream rand.c. Its default RNG is a stub.
# The real hardware TRNG binding (random32 / random_buffer) is provided by the
# secure_core component using the ESP32 hardware RNG. See secure_core/trng.c.

set(LIB "${CMAKE_CURRENT_LIST_DIR}/lib/crypto")

if(NOT EXISTS "${LIB}/bip32.c")
    message(FATAL_ERROR
        "trezor-crypto submodule missing. Run (shallow, non-recursive):\n"
        "  git submodule update --init --depth 1")
endif()

# Curated source list. Covers all transitive dependencies of the primitives we
# use (BIP32/39/44, ECDSA secp256k1/nist256p1, ed25519, keccak/SHA/RIPEMD,
# base58/base32/segwit, hasher). Deliberately EXCLUDES:
#   * rand.c            — we provide random32/random_buffer (secure_core/trng.c)
#   * zkp_*.c           — need the external secp256k1-zkp submodule
#   * monero/cardano/nem/noise/slip39/aes/chacha — unused coin/cipher modules
# hasher.c hard-references groestl512_* and blake*, so those are included.
# ed25519.c is NOT self-contained; its donna companion units are required.
set(TC_SRCS
    "${LIB}/bignum.c"
    "${LIB}/ecdsa.c"
    "${LIB}/curves.c"
    "${LIB}/secp256k1.c"
    "${LIB}/nist256p1.c"
    "${LIB}/rfc6979.c"
    "${LIB}/hmac.c"
    "${LIB}/hmac_drbg.c"
    "${LIB}/rand_insecure.c"
    "${LIB}/bip32.c"
    "${LIB}/bip39.c"
    "${LIB}/bip39_english.c"
    "${LIB}/pbkdf2.c"
    "${LIB}/sha2.c"
    "${LIB}/sha3.c"
    "${LIB}/ripemd160.c"
    "${LIB}/blake256.c"
    "${LIB}/blake2b.c"
    "${LIB}/groestl.c"
    "${LIB}/hasher.c"
    "${LIB}/base58.c"
    "${LIB}/base32.c"
    "${LIB}/segwit_addr.c"
    "${LIB}/address.c"
    "${LIB}/memzero.c"
    # ed25519-donna (ed25519.c depends on all of these)
    "${LIB}/ed25519-donna/curve25519-donna-32bit.c"
    "${LIB}/ed25519-donna/curve25519-donna-helpers.c"
    "${LIB}/ed25519-donna/modm-donna-32bit.c"
    "${LIB}/ed25519-donna/ed25519-donna-basepoint-table.c"
    "${LIB}/ed25519-donna/ed25519-donna-32bit-tables.c"
    "${LIB}/ed25519-donna/ed25519-donna-impl-base.c"
    "${LIB}/ed25519-donna/curve25519-donna-scalarmult-base.c"
    "${LIB}/ed25519-donna/ed25519.c"
    "${LIB}/ed25519-donna/ed25519-sha3.c"
    "${LIB}/ed25519-donna/ed25519-keccak.c"
)

idf_component_register(
    SRCS ${TC_SRCS}
    INCLUDE_DIRS "${LIB}" "${LIB}/ed25519-donna"
    REQUIRES ""
)

# trezor-crypto build-time switches. ed25519.c pulls randomness from rand.h
# (our secure_core/trng.c provides random32/random_buffer), so no custom-random
# hook is defined here.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    USE_BIP39_CACHE=0
    USE_BIP32_CACHE=0
    USE_KECCAK=1
    USE_ETHEREUM=1
    USE_MONERO=0
    USE_NEM=0
    USE_CARDANO=0
)

# Silence upstream-only warnings so our -Werror builds stay clean.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-implicit-fallthrough
)
