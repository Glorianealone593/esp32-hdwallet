// DibaVault — hardware TRNG binding for trezor-crypto.
// Copyright (c) dibachain. GPL-3.0.
//
// trezor-crypto declares random32()/random_buffer() but we deliberately do not
// compile its rand.c (which ships a deterministic stub). Here we bind those
// symbols to the ESP32 hardware RNG.
//
// SECURITY: esp_random()/esp_fill_random() are only cryptographically strong
// when an entropy source is active — i.e. the RF subsystem (WiFi/BT) is enabled
// OR the chip's SAR-ADC bootstrap entropy has been seeded. During seed
// GENERATION we additionally mix in bootloader_random + a hashing whitening
// step (see keystore_provision_new) so we never rely on a single source.
#include <string.h>
#include "esp_random.h"
#include "esp_system.h"

uint32_t random32(void) {
    return esp_random();
}

void random_buffer(uint8_t *buf, size_t len) {
    esp_fill_random(buf, len);
}

// ed25519-donna (built with ED25519_CUSTOMRANDOM) asks for randomness here.
void ed25519_randombytes_unsafe(void *out, size_t count) {
    esp_fill_random(out, count);
}
