# DibaVault — Architecture

_Copyright © dibachain. GPL-3.0._

This document describes how DibaVault is put together: the component map, the vault
IPC boundary and message flow, task/core pinning, the flash layout, and how to add
a new chain.

## Contents

- [High-level design](#high-level-design)
- [Component map](#component-map)
- [The vault IPC boundary](#the-vault-ipc-boundary)
- [Sign-transaction message flow](#sign-transaction-message-flow)
- [Tasks and core pinning](#tasks-and-core-pinning)
- [Storage partitions](#storage-partitions)
- [Runtime hardware configuration (HAL)](#runtime-hardware-configuration-hal)
- [Adding a new chain](#adding-a-new-chain)

## High-level design

DibaVault is one ESP-IDF application split into two trust domains:

- a **secure core** that owns the seed and private keys, derives accounts, runs the
  physical-confirmation gate, and signs; and
- an **untrusted connectivity layer** (WiFi, HTTP web UI, RPC, OTA) plus a **local
  console** that build unsigned transactions and display information.

They communicate **only** through the vault IPC boundary
([`components/secure_core/include/vault_ipc.h`](../components/secure_core/include/vault_ipc.h)).
The connectivity code never links against the key code; a boundary check enforces
this at build time.

A single firmware image is portable across ESP32, ESP32-S3 and ESP32-C3; the exact
board wiring is chosen at runtime during first-boot provisioning and stored in NVS.

## Component map

All first-party code lives under [`components/`](../components). Each is a standard
ESP-IDF component.

| Component | Trust domain | Role | Key headers |
|---|---|---|---|
| [`secure_core`](../components/secure_core) | **Trusted** | The vault. Holds the seed and keys; derives accounts (BIP32/39); signs (secp256k1 / ed25519); binds the ESP32 hardware TRNG; runs the confirmation gate. Exposes **only** the IPC. | `vault_ipc.h`, `keystore.h`, `confirm.h`, `dv_types.h` |
| [`chains`](../components/chains) | Shared (pure) | Chain abstraction: address encoding, canonical sighash, signed-tx serialization, human-readable review lines. **Pure** functions — no keys, no network, no storage. One `dv_chain_ops_t` vtable per chain family. | `chains.h` |
| [`trezor-crypto`](../components/trezor-crypto) | Trusted (used by core) | Vendored cryptography (ECDSA/secp256k1, ed25519, BIP32/39, hashes, base58, segwit). Git submodule under `lib/`. Upstream stub RNG is **not** compiled. | (submodule) |
| [`hal`](../components/hal) | Support | Hardware abstraction: display (SSD1306/SH1106/ST7789), buttons, and the persisted runtime config describing the wiring. | `hal_config.h`, `display.h`, `button.h` |
| [`connectivity`](../components/connectivity) | **Untrusted** | WiFi AP/STA manager, HTTP web server + JSON API, RPC clients (balances / tx building / broadcast), signed OTA, and the user-managed network/RPC registry. Links only against `vault_ipc.h`. | `wifi_mgr.h`, `web_server.h`, `rpc_client.h`, `net_config.h`, `ota.h` |
| [`console`](../components/console) | Local (privileged) | Serial/USB REPL. A **local** origin, so it may do things HTTP may not: reveal the mnemonic at setup, factory reset, and drive fully-offline signing. | `console_ui.h` |

Boundary rules:

- `connectivity` and the HTTP/RPC/OTA code **must not** include `keystore.h` or the
  signer. The build fails (`tools/check_boundary.sh`, referenced from
  `keystore.h`) if they do.
- `chains` is pure and may be called from **either** side: by the vault to
  recompute the sighash and serialize the final signed tx, and by the connectivity
  layer to format addresses for display.

## The vault IPC boundary

The connectivity and console layers interact with the vault through two functions:

```c
dv_err_t vault_ipc_start(void);                            // once, at boot
dv_err_t vault_ipc_request(const vault_req_t *req,
                           vault_resp_t *resp,
                           uint32_t timeout_ms);           // blocking, thread-safe
```

- A caller fills a `vault_req_t` (a `kind`, an `origin`, and a payload union) and
  blocks on `vault_ipc_request`. The request is copied onto the vault's queue; the
  reply is copied back on a per-request reply queue. **No writable heap is shared**
  beyond the copied message payloads.
- Request kinds (`vreq_kind_t`): `GET_STATUS`, `LIST_ACCOUNTS`, `DERIVE_ACCOUNT`,
  `SIGN_TX`, `SIGN_MESSAGE`, `UNLOCK`, `LOCK`, and setup-only
  `PROVISION_NEW` / `PROVISION_IMPORT` / `REVEAL_MNEMONIC`.
- Origins (`vreq_origin_t`): `LOCAL_CONSOLE`, `LOCAL_DISPLAY`, `REMOTE_HTTP`. The
  vault serializes requests internally, so only one signing/confirmation runs at a
  time, and applies stricter policy to remote origins.

**Invariants enforced by construction:**

1. No request returns a private key, seed, or mnemonic to a remote caller.
   `REVEAL_MNEMONIC` is delivered only to a **local** origin; remote gets
   `DV_ERR_BOUNDARY`.
2. No signature is produced without a decoded, human-reviewable summary **and**
   physical confirmation.
3. The untrusted side supplies a **decoded, reviewable** transaction
   (`dv_tx_review_t`); the vault re-validates it and **recomputes the canonical
   signing payload itself** — it never signs a raw blob handed in from outside.

## Sign-transaction message flow

The web UI (or console) builds an unsigned transaction, the vault re-derives
everything, the human approves on the button/OLED, and a broadcast-ready signed
transaction comes back. Keys never appear in any message.

```
 Web UI /        Connectivity     chains (pure)        Vault task           Human
 console         layer            addr/sighash/ser     (secure core)        (button/OLED)
   │                │                   │                   │                   │
   │ enter tx       │                   │                   │                   │
   │ details        │                   │                   │                   │
   │───────────────▶│                   │                   │                   │
   │                │ rpc_prepare_tx()  │                   │                   │
   │                │ (nonce/gas/UTXO/  │                   │                   │
   │                │  blockhash)       │                   │                   │
   │                │──── RPC (untrusted results) ──────────────────────────▶  (network)
   │                │                   │                   │                   │
   │                │ build dv_tx_review_t (decoded, human-readable)           │
   │                │ vault_ipc_request(VREQ_SIGN_TX, origin=REMOTE_HTTP)       │
   │                │──────────────────────────────────────▶│                   │
   │                │                   │  re-validate +    │                   │
   │                │                   │  recompute sighash│                   │
   │                │                   │◀──── sighash() ───│                   │
   │                │                   │───────────────────▶                   │
   │                │                   │                   │ confirm_transaction()
   │                │                   │                   │──────────────────▶│
   │                │                   │                   │   review on OLED  │
   │                │                   │                   │◀── press OK ──────│
   │                │                   │                   │ sign (secp256k1/  │
   │                │                   │                   │       ed25519)    │
   │                │                   │  serialize_signed()                   │
   │                │                   │◀──────────────────│                   │
   │                │                   │───────────────────▶ signed tx bytes   │
   │                │◀─────── vault_resp_t: sig + signed_tx ─│                   │
   │                │ rpc_broadcast()   │                   │                   │
   │                │──── broadcast signed tx ──────────────────────────────▶  (network)
   │◀── txid ───────│                   │                   │                   │
```

If the user rejects or the timeout elapses, the vault returns `DV_ERR_USER_REJECTED`
or `DV_ERR_TIMEOUT` and nothing is signed. A locked vault returns `DV_ERR_LOCKED`
until a correct PIN is submitted via `VREQ_UNLOCK`.

## Tasks and core pinning

- The **vault** runs on a **dedicated FreeRTOS task pinned to its own core** on
  dual-core parts (ESP32, ESP32-S3). The connectivity/WiFi stack runs on the other
  core. This isolates signing/confirmation timing from the network stack and keeps
  the key-handling task off the core doing untrusted work.
- On the **single-core ESP32-C3**, both domains time-share one core; the trust
  boundary is still enforced by the IPC and the no-link rule, but there is no
  physical core separation.
- The vault **serializes** requests: only one signing/confirmation is in flight at
  a time. Callers block in `vault_ipc_request` with a timeout.
- The **confirmation gate** (`confirm.h`) blocks the vault task waiting for a
  debounced button event (`button_wait`) up to `confirm_timeout_ms` from the HAL
  config.

## Storage partitions

The flash layout is defined by [`partitions.csv`](../partitions.csv) (8 MB, dual
OTA) and [`partitions_4mb.csv`](../partitions_4mb.csv) (4 MB modules such as
ESP32-C3). The 8 MB table:

| Partition | Type / SubType | Purpose | Sensitive? |
|---|---|---|---|
| `nvs` | data / nvs | General settings, incl. the user-managed **network/RPC registry** and HAL config. | No (non-secret config) |
| `otadata` | data / ota | Tracks which OTA slot is active. | No |
| `phy_init` | data / phy | RF PHY calibration data. | No |
| `nvs_secure` | data / nvs (**encrypted**) | The **seed / key material**, stored encrypted and PIN-wrapped. | **Yes** |
| `ota_0` | app / ota_0 | Application slot A (signed). | — |
| `ota_1` | app / ota_1 | Application slot B (signed) — enables safe OTA. | — |
| `storage` | data / spiffs | Embedded **web UI assets** (public, non-sensitive). | No |

Notes:

- The seed lives in its **own encrypted NVS partition** (`nvs_secure`), separate
  from the general `nvs` partition, so sensitive and non-sensitive data are never
  mixed.
- Dual `ota_0` / `ota_1` slots let a verified update be written to the inactive
  slot and activated only after signature checks (see `ota.h`), with rollback if it
  fails to validate.
- The 4 MB layout collapses the same structure into smaller slots for constrained
  modules.

## Runtime hardware configuration (HAL)

Because one image runs on any wiring and any of the three chips, the board layout is
**data, not compile-time config**. `hal_config.h` defines `dv_hal_config_t`
(display type, I2C SDA/SCL/addr, panel size, optional SPI pins, confirm mode and
button GPIOs, optional LED, confirm timeout). At first boot:

1. `hal_config_default_for_target()` prefills a sane template for the detected chip.
2. The setup UI lets the operator edit it.
3. `hal_config_validate()` checks each GPIO against the current chip (pin exists,
   is **not a strapping/flash pin**, no duplicates) and returns a reason string on
   failure.
4. `hal_config_save()` persists it to NVS; thereafter it is fixed for the device.

See [`HARDWARE.md`](HARDWARE.md) for per-chip pin guidance.

## Adding a new chain

A chain family is a single file that registers a `dv_chain_ops_t` vtable
(see [`components/chains/include/chains.h`](../components/chains/include/chains.h)).
You do not touch the vault or the connectivity layer.

1. **Add the family** to `dv_chain_t` in `dv_types.h` (before `DV_CHAIN__COUNT`),
   with its BIP44 coin type and curve, plus any needed address kind in
   `dv_addr_kind_t`.
2. **Create `components/chains/<name>.c`** implementing a `dv_chain_ops_t`:
   - `format_address(pubkey, len, evm_chain_id, out, out_sz)` — encode the public
     key into the chain's address string.
   - `sighash(tx, signer_pubkey, len, out, out_len, is_prehash)` — produce the
     canonical 32-byte signing digest, or (for ed25519 chains) the full message to
     sign with `*is_prehash = false`. This is what the vault recomputes; it must be
     deterministic and depend only on the reviewable fields.
   - `serialize_signed(tx, pubkey, sig, sig_len, recid, out, out_len, out_cap)` —
     assemble a broadcast-ready signed transaction.
   - `describe(tx, out)` — fill `dv_tx_review_t` with the human-readable lines shown
     on the confirmation screen. **Keep this faithful**: it is the user's only
     defense against a malicious payload.
3. **Register** the ops so `chains_get(<chain>)` returns them, and implement
   `chains_default_path()` for the family's default derivation path.
4. Use the pure crypto in `trezor-crypto` for hashing/encoding; **do not** add key
   access to the `chains` component — it must stay pure (no keys, no network, no
   storage).
5. Optionally add default network(s)/RPC entries via the registry
   (`net_config.h`) so the new chain appears in the UI.

Because `chains` is pure and the vault drives it, a correct vtable is automatically
covered by the existing confirmation and boundary guarantees.
