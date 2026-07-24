# chains — pure address/sighash/serialization logic. No keys, no network.
idf_component_register(
    SRCS
        "chains.c"
        "chain_btc.c"
        "chain_evm.c"
        "chain_trx.c"
        "chain_sol.c"
    INCLUDE_DIRS "include"
    REQUIRES common trezor-crypto
)
