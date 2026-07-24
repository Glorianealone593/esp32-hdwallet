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
        "trezor-crypto submodule missing. Run:\n"
        "  git submodule update --init --recursive")
endif()

# Curated source list (avoids upstream tests, tools, and rand.c).
set(TC_SRCS
    "${LIB}/bignum.c"
    "${LIB}/ecdsa.c"
    "${LIB}/curves.c"
    "${LIB}/secp256k1.c"
    "${LIB}/nist256p1.c"
    "${LIB}/rfc6979.c"
    "${LIB}/hmac.c"
    "${LIB}/hmac_drbg.c"
    "${LIB}/bip32.c"
    "${LIB}/bip39.c"
    "${LIB}/pbkdf2.c"
    "${LIB}/sha2.c"
    "${LIB}/sha3.c"
    "${LIB}/ripemd160.c"
    "${LIB}/blake256.c"
    "${LIB}/blake2b.c"
    "${LIB}/hasher.c"
    "${LIB}/base58.c"
    "${LIB}/base32.c"
    "${LIB}/segwit_addr.c"
    "${LIB}/address.c"
    "${LIB}/memzero.c"
    "${LIB}/ed25519-donna/ed25519.c"
)

idf_component_register(
    SRCS ${TC_SRCS}
    INCLUDE_DIRS "${LIB}" "${LIB}/ed25519-donna"
    REQUIRES ""
)

# trezor-crypto expects these build-time switches.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    USE_BIP39_CACHE=0
    USE_BIP32_CACHE=0
    USE_KECCAK=1
    USE_ETHEREUM=1
    USE_MONERO=0
    USE_NEM=0
    USE_CARDANO=0
    ED25519_CUSTOMRANDOM=1
    ED25519_CUSTOMHASH=0
)

# Silence upstream-only warnings so our -Werror builds stay clean.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-implicit-fallthrough
)
