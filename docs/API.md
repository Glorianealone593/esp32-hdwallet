# DibaVault — REST API Reference

_Copyright © dibachain. GPL-3.0._

This is the complete reference for the JSON REST API served by DibaVault's on-device
HTTP server ([`components/connectivity/web_server.c`](../components/connectivity/web_server.c)).
The same API powers the embedded web UI and is intended for external clients —
including a planned companion mobile/desktop app — to integrate against the device.

> **Scope note.** Every endpoint below is exactly what the firmware registers today.
> The API is deliberately small: status, provisioning, unlock/lock, the network and
> token registries, account/balance reads, and the build → sign → broadcast
> transaction flow.

## Contents

- [Base URL & transport](#base-url--transport)
- [Security model for API consumers](#security-model-for-api-consumers)
- [Authentication & threat notes](#authentication--threat-notes)
- [Conventions](#conventions)
- [Error format & codes](#error-format--codes)
- [Endpoints](#endpoints)
  - [Status & hardware](#status--hardware)
  - [Provisioning](#provisioning)
  - [Unlock / lock](#unlock--lock)
  - [Networks](#networks)
  - [Tokens](#tokens)
  - [Accounts & balances](#accounts--balances)
  - [Transactions](#transactions)
  - [WiFi](#wifi)
  - [OTA](#ota)
- [Example flows (curl)](#example-flows-curl)
- [Future app integration](#future-app-integration)

## Base URL & transport

- **Transport:** plain HTTP (no TLS) served by the device.
- **AP mode (default):** the device is its own WiFi access point; the API is at
  `http://192.168.4.1`. This is the address the first-boot web UI uses.
- **STA / joined-router mode:** when the device joins your router, it is reachable at
  the IP the router assigns it, on the same paths.
- **Content type:** request bodies are JSON; responses are JSON
  (`application/json`). Request bodies are capped at **4096 bytes**.
- All paths below are relative to the base URL, e.g. `GET /api/status` →
  `http://192.168.4.1/api/status`.

## Security model for API consumers

The API is served by the **untrusted connectivity layer**. It never has access to
private keys; it can only ask the secure core (the "vault") to act, through a single
narrow IPC boundary. Every request the HTTP layer makes to the vault is tagged with
the **remote** origin, so the vault applies its strictest policy. Consequences a
client must design around:

- **The mnemonic is never returned over HTTP after initial setup.** A fresh seed's
  words are returned exactly once, by `POST /api/provision/new`, during first-boot
  provisioning. There is no endpoint that reveals an existing wallet's mnemonic —
  that is only possible from the local (on-device / serial) origin.
- **Signing requires physical confirmation.** `/api/tx/sign`, `/api/tx/broadcast`
  and `/api/offline/sign` block until the user reviews the decoded transaction on
  the device and presses OK. The HTTP call can wait up to ~120 seconds; a rejection
  or timeout returns an error (`user_rejected` / `timeout`).
- **The device recomputes what it signs.** A client supplies transaction intent
  (recipient, amount, token, etc.); the vault re-derives the canonical signing
  payload itself and shows a human-readable summary. A client cannot smuggle opaque
  bytes past the on-screen review.
- **No secrets in errors.** Error codes are safe to surface; they never carry key
  material.

## Authentication & threat notes

- **There is no bearer-token / password authentication on the API itself.** Access
  control is the WiFi layer: in AP mode the WPA2 password on the hotspot gates who
  can reach the device; in STA mode, anyone who can route to the device's IP on your
  LAN can call the API.
- **Treat the device as the source of truth.** A client should never assume it holds
  authority over funds. All sensitive operations (signing, and any mnemonic reveal)
  require **on-device confirmation** or a **local origin** and cannot be forced from
  HTTP. Design the app UI around "propose on the app, confirm on the device".
- **Untrusted network posture.** Because the transport is plain HTTP on a local
  network, assume a co-located attacker could observe or tamper with API traffic.
  DibaVault's guarantees do not rely on the transport being confidential: keys never
  traverse it, and nothing is signed without the physical button. Still, prefer a
  strong AP password, prefer AP-only over joining a shared LAN, and use air-gapped
  offline signing for significant funds (see [`SECURITY.md`](SECURITY.md)).
- **One operation at a time.** The vault serializes signing/confirmation; concurrent
  sign requests will queue behind the one awaiting confirmation.

## Conventions

- **Amounts** in transaction requests are decimal strings in the smallest base unit
  of the asset (wei / satoshi / sun / lamports), e.g. `"amount":"1000000000000000000"`
  for 1 ETH. The device converts base units ↔ display units for the review screen.
- **`net_idx`** is the integer index of a network in the network registry (from
  `GET /api/networks`). **`token_idx`** is the index of a token in the token registry
  (from `GET /api/tokens`).
- **Enums are returned as integers** in some responses; their meanings:
  - `family` (chain family): `0` = Bitcoin, `1` = EVM, `2` = Tron, `3` = Solana.
  - `display`: `0` = none, `1` = SSD1306, `2` = SH1106, `3` = ST7789.
  - `confirm`: `0` = none, `1` = 1-button, `2` = 2-button, `3` = 3-button.
  - `mode` (WiFi): `0` = off (air-gapped), `1` = AP, `2` = STA, `3` = AP+STA.
- When **writing** a network or token, `family` may be given either as the integer
  above **or** as a string: `"bitcoin"`, `"evm"`, `"tron"`, `"solana"` (anything
  unrecognized defaults to EVM).

## Error format & codes

On failure the server responds with HTTP `400 Bad Request` and a JSON body:

```json
{ "error": "locked", "message": "human-readable detail" }
```

`error` is a stable machine code; `message` is a human-readable hint (may equal the
code). Vault-originated codes map from `dv_err_t` via `errname()`:

| `error` | Meaning |
|---|---|
| `ok` | Success (not normally seen in an error body). |
| `locked` | Vault is locked; call `POST /api/unlock` first. |
| `bad_pin` | Wrong PIN supplied to unlock. |
| `pin_lockout` | Too many wrong PINs; unlock is temporarily blocked. |
| `user_rejected` | The on-device physical confirmation was declined. |
| `timeout` | The confirmation (or vault request) timed out. |
| `not_provisioned` | No seed yet; run provisioning first. |
| `already_provisioned` | A seed already exists; refusing to overwrite (remote origin cannot wipe it). |
| `boundary_denied` | A request tried to cross the key boundary (e.g. reveal mnemonic over HTTP). |
| `unsupported_chain` | The chain family is not supported for this operation. |
| `error` | Generic/unclassified vault error. |

HTTP-layer validation codes (returned the same way):

| `error` | Meaning |
|---|---|
| `bad_body` | Missing/oversized request body (empty or > 4096 bytes). |
| `bad_json` | Body is not valid JSON. |
| `bad_net` | `net_idx` does not refer to a configured network. |
| `bad_token` | `token_idx` does not refer to a configured token. |
| `bad_tx` | Transaction intent is malformed (e.g. missing `to`). |
| `invalid_hal` | Proposed hardware config is invalid (`message` carries the reason). |
| `add_failed` | Could not add the network/token (registry full or invalid). |
| `del_failed` | Could not delete the network/token at that index. |
| `no_contract` | Token create was missing a `contract`. |
| `describe_failed` | The chain could not describe the transaction for review. |
| `rpc_failed` | An RPC query (balance / broadcast) failed. |
| `no_url` | OTA apply was called without an image `url`. |
| `ota_failed` | OTA image download or signature verification failed. |

> Note: a successful call returns HTTP `200`. Some endpoints (`/api/unlock`) return
> `200` with an `ok:false` field plus an `error` code rather than a `400`, so that a
> client can read `attempts_left` on a wrong PIN — see the endpoint for details.

## Endpoints

### Status & hardware

#### `GET /api/status`

Device status. No auth, no body.

**Response:**

```json
{
  "provisioned": true,
  "locked": false,
  "fw_version": "1.2.3",
  "attempts_left": 5,
  "airgap": false,
  "mode": 1,
  "sta_connected": false,
  "hardware": { "display": 1, "confirm": 2 }
}
```

| Field | Type | Meaning |
|---|---|---|
| `provisioned` | bool | A seed exists on the device. |
| `locked` | bool | Vault is locked (needs `unlock`). |
| `fw_version` | string | Firmware version. |
| `attempts_left` | number | Remaining PIN attempts before lockout. |
| `airgap` | bool | Radio fully off (air-gapped). |
| `mode` | number | WiFi mode (see enums). |
| `sta_connected` | bool | Joined-router link is up. |
| `hardware.display` | number | Configured display type. |
| `hardware.confirm` | number | Configured confirm mode. |

#### `GET /api/hal`

Current hardware (HAL) configuration.

**Response:**

```json
{
  "display": 1, "i2c_sda": 8, "i2c_scl": 9, "i2c_addr": 60,
  "confirm": 2, "btn_ok": 4, "btn_cancel": 5, "btn_up": -1, "btn_down": -1,
  "active_low": true
}
```

Pins set to `-1` are unused. `i2c_addr` is decimal (e.g. `60` = `0x3C`).

#### `POST /api/hal`

Update the hardware configuration (used by first-boot setup). Body may contain any
subset of the fields above; omitted fields keep their current value.

**Request:**

```json
{ "display": 1, "i2c_sda": 8, "i2c_scl": 9, "i2c_addr": 60,
  "confirm": 2, "btn_ok": 4, "btn_cancel": 5, "active_low": true }
```

**Response (success):** `{ "ok": true }`

**Response (invalid):** `400` with `{ "error": "invalid_hal", "message": "<reason>" }`
where the message explains which pin/field was rejected (e.g. a strapping/flash pin,
a duplicate, or an out-of-range GPIO).

### Provisioning

Provisioning is only meaningful on an unprovisioned device (or from the local
origin). Over HTTP it cannot overwrite an existing seed — that returns
`already_provisioned`.

#### `POST /api/provision/new`

Generate a fresh seed and return its recovery words **once** (first-boot only).

**Request:**

```json
{ "word_count": 24, "pin": "123456", "passphrase": "" }
```

| Field | Type | Notes |
|---|---|---|
| `word_count` | number | `12`, `18` or `24` (defaults to `12`). |
| `pin` | string | PIN to set (used to wrap the seed). |
| `passphrase` | string | Optional BIP39 passphrase ("25th word"). |

**Response:**

```json
{ "mnemonic": "word1 word2 … word24" }
```

> This is the **only** time the words are returned over HTTP, and only over the
> AP-only setup UI. Write them down offline. There is no endpoint to retrieve them
> again over the network.

#### `POST /api/provision/import`

Import an existing mnemonic.

**Request:**

```json
{ "mnemonic": "word1 word2 … word12", "pin": "123456", "passphrase": "" }
```

**Response:** `{ "ok": true }` (or an error such as `already_provisioned`).

#### `POST /api/provision/confirm`

Acknowledge that the user has written down the recovery words. The seed is already
sealed by `/api/provision/new`; this endpoint persists nothing server-side and is a
UI checkpoint. No body required.

**Response:** `{ "ok": true }`

### Unlock / lock

#### `POST /api/unlock`

Submit the PIN to unlock the vault for the session.

**Request:**

```json
{ "pin": "123456" }
```

**Response (always HTTP 200):**

```json
{ "ok": true, "attempts_left": 5 }
```

On a wrong PIN, `ok` is `false`, `attempts_left` decreases, and an `error` field is
included (e.g. `bad_pin`, `pin_lockout`):

```json
{ "ok": false, "attempts_left": 4, "error": "bad_pin" }
```

#### `POST /api/lock`

Wipe the session keys from RAM (re-lock). No body.

**Response:** `{ "ok": true }`

### Networks

The network/RPC registry (non-sensitive config). Up to `DV_NET_MAX` (32) entries.

#### `GET /api/networks`

List configured networks.

**Response:**

```json
{
  "networks": [
    {
      "idx": 0, "family": 1, "evm_chain_id": 1,
      "name": "Ethereum", "rpc_url": "https://rpc.example",
      "symbol": "ETH", "decimals": 18, "explorer": "https://etherscan.io"
    }
  ]
}
```

`idx` is the `net_idx` used by other endpoints.

#### `POST /api/networks`

Add a network.

**Request:**

```json
{
  "family": "evm", "evm_chain_id": 137,
  "name": "Polygon", "rpc_url": "https://polygon-rpc.example",
  "symbol": "MATIC", "decimals": 18, "explorer": "https://polygonscan.com"
}
```

`family` accepts the integer or string form (see [Conventions](#conventions)).

**Response:** `{ "ok": true }` (or `add_failed` if the registry is full/invalid).

#### `DELETE /api/networks/{idx}`

Remove the network at the given registry index.

**Response:** `{ "ok": true }` (or `del_failed`).

### Tokens

The token registry (ERC-20 / TRC-20 / SPL), non-sensitive config. Up to
`DV_TOKEN_MAX` (64) entries. A token is bound to a chain `family` and, for EVM, an
`evm_chain_id`.

#### `GET /api/tokens`

List configured tokens. Optional query `?net_idx=<i>` filters to tokens matching that
network's `(family, evm_chain_id)`.

**Response:**

```json
{
  "tokens": [
    {
      "idx": 0, "family": 1, "evm_chain_id": 1,
      "symbol": "USDT", "name": "Tether USD",
      "contract": "0xdAC17F958D2ee523a2206206994597C13D831ec7",
      "decimals": 6
    }
  ]
}
```

`idx` is the `token_idx` used by `/api/token/balance`. `contract` is an ERC-20 /
TRC-20 address, or an SPL mint for Solana.

#### `POST /api/tokens`

Register a token. You may pass `net_idx` to inherit `family` / `evm_chain_id` from a
network, and/or set them explicitly.

**Request:**

```json
{
  "net_idx": 0,
  "symbol": "USDC", "name": "USD Coin",
  "contract": "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",
  "decimals": 6
}
```

Or fully explicit:

```json
{
  "family": "evm", "evm_chain_id": 1,
  "symbol": "USDC", "name": "USD Coin",
  "contract": "0xA0b8…eB48", "decimals": 6
}
```

`contract` is required (`no_contract` otherwise).

**Response:** `{ "ok": true }` (or `add_failed`).

#### `DELETE /api/tokens/{idx}`

Remove the token at the given registry index.

**Response:** `{ "ok": true }` (or `del_failed`).

#### `GET /api/token/balance`

Read a token balance for an address via the network's RPC endpoint.

**Query params:**

| Param | Required | Meaning |
|---|---|---|
| `net_idx` | yes | Network to query through (its RPC + family). |
| `token_idx` | yes | Token in the registry (provides contract + decimals). |
| `address` | yes | Owner address to query. |

Example: `GET /api/token/balance?net_idx=0&token_idx=0&address=0xabc…`

**Response:**

```json
{ "balance": "12500000", "decimals": 6, "symbol": "USDT" }
```

`balance` is a decimal string in base units; divide by `10^decimals` for display.
Returns `bad_net` / `bad_token` for bad indices, or `rpc_failed` on a query error.

### Accounts & balances

#### `GET /api/accounts`

Derive and return the **public** account (address) for a network. Requires the vault
to be unlocked (derivation happens in the secure core; no private material is
returned).

**Query params:** `net_idx` (default `0`).

**Response:**

```json
{ "address": "0x1234…abcd", "symbol": "ETH" }
```

Returns `locked` if the vault is locked, or `bad_net` for a bad index.

#### `GET /api/balance`

Native (coin) balance for an address via the network's RPC.

**Query params:**

| Param | Required | Meaning |
|---|---|---|
| `net_idx` | yes (default `0`) | Network to query. |
| `address` | yes | Address to query. |

Example: `GET /api/balance?net_idx=0&address=0xabc…`

**Response:**

```json
{ "balance": "1000000000000000000", "decimals": 18, "symbol": "ETH" }
```

### Transactions

The flow is: **build** (get a decoded review) → **sign** (physical confirm) →
optionally **broadcast**. All three take the same transaction-intent body.

**Transaction intent body fields:**

| Field | Type | Meaning |
|---|---|---|
| `net_idx` | number | Network to sign for (provides family + chain-id + RPC). |
| `to` | string | Recipient address. **Required.** |
| `amount` | string | Amount in base units (decimal string). |
| `token_contract` | string | Optional: ERC-20/TRC-20 address or SPL mint for a **token** transfer. Omit for a native transfer. |
| `token_decimals` | number | Optional: decimals for the token transfer. |
| `nonce` | number | Optional: EVM/Tron nonce (the device fills chain fields via RPC if omitted). |
| `eip1559` | bool | Optional: use an EIP-1559 (type-2) EVM transaction. |

The device fills in nonce/gas/fees/UTXOs/blockhash via RPC (`rpc_prepare_tx`) where
they are not supplied.

#### `POST /api/tx/build`

Return a decoded, human-readable review of the transaction **without signing**. Use
this to show the user what they are about to sign.

**Request:**

```json
{ "net_idx": 0, "to": "0xRecipient…", "amount": "1000000000000000000", "eip1559": true }
```

**Response:**

```json
{
  "to": "0xRecipient…",
  "amount": "1.0",
  "symbol": "ETH",
  "fee": "0.00021",
  "nonce": 7,
  "net_idx": 0
}
```

`amount`/`fee` are in display units. Returns `bad_net`, `bad_tx`, or
`describe_failed`.

#### `POST /api/tx/sign`

Sign the transaction. **Blocks until the user confirms on the device** (up to ~120s).
Same body as `/api/tx/build`.

**Response:**

```json
{ "signed_hex": "02f8..." }
```

`signed_hex` is the fully-serialized, broadcast-ready signed transaction as a hex
string. Returns `user_rejected` if the user declines, `timeout` if no confirmation
arrives, or `locked` if the vault is locked.

#### `POST /api/tx/broadcast`

Same as `/api/tx/sign`, but after signing the device **broadcasts** the signed
transaction via the network's RPC and returns the transaction id.

**Response (broadcast supported):**

```json
{ "signed_hex": "02f8...", "txid": "0xhash…" }
```

**Response (chain has no broadcast path configured):**

```json
{ "signed_hex": "02f8...", "warning": "signed ok but broadcast unsupported for this chain" }
```

#### `POST /api/offline/sign`

Identical to `/api/tx/sign` (signs, never broadcasts). Intended for the air-gapped
workflow where the radio is off and the signed hex is carried to an online machine
(e.g. via QR). Returns `{ "signed_hex": "…" }`.

### WiFi

#### `GET /api/wifi`

Current WiFi configuration and mode.

**Response:**

```json
{ "ap_ssid": "DibaVault", "sta_ssid": "HomeNet", "sta_updates_only": true, "mode": 1 }
```

Passwords are never returned.

#### `POST /api/wifi`

Update WiFi. Two shapes are accepted.

**Air-gap toggle** (takes precedence if present): turns the radio fully off or back
to AP mode.

```json
{ "airgap": true }
```

`airgap:true` → radio off (air-gapped); `airgap:false` → AP mode.

**Configuration update:** any subset of the following; omitted fields are unchanged.

```json
{
  "ap_ssid": "DibaVault",
  "ap_pass": "a-strong-ap-password",
  "sta_ssid": "HomeNet",
  "sta_pass": "home-wifi-password",
  "sta_updates_only": true,
  "connect_sta": true
}
```

If `connect_sta` is `true`, the device switches to AP+STA mode (joins the router
while keeping the hotspot). `sta_updates_only` is a policy flag restricting the
joined link to updates + read-only.

**Response:** `{ "ok": true }`

### OTA

#### `GET /api/ota/check`

Check for a firmware update on the dibachain release channel (or configured mirror).

**Response:**

```json
{ "current": "1.2.3", "update_available": false }
```

#### `POST /api/ota/apply`

Download, **verify against the pinned dibachain signing key**, install to the
inactive slot, then activate and reboot. An image that fails the signature check is
rejected.

**Request:**

```json
{ "url": "https://releases.example/dibavault-esp32s3-signed.bin" }
```

**Response:** `{ "ok": true }` is sent, then the device reboots into the new image.
Returns `no_url` if `url` is missing, or `ota_failed` if download/verification fails.

> Because the device reboots on success, the HTTP connection drops right after the
> `{ "ok": true }` response. A client should treat a dropped connection following a
> `200` as "update applied, device rebooting" and re-poll `/api/status` afterwards.

## Example flows (curl)

Assume the device is at `http://192.168.4.1`.

**1. Status**

```bash
curl http://192.168.4.1/api/status
```

**2. Unlock**

```bash
curl -X POST http://192.168.4.1/api/unlock \
  -H 'Content-Type: application/json' \
  -d '{"pin":"123456"}'
```

**3. List networks (find a `net_idx`)**

```bash
curl http://192.168.4.1/api/networks
```

**4. Add a token (USDC on network 0)**

```bash
curl -X POST http://192.168.4.1/api/tokens \
  -H 'Content-Type: application/json' \
  -d '{"net_idx":0,"symbol":"USDC","name":"USD Coin",
       "contract":"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48","decimals":6}'
```

**5. Get an account address, then its native and token balances**

```bash
curl "http://192.168.4.1/api/accounts?net_idx=0"
# -> {"address":"0xABC…","symbol":"ETH"}

curl "http://192.168.4.1/api/balance?net_idx=0&address=0xABC…"
curl "http://192.168.4.1/api/token/balance?net_idx=0&token_idx=0&address=0xABC…"
```

**6. Build → sign → broadcast a native transfer**

```bash
# Build a review (nothing is signed yet)
curl -X POST http://192.168.4.1/api/tx/build \
  -H 'Content-Type: application/json' \
  -d '{"net_idx":0,"to":"0xRecipient…","amount":"1000000000000000000","eip1559":true}'

# Sign (blocks until you press OK on the device), returns signed_hex
curl -X POST http://192.168.4.1/api/tx/sign \
  -H 'Content-Type: application/json' \
  -d '{"net_idx":0,"to":"0xRecipient…","amount":"1000000000000000000","eip1559":true}'

# Or sign AND broadcast in one call, returns signed_hex + txid
curl -X POST http://192.168.4.1/api/tx/broadcast \
  -H 'Content-Type: application/json' \
  -d '{"net_idx":0,"to":"0xRecipient…","amount":"1000000000000000000","eip1559":true}'
```

**7. Build → sign a token transfer** (add `token_contract` + `token_decimals`)

```bash
curl -X POST http://192.168.4.1/api/tx/broadcast \
  -H 'Content-Type: application/json' \
  -d '{"net_idx":0,"to":"0xRecipient…","amount":"1000000",
       "token_contract":"0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48","token_decimals":6}'
```

## Future app integration

The API is documented first so a companion **mobile/desktop app** and the firmware
can evolve against a stable contract. A well-behaved app would:

1. **Pair with the device** by joining its AP (or reaching it on the LAN) and calling
   `GET /api/status` to learn `provisioned`, `locked`, `airgap` and the firmware
   version.
2. **Guide first-boot** on a fresh device: `POST /api/hal` to describe the hardware,
   `POST /api/provision/new` (showing the returned words to the user to write down),
   then `POST /api/provision/confirm`. The app must **never persist the mnemonic**;
   it is shown once for the user to record offline.
3. **Unlock per session** with `POST /api/unlock`, surfacing `attempts_left` and
   respecting `pin_lockout`.
4. **Manage chains and tokens** with the `/api/networks` and `/api/tokens`
   endpoints, caching the returned `idx` values for later calls.
5. **Show balances** via `/api/accounts`, `/api/balance` and `/api/token/balance`.
6. **Send funds** with the **propose-on-app, confirm-on-device** pattern: call
   `/api/tx/build` to render a review in the app, then `/api/tx/sign` or
   `/api/tx/broadcast` and prompt the user to approve on the device. Handle
   `user_rejected` and `timeout` gracefully.
7. **Support air-gapped signing** by toggling `POST /api/wifi {"airgap":true}` and,
   in an offline session, using `/api/offline/sign` (or the serial console) with the
   signed hex carried back online out-of-band (e.g. via QR).
8. **Offer updates** through `/api/ota/check` and `/api/ota/apply`, treating a
   dropped connection after a `200` as "rebooting into the new image".

Design principle: **the device is the source of truth and the point of
confirmation.** The app proposes and displays; the device decides and signs. No
API call can extract a key or force a signature without the on-device button.
