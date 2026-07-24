# secure_core — holds keys and signs. Depends on trezor-crypto and chains.
# NOTE: connectivity/console link against vault_ipc.h only; the private headers
# keystore.h/signer.h are intentionally NOT re-exported for their use. The
# boundary is checked by tools/check_boundary.sh in CI.
idf_component_register(
    SRCS
        "vault_ipc.c"
        "keystore.c"
        "signer.c"
        "confirm.c"
        "trng.c"
    INCLUDE_DIRS "include"
    REQUIRES
        common
        trezor-crypto
        chains
        hal
    PRIV_REQUIRES
        nvs_flash
        mbedtls
        esp_hw_support
)
