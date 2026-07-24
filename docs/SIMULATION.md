# Testing DibaVault without hardware

You do **not** need a physical ESP32 to try DibaVault. Three options, from most
to least complete:

| Method | Exercises | Doesn't cover |
|---|---|---|
| **Wokwi** | boot, serial console, SSD1306 OLED, buttons, crypto, NVS, STA WiFi + RPC over real internet | full AP web-UI browsing |
| **QEMU** (`idf.py qemu`) | boot, serial console, NVS, seed generation, address derivation, signing | WiFi radio, OLED, buttons |
| **Host unit test** | pure crypto / address / signing correctness | anything ESP-specific |

> Reminder: this is experimental, unaudited firmware. Only ever generate or
> import **test** seeds in a simulator, and only use **testnet** funds. A seed
> typed into a simulator is not secret.

---

## 1. Wokwi (recommended)

Wokwi simulates the ESP32-S3, an SSD1306 OLED, push buttons, and even a virtual
WiFi network (`Wokwi-GUEST`) with **real internet access** — so join-router
(STA) mode and RPC balance queries actually work.

This repo ships a ready wiring: [`diagram.json`](../diagram.json) (ESP32-S3 +
SSD1306 on GPIO 8/9 + OK/Cancel buttons on GPIO 0/14) and
[`wokwi.toml`](../wokwi.toml).

**Steps**

```bash
idf.py set-target esp32s3
idf.py build
```

Then either:

- **VS Code** — install the *Wokwi Simulator* extension, open this folder, press
  `F1` → **Wokwi: Start Simulator**.
- **CLI** — install [`wokwi-cli`](https://docs.wokwi.com/wokwi-ci/cli-installation)
  and run `wokwi-cli .` in the repo root.

**First-run flow in the simulator** (serial console at 115200):

```
help                       # list commands
setup-new -w 12 1234       # create a 12-word wallet, PIN 1234 (prints the words)
unlock 1234
addresses                  # shows your BTC / EVM / TRX / SOL receive addresses
```

To light up the OLED, provision the display in the setup wizard (or over the web
UI): choose **SSD1306**, I²C SDA **8**, SCL **9**, and the two buttons on **0**
and **14** — matching `diagram.json`. To test balances, configure STA WiFi to
SSID `Wokwi-GUEST` (open, no password) and the default RPC endpoints will be
reachable.

---

## 2. QEMU (`idf.py qemu`)

Espressif ships a QEMU fork that runs the firmware headless. Great for exercising
the vault, NVS, and all four chains' address/signing logic through the console.

```bash
# one-time: install the emulator tool
python "$IDF_PATH/tools/idf_tools.py" install qemu-xtensa qemu-riscv32

idf.py set-target esp32          # esp32 has the most mature QEMU support
idf.py build
idf.py qemu monitor              # boots the firmware; Ctrl-] to exit
```

WiFi and I²C peripherals are not emulated, so the device comes up headless and
the console is your interface. The full crypto path — TRNG-seeded BIP39
generation, BIP32/44 derivation, and secp256k1 / ed25519 signing — runs exactly
as on hardware.

---

## 3. Host unit tests (crypto correctness)

The `chains` and `trezor-crypto` code is portable C. To validate address
derivation and signing against known BIP39 test vectors on your PC, build a
small host harness that links `components/chains` and the trezor-crypto sources
and checks derived addresses against a reference wallet. (A ready-made host test
target is on the roadmap — see the main [README](../README.md).)

---

© dibachain — GPL-3.0
