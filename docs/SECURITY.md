# DibaVault — Security

_Copyright © dibachain. GPL-3.0._

This document describes DibaVault's threat model, what the firmware does and does
not protect against, and the honest limitations of using a commodity ESP32 as a
hardware wallet. **Read it in full before storing real funds.**

> **DibaVault has not been independently audited.** Nothing here should be read as
> a guarantee of security. It documents design intent and known gaps so you can
> make an informed decision.

## Contents

- [Summary](#summary)
- [Threat model](#threat-model)
- [What is protected](#what-is-protected)
- [What is NOT protected](#what-is-not-protected)
- [The secure-core / connectivity split](#the-secure-core--connectivity-split)
- [Entropy / TRNG source](#entropy--trng-source)
- [PIN, seed wrapping, and lockout](#pin-seed-wrapping-and-lockout)
- [Flash encryption & secure boot](#flash-encryption--secure-boot)
- [OTA signature verification](#ota-signature-verification)
- [Air-gap mode](#air-gap-mode)
- [Known limitations of ESP32 as a wallet](#known-limitations-of-esp32-as-a-wallet)
- [Responsible disclosure](#responsible-disclosure)
- [Pre-flight checklist (before real funds)](#pre-flight-checklist-before-real-funds)

## Summary

DibaVault keeps the BIP39 seed and all private keys inside an isolated **secure
core** and requires a **physical confirmation** for every signature. The seed is
stored encrypted at rest and additionally wrapped with a key derived from the
user's PIN. Networking code is treated as untrusted and cannot reach key material.
Production builds add ESP-IDF flash encryption and Secure Boot v2, and all OTA
images are verified against a pinned dibachain signing key.

These are sound engineering practices, but the ESP32 is a general-purpose MCU with
**no dedicated secure element**. A wallet built on it cannot offer the physical
tamper resistance of a purpose-built secure chip. Treat DibaVault as a
defense-in-depth hot/cold wallet for amounts you understand the risk of, not as an
audited, certified device.

## Threat model

DibaVault is designed primarily to resist a **remote / network attacker** and to
raise the cost of a **casual physical attacker**. It explicitly does **not** claim
to resist a well-resourced physical attacker.

| Adversary | In scope? | DibaVault's stance |
|---|---|---|
| Remote attacker on the same WiFi / the internet | **Yes** | Cannot reach keys: networking never links the key code; keys never cross the IPC boundary; signing needs the button. |
| Malicious RPC endpoint / MITM of RPC data | **Yes** | RPC results are untrusted input; the vault re-decodes and re-encodes the signing payload and shows a human summary before signing. |
| Malicious/rogue firmware pushed as an "update" | **Yes** | OTA images must be signed by the pinned dibachain key; unsigned/altered images are rejected. Secure Boot v2 rejects unsigned bootloaders/apps. |
| Casual thief with the powered-off device | **Partly** | Flash encryption + PIN-wrapped seed mean a flash dump yields ciphertext; PIN lockout slows guessing. |
| Attacker who watches you type / shoulder-surfs | **Partly** | PIN entry and seed display require care by the user; the mnemonic is shown only during setup and only to a local origin. |
| Determined physical attacker (decapping, glitching, side-channel, probing) | **No** | The ESP32 has no secure element; assume secrets are eventually extractable. Use air-gap + small amounts. |
| Supply-chain tampering (pre-flashed/backdoored board) | **No** | Cannot be detected in general. Build from source and provision on hardware you trust. |
| Coercion ("wrench attack") | **No** | Out of scope for any wallet firmware. |

## What is protected

- **Private keys and the BIP39 seed** never leave the secure core and are never
  returned across the IPC boundary (`vault_ipc.h`). There is no request that
  yields a key; requesting one returns `DV_ERR_BOUNDARY`.
- **Signing requires physical confirmation** of a decoded, human-readable
  transaction. The connectivity layer cannot smuggle a different payload past the
  on-screen summary: the vault recomputes the canonical sighash itself from
  reviewable fields.
- **The recovery mnemonic is shown only during first-time setup**, and only to a
  **local** origin (on-device console/display). It is never revealed over HTTP.
- **Seed at rest is encrypted and PIN-wrapped** — a flash dump without the PIN
  yields only ciphertext (subject to the ESP32 limitations below).
- **Only signed firmware runs** — OTA images and, in production, the bootloader
  and app are signature-checked.
- **Secrets are never logged.** Error codes (`dv_err_t`) are safe to surface over
  the network and never carry key material.

## What is NOT protected

- **Physical extraction by a skilled attacker.** No secure element ⇒ no strong
  guarantee against decapping, fault injection, or power/EM side-channel analysis.
- **A compromised host you paste secrets into.** If you type your recovery words
  into a malicious computer during import, or photograph them, DibaVault cannot
  help.
- **Your backup.** If you lose your recovery words, the funds are gone. If someone
  else finds them, the funds are theirs. Back up offline and securely.
- **A device provisioned with WiFi left on and a weak/blank AP password.** Reduce
  attack surface: air-gap for significant funds.
- **Bugs.** This firmware is unaudited. There may be exploitable defects in the
  transaction decoders, the crypto bindings, or the network stack.

## The secure-core / connectivity split

The single most important design property is that the two domains share only one
narrow, message-passing boundary:
[`components/secure_core/include/vault_ipc.h`](../components/secure_core/include/vault_ipc.h).

- The **connectivity layer** (WiFi AP/STA, HTTP web UI, RPC clients, OTA client)
  **never `#include`s** `keystore.h` or the signer. A build-time boundary check
  (`tools/check_boundary.sh`, referenced from `keystore.h`) fails the build if it
  does.
- All the untrusted side can do is send `vault_req_t` messages on a queue and read
  `vault_resp_t` replies. The set of request kinds is fixed and small
  (status, list accounts, derive public account, sign tx, sign message, unlock,
  lock, provisioning-only requests). None return private material.
- Every request carries a **`vreq_origin_t`** (`LOCAL_CONSOLE`, `LOCAL_DISPLAY`,
  `REMOTE_HTTP`). The vault applies stricter policy to remote requests — e.g.
  provisioning that would overwrite an existing seed and mnemonic reveal are
  refused from HTTP.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for the message flow and task/core
pinning.

## Entropy / TRNG source

- Seed entropy is generated from the **ESP32 hardware RNG**. The wrapper component
  for trezor-crypto **deliberately does not compile upstream `rand.c`** (whose
  default RNG is a stub); the real `random32` / `random_buffer` are bound to the
  hardware TRNG in the secure core (see
  [`components/trezor-crypto/CMakeLists.txt`](../components/trezor-crypto/CMakeLists.txt)).
- **Operational note:** the ESP32 hardware RNG is specified to produce true random
  numbers only when an entropy source is active — that is, when the **RF subsystem
  (WiFi/BT) is enabled**, or the internal entropy source / bootloader RNG is
  active, and the CPU is not running from a fixed clock without those sources.
  DibaVault seeds new wallets in a state where the hardware entropy source is
  available. If you generate a seed, prefer doing so on first boot as guided by
  the setup flow rather than in a stripped-down custom build.

## PIN, seed wrapping, and lockout

- The seed entropy is stored in the dedicated **`nvs_secure`** partition, which
  uses **NVS encryption** (backed by a flash-encryption key held in eFuse).
- On top of that, the entropy is **wrapped with a key derived from your PIN** via
  **PBKDF2-HMAC-SHA256** with a high iteration count. A flash dump without the PIN
  therefore yields only ciphertext.
- On **unlock**, the 64-byte BIP39 seed is derived and held in a **locked RAM
  buffer for the session only**. `lock()` **zeroizes** it (`memzero`, not a
  compiler-elidable `memset`). Locking also happens on explicit request.
- **PIN lockout:** repeated wrong PINs consume attempts (`pin_attempts_left` in
  the status response); exhausting them returns `DV_ERR_PIN_LOCKOUT`. Choose a PIN
  long enough that lockout is meaningful; a short numeric PIN is only as strong as
  the lockout policy plus the flash-encryption/secure-boot fuses backing it.

> The strength of PIN wrapping against an **offline** attacker depends on flash
> encryption and secure boot being enabled (production builds). Without them, an
> attacker who can read flash and run code can attempt to brute-force the PBKDF2
> wrapping off-device.

## Flash encryption & secure boot

Production release builds enable:

- **Flash encryption** (`CONFIG_SECURE_FLASH_ENC_ENABLED`) — flash contents,
  including the encrypted NVS key, are encrypted with a key in eFuse.
- **Secure Boot v2** (`CONFIG_SECURE_BOOT`, `CONFIG_SECURE_BOOT_V2_ENABLED`) —
  the ROM/bootloader only boots a bootloader and app signed by your secure-boot
  key, preventing an attacker from booting modified firmware to read secrets.
- **NVS encryption** (`CONFIG_NVS_ENCRYPTION`) for the vault partition.

> ⚠️ **These fuses are irreversible.** Burning flash encryption and secure boot is
> a one-way operation that can brick a board if done wrong and permanently
> disables plain re-flashing. They are **disabled in the default dev config** and
> must be turned on deliberately for production. Follow
> [`BUILD.md`](BUILD.md#production-flash-encryption--secure-boot) exactly, and
> practise on a throwaway board first.

## OTA signature verification

- Firmware is the **only** thing the WiFi path is allowed to change on the device.
- Every OTA image is verified against a **pinned dibachain public key** (ECDSA over
  the image hash) **before** it is written to the inactive OTA slot and **before**
  it is booted. A rejected image is discarded (see
  [`components/connectivity/include/ota.h`](../components/connectivity/include/ota.h)).
- OTA cannot leak keys: the OTA path does not have access to the running key
  domain, and any replacement image must itself be signed by dibachain.
- With Secure Boot v2 enabled, the app signature is additionally enforced by the
  bootloader at every boot.

You can point OTA at the dibachain release channel or a user mirror URL, but the
signature check against the pinned key is not optional.

## Air-gap mode

Setting WiFi to **off** puts the device in an air-gapped state
(`wifi_mgr_is_airgapped()` returns true). In this mode:

- No radio is active; there is no network path to the device at all.
- Transactions are entered over the **local serial console**; the vault re-decodes
  and re-encodes the canonical signing payload and shows a summary for physical
  confirmation.
- The signed transaction is emitted for you to broadcast from a separate, online
  machine (optionally via QR).

This is the recommended posture for meaningful funds. Caveats: air-gap protects
against **network** attackers, not against a malicious host used to prepare/parse
transactions, nor against physical attacks on the device.

## Known limitations of ESP32 as a wallet

Be honest with yourself about these before trusting real value:

- **No dedicated secure element.** Keys are protected by flash encryption, secure
  boot and PIN wrapping — not by a certified tamper-resistant chip. This is the
  single biggest difference from commercial hardware wallets.
- **Side-channel exposure.** General-purpose MCUs are susceptible to power and
  electromagnetic side-channel analysis. The bundled crypto uses constant-time-ish
  routines (e.g. RFC6979 deterministic nonces), but no side-channel resistance is
  guaranteed on this platform.
- **Fault injection / glitching.** Voltage/clock/EM glitching can, in principle,
  bypass software checks on commodity silicon. Some ESP32 revisions have had
  documented fault-injection and eFuse/secure-boot bypass findings; keep firmware
  and ESP-IDF up to date and prefer newer silicon revisions.
- **Debug interfaces.** JTAG/USB-Serial-JTAG must be disabled via eFuse in
  production, or they become an extraction path. This is part of the production
  fuse burn.
- **Supply chain.** A pre-flashed or hardware-tampered board cannot be trusted.
  Buy from reputable sources, build from source, and provision yourself.
- **Unaudited firmware and dependencies.** Neither DibaVault nor its exact
  integration of trezor-crypto has been independently audited.

## Responsible disclosure

If you find a security issue, please report it privately rather than opening a
public issue.

- **Contact:** `security@dibachain` &nbsp; _(**placeholder** — replace with the
  real dibachain security contact / PGP key before public release)._
- Please include reproduction steps and affected versions, and give the
  maintainers reasonable time to remediate before any public disclosure.

Do not include private keys, seeds, or funds in reports.

## Pre-flight checklist (before real funds)

Work through **all** of these first. If you cannot check a box, do not store real
value yet.

- [ ] I understand DibaVault is **unaudited, experimental** firmware used **at my
      own risk**.
- [ ] I **built from source** (or independently verified the release) rather than
      trusting an unknown binary.
- [ ] I tested the full flow (create, receive, **sign**, broadcast) on **testnets**
      first.
- [ ] I provisioned a **display + button** build so I review and confirm
      transactions **on the device**, not headless.
- [ ] I set a **strong PIN** and understand the **lockout** behavior.
- [ ] I **wrote down my recovery words offline** during setup and verified them,
      and I store them securely and privately.
- [ ] For meaningful amounts, I run **air-gapped** (WiFi off) or at minimum
      AP-only, watch-only, with a strong AP password.
- [ ] For production, I enabled **flash encryption + Secure Boot v2** and disabled
      debug interfaces, following [`BUILD.md`](BUILD.md), and I practised on a
      throwaway board first.
- [ ] I keep firmware updated via **signed OTA** from dibachain.
- [ ] I am only committing an amount I can afford to lose while this firmware is
      unaudited.
