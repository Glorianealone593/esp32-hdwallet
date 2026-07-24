<div align="center">

# DibaVault

**An open-source hierarchical-deterministic (HD) crypto wallet firmware for ESP32**

_by **dibachain**_

Bitcoin · EVM · Tron · Solana — signed on-device, keys never leave the secure core.

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Targets](https://img.shields.io/badge/targets-ESP32%20%7C%20S3%20%7C%20C3-orange.svg)](docs/HARDWARE.md)

</div>

---

DibaVault turns an inexpensive ESP32 development board into a self-custody hardware
wallet. It generates and stores a BIP39 seed on-device, derives Bitcoin, EVM, Tron
and Solana accounts from it, and signs transactions locally after a physical
confirmation on a button/OLED. A separate, untrusted connectivity layer (a WiFi
hotspot, a local web UI, RPC clients and signed OTA updates) can view balances and
build unsigned transactions, but it is architecturally prevented from ever touching
your keys. WiFi can be turned off entirely for fully air-gapped, offline signing.

---

## ⚠️ Security disclaimer — read this first

> **DibaVault is experimental firmware. It has NOT been independently audited.**
>
> - **No independent security audit** has been performed on this firmware. Review
>   the source yourself before trusting it.
> - **The ESP32 has no dedicated secure element** (no certified tamper-resistant
>   key store). Keys are protected by flash encryption, secure boot and a
>   PIN-derived wrapping key — good practices, but **not** equivalent to a
>   purpose-built secure chip. See [`docs/SECURITY.md`](docs/SECURITY.md).
> - Commodity ESP32 boards are exposed to **physical side-channel and fault-injection
>   attacks** and to **supply-chain risks**. Assume a determined attacker with
>   physical access can eventually extract secrets.
> - **Test on testnets first**, and with **small amounts** you can afford to lose,
>   before considering any real funds.
> - This software is provided **"as is", without warranty of any kind**, under the
>   GPL-3.0. **You use it entirely at your own risk.**
>
> Before storing real value, work through the checklist in
> [`docs/SECURITY.md`](docs/SECURITY.md#pre-flight-checklist-before-real-funds).

---

## Table of contents

- [Features](#features)
- [Supported chains](#supported-chains)
- [Hardware requirements](#hardware-requirements)
  - [Example wiring — ESP32-S3 + SSD1306 + 2 buttons](#example-wiring--esp32-s3--ssd1306--2-buttons)
- [Quick start (prebuilt release)](#quick-start-prebuilt-release)
- [Build from source](#build-from-source)
- [Security model](#security-model)
- [Offline (air-gapped) signing](#offline-air-gapped-signing)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)
- [راهنمای فارسی](#راهنمای-فارسی)

---

## Features

- **Multi-chain HD wallet** — one BIP39 seed, multiple coins (see table below).
- **Secure-core / connectivity split** — private keys live in a dedicated,
  isolated FreeRTOS task. The networking code never links against the key
  code; it can only talk to the vault through one narrow IPC boundary
  ([`vault_ipc.h`](components/secure_core/include/vault_ipc.h)).
- **Physical confirmation to sign** — every signature requires an explicit
  button press with the decoded transaction shown on the OLED (or on the
  console/web summary on headless setups).
- **Encrypted, PIN-wrapped seed at rest** — the seed lives in a dedicated
  encrypted NVS partition and is additionally wrapped with a key derived from
  your PIN. A raw flash dump without the PIN yields only ciphertext.
- **WiFi hotspot for watch-only use** — bring the device up as its own AP and
  browse balances / build transactions from a phone or laptop, no internet
  required. Signing still needs the button.
- **Join-router mode, restrictable to updates-only** — connect to your router
  to reach RPC endpoints and pull OTA updates; this can be policy-limited to
  "updates + read-only".
- **Bring your own networks & RPCs** — add custom EVM chains (by chain-id),
  Bitcoin nodes, Tron and Solana endpoints from the web UI or console.
- **Air-gapped offline signing** — disable the radio entirely and sign
  transactions you paste in by hand. See [below](#offline-air-gapped-signing).
- **Signed OTA updates** — firmware images are verified against a **pinned
  dibachain public key** before they are ever booted.
- **Runtime hardware configuration** — display type, button GPIOs and I2C pins
  are chosen once at first boot and stored in NVS, so a single firmware image
  runs on any wiring and on any of the three supported chips.

## Supported chains

| Chain family | Coin(s) | BIP44 coin type | Curve | Address / signing notes |
|---|---|---|---|---|
| **Bitcoin** | BTC | `0'` | secp256k1 | Native SegWit `bc1…` (P2WPKH, BIP84) by default; legacy `1…` (P2PKH, BIP44) supported. PSBT-style inputs. |
| **EVM** | ETH + any EVM chain by chain-id | `60'` | secp256k1 / keccak256 | EIP-55 checksummed `0x…` addresses; legacy and EIP-1559 transactions; ERC-20 transfers. |
| **Tron** | TRX | `195'` | secp256k1 | Base58Check `T…` addresses (`0x41` prefix); TRC-20 transfers. |
| **Solana** | SOL | `501'` | ed25519 | Base58 public-key addresses; SPL token transfers. |

Cryptography is provided by [trezor-crypto](https://github.com/trezor/trezor-crypto),
included as a git submodule under
[`components/trezor-crypto/lib`](components/trezor-crypto). The ESP32 hardware RNG
is bound in as the entropy source (upstream's stub RNG is deliberately not
compiled — see [`components/trezor-crypto/CMakeLists.txt`](components/trezor-crypto/CMakeLists.txt)).

## Hardware requirements

- One of: **ESP32**, **ESP32-S3** (recommended), or **ESP32-C3**.
- Flash: **8 MB recommended** for dual-slot OTA. 4 MB modules are supported with
  a reduced partition layout (typical for ESP32-C3).
- A display and at least one button are strongly recommended so that transaction
  review and confirmation happen **on the device**. Supported out of the box:
  SSD1306 / SH1106 OLEDs (I2C) and ST7789 TFTs (SPI). Headless operation is
  possible but weaker — the firmware and UI warn you about it.
- Optional: a status LED.

Hardware is not selected at compile time. You wire your board however you like,
then describe the wiring once during first-boot provisioning. Details, per-chip
GPIO advice and more example wirings are in [`docs/HARDWARE.md`](docs/HARDWARE.md).

### Example wiring — ESP32-S3 + SSD1306 + 2 buttons

A minimal, recommended build: a 128×64 SSD1306 OLED on I2C and two buttons
(OK / Cancel).

| Peripheral | Signal | ESP32-S3 GPIO | Notes |
|---|---|---|---|
| SSD1306 OLED | SDA | **GPIO8** | I2C data |
| SSD1306 OLED | SCL | **GPIO9** | I2C clock |
| SSD1306 OLED | VCC | 3V3 | |
| SSD1306 OLED | GND | GND | |
| Button — OK | to GND | **GPIO4** | active-low, internal pull-up |
| Button — Cancel | to GND | **GPIO5** | active-low, internal pull-up |
| Status LED (opt.) | anode via resistor | **GPIO2** | optional |

- OLED I2C address is typically `0x3C`.
- Buttons connect the GPIO to **GND**; enable the internal pull-up and set
  "active low" during provisioning.
- Avoid the strapping and SPI-flash/PSRAM pins on each chip. Per-chip
  "pins to avoid" tables are in [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Quick start (prebuilt release)

You do **not** need a build environment to try DibaVault — you can flash a signed
release binary from GitHub Releases.

1. **Install `esptool`** (the Espressif flashing tool):

   ```bash
   pip install esptool
   ```

2. **Download** the firmware set for your chip from the
   [dibachain/esp32-hdwallet Releases](https://github.com/dibachain/esp32-hdwallet/releases)
   page. Each target ships `bootloader.bin`, `partition-table.bin` and
   `dibavault.bin`, plus a `FLASHING.txt` with the exact offsets.

3. **Flash** (offsets shown are for the ESP32-S3 8 MB layout; the release's
   `FLASHING.txt` has the authoritative values for your target):

   ```bash
   esptool.py -p /dev/ttyUSB0 -b 460800 \
     --chip esp32s3 write_flash \
     0x0     bootloader.bin \
     0x8000  partition-table.bin \
     0x20000 dibavault.bin
   ```

   > On ESP32-S3/C3 the bootloader offset is `0x0`; on the classic ESP32 it is
   > `0x1000`. Always follow the `FLASHING.txt` included with the release.

4. **First-boot setup.** Open the serial console at **115200 baud**
   (`idf.py -p /dev/ttyUSB0 monitor`, or any serial terminal), or connect to the
   device's WiFi hotspot and browse to **`http://192.168.4.1`**. You will be
   guided through:
   - choosing your **display type, button GPIOs and I2C pins**,
   - setting a **PIN**,
   - generating a **new 12/18/24-word seed** (or importing an existing one),
   - and **writing down your recovery words**. The words are shown only during
     this initial setup (over the AP-only web UI, or on the local console) and
     never again over the network.

> **Reminder:** prebuilt public releases are for evaluation. For real use, review
> the source, build it yourself, and enable flash encryption + secure boot
> (see [`docs/BUILD.md`](docs/BUILD.md)).

## Build from source

You need [ESP-IDF **v5.x**](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
installed and its environment sourced (`. $IDF_PATH/export.sh`).

```bash
# 1. Clone WITH submodules (trezor-crypto is a submodule).
git clone --recursive https://github.com/dibachain/esp32-hdwallet.git
cd esp32-hdwallet
# If you forgot --recursive:
#   git submodule update --init --recursive

# 2. Select your target (esp32s3 recommended; also esp32, esp32c3).
idf.py set-target esp32s3

# 3. Build, flash and open the console.
idf.py build flash monitor
```

Full instructions for all three targets, production security fuses, and signing
keys are in [`docs/BUILD.md`](docs/BUILD.md).

## Security model

DibaVault is split into two domains that share only one, deliberately narrow,
message-passing boundary. The **secure core** owns the seed, derives keys, runs
the physical-confirmation gate and signs. The **connectivity layer** (WiFi, HTTP,
RPC, OTA) is treated as untrusted: it never links against the key code and can
only send request/response messages through
[`vault_ipc.h`](components/secure_core/include/vault_ipc.h). There is, by
construction, **no message that returns a private key, seed or mnemonic** across
that boundary, and **no way to sign without a decoded, human-reviewable summary
plus physical confirmation**.

```
        UNTRUSTED  (connectivity layer)                 TRUSTED  (secure core)
  ┌───────────────────────────────────────┐        ┌──────────────────────────────┐
  │  WiFi AP / STA hotspot                 │        │  keystore  (seed, PIN wrap)   │
  │  HTTP web UI  (http://192.168.4.1)     │        │  key derivation (BIP32/39)    │
  │  JSON API / RPC clients                │        │  signer  (secp256k1 / ed25519)│
  │  OTA client (verifies dibachain key)   │        │  confirm gate (button/OLED)   │
  │                                        │        │                              │
  │  can: read balances, build UNSIGNED tx │        │  holds ALL private material   │
  │  cannot: see keys, sign without button │        │  runs on its own pinned core  │
  └───────────────────┬───────────────────┘        └───────────────┬──────────────┘
                      │                                             │
                      │        vault_ipc  (the ONE boundary)        │
                      │  request queue  ─────────────────────────▶  │
                      │   VREQ_SIGN_TX (decoded, reviewable) …      │
                      │  ◀─────────────────────────  reply queue    │
                      │   signature / signed tx  (NEVER a key)      │
                      └─────────────────────────────────────────────┘
                                          ▲
                                          │  physical button press required
                                     ┌────┴─────┐
                                     │  HUMAN   │  reviews tx on OLED, approves
                                     └──────────┘
```

Requests carry an **origin** (`local console`, `on-device buttons`, or
`remote HTTP`) and the vault applies stricter policy to remote ones — for example,
the recovery mnemonic can be revealed only to a **local** origin, never over HTTP.

The full threat model, what is and isn't protected, and the ESP32's known
limitations as a wallet platform are documented in
[`docs/SECURITY.md`](docs/SECURITY.md). The architecture, IPC message flow and
task/core pinning are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Offline (air-gapped) signing

For the strongest posture, run DibaVault with the radio fully off and never let
it touch a network:

1. During setup, choose a build with a display + button and set **WiFi = off**
   (the firmware reports the air-gapped state via `wifi_mgr_is_airgapped()`).
2. On an online, watch-only machine, gather the transaction details you need
   (recipient, amount, fee, and chain-specific fields such as EVM nonce/gas,
   a Solana recent blockhash, or a Bitcoin PSBT).
3. On the device, over the **local serial console**, enter the transaction
   details for the chosen account. The vault **re-decodes and re-encodes** the
   canonical signing payload itself — it never trusts a raw blob — and shows you
   a human-readable summary.
4. **Review on the OLED and press OK.** The device outputs the signed,
   broadcast-ready transaction (and can render it as a QR code).
5. Broadcast the signed transaction from your online machine.

Because the private keys never leave the secure core and the radio is off, an
online attacker has no path to them. This is the recommended mode for significant
funds. See [`docs/SECURITY.md`](docs/SECURITY.md) for caveats.

## Documentation

| Document | Contents |
|---|---|
| [`docs/SECURITY.md`](docs/SECURITY.md) | Threat model, protections, limitations, disclosure, pre-flight checklist |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Component map, IPC boundary & flow, task/core pinning, partitions, adding a chain |
| [`docs/BUILD.md`](docs/BUILD.md) | Build/flash for all targets, production fuses, signing keys, releases |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Supported boards, runtime HAL config, per-chip wiring & pins to avoid |

## Contributing

Issues and pull requests are welcome at
[github.com/dibachain/esp32-hdwallet](https://github.com/dibachain/esp32-hdwallet).
Please keep the secure-core / connectivity boundary intact: the connectivity
layer must never include `keystore.h` or the signer, and the build's boundary
check enforces this. Security reports should follow the responsible-disclosure
process in [`docs/SECURITY.md`](docs/SECURITY.md).

## License

DibaVault is released under the **GNU General Public License v3.0 (GPL-3.0)**.
See [`LICENSE`](LICENSE). Copyright © dibachain.

The bundled [trezor-crypto](https://github.com/trezor/trezor-crypto) submodule is
distributed under its own license by its respective authors.

---

## راهنمای فارسی

**دیبا‌ولت (DibaVault)** یک فریم‌ور کیف‌پول سخت‌افزاری متن‌باز برای بردهای ESP32
است که توسط **dibachain** توسعه داده می‌شود. این دستگاه کلید خصوصی شما را روی خودِ
سخت‌افزار تولید و نگه‌داری می‌کند و تراکنش‌ها را به‌صورت محلی و پس از تأیید فیزیکی
(فشردن دکمه) امضا می‌کند.

**هشدار امنیتی مهم:**
- این فریم‌ور **آزمایشی** است و **هنوز به‌صورت مستقل ممیزی (audit) نشده است**.
- تراشهٔ ESP32 **عنصر امن اختصاصی (secure element) ندارد**؛ محافظت از کلید بر پایهٔ
  رمزنگاری فلش، بوت امن و پین است، اما هم‌ارز یک تراشهٔ امن تخصصی نیست.
- در برابر حملات فیزیکی (کانال جانبی و تزریق خطا) و خطرات زنجیرهٔ تأمین آسیب‌پذیر است.
- **ابتدا روی شبکه‌های آزمایشی (testnet) و با مبالغ کم** استفاده کنید. مسئولیت
  استفاده کاملاً بر عهدهٔ شماست.

**زنجیره‌های پشتیبانی‌شده:** بیت‌کوین، شبکه‌های EVM (اتریوم و هر زنجیرهٔ سازگار با
chain-id)، ترون (TRX) و سولانا (SOL).

**معماری امنیتی:** بخش «هستهٔ امن» کلیدها را نگه می‌دارد و امضا می‌کند؛ بخش
«اتصال» (وای‌فای، رابط وب، RPC و به‌روزرسانی OTA) نامطمئن در نظر گرفته می‌شود و
هرگز به کد کلیدها لینک نمی‌شود. این دو تنها از طریق یک مرز باریک پیام‌رسانی
(`vault_ipc.h`) با هم صحبت می‌کنند و **هیچ کلید خصوصی از این مرز عبور نمی‌کند**.
وای‌فای را می‌توان کاملاً خاموش کرد تا امضای کاملاً آفلاین (air-gapped) انجام شود.

**شروع سریع:**
1. با `pip install esptool` ابزار فلش را نصب کنید.
2. فایل‌های نسخهٔ مناسب تراشهٔ خود را از بخش Releases دانلود کنید.
3. با `esptool.py … write_flash …` (مطابق فایل `FLASHING.txt`) فلش کنید.
4. در نخستین راه‌اندازی، از طریق کنسول سریال یا هات‌اسپات وای‌فای در نشانی
   **`http://192.168.4.1`** پین را تعیین کنید، سخت‌افزار (نمایشگر/دکمه‌ها) را
   انتخاب کنید و **کلمات بازیابی (۱۲/۱۸/۲۴ کلمه) را یادداشت کنید**. این کلمات فقط
   یک‌بار و در همان مرحلهٔ راه‌اندازی نمایش داده می‌شوند.

**ساخت از منبع:** با `git clone --recursive`، سپس `idf.py set-target esp32s3` و
`idf.py build flash monitor` (نیازمند ESP-IDF نسخهٔ ۵). جزئیات کامل در
[`docs/BUILD.md`](docs/BUILD.md).

برای مطالعهٔ کامل به مستندات انگلیسی و پوشهٔ [`docs/`](docs) مراجعه کنید.

---

<div align="center">

© dibachain — GPL-3.0

</div>
