# DibaVault — Build & Flash

_Copyright © dibachain. GPL-3.0._

How to build, flash and release DibaVault for all three supported targets, and how
to enable production security fuses and signing keys.

## Contents

- [Prerequisites](#prerequisites)
- [Get the source (with submodules)](#get-the-source-with-submodules)
- [Build & flash per target](#build--flash-per-target)
- [Injecting the version](#injecting-the-version)
- [Flashing a clean-slate device](#flashing-a-clean-slate-device)
- [Production: flash encryption + secure boot](#production-flash-encryption--secure-boot)
- [Signing keys (secure boot + OTA)](#signing-keys-secure-boot--ota)
- [Creating a GitHub release](#creating-a-github-release)

## Prerequisites

- **ESP-IDF v5.x** installed and activated. Follow Espressif's
  [Get Started](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
  guide, then in each shell:

  ```bash
  . $IDF_PATH/export.sh
  ```

- Python tooling that ships with ESP-IDF: `esptool.py`, `espsecure.py`,
  `espefuse.py`.
- A USB cable and the serial port of your board (e.g. `/dev/ttyUSB0`,
  `/dev/ttyACM0`, or `COMx`).

The CI/release pipeline uses the `espressif/idf:release-v5.3` Docker image; you can
reproduce a build locally with the same image if you prefer not to install IDF.

## Get the source (with the crypto submodule)

The cryptography lives in a git submodule at `components/trezor-crypto/lib`, which is
the **[trezor-firmware](https://github.com/trezor/trezor-firmware) monorepo**.
DibaVault builds **only its `crypto/` directory** (a curated source list in
[`components/trezor-crypto/CMakeLists.txt`](../components/trezor-crypto/CMakeLists.txt)).

Initialize the submodule **shallow and non-recursively**. Do **not** use
`--recursive`: trezor-firmware declares many nested submodules DibaVault does not
need, and pulling them all is slow and unnecessary.

```bash
git clone https://github.com/AliAkrami1375/esp32-hdwallet.git
cd esp32-hdwallet

# Fetch ONLY the trezor-firmware submodule, shallow (no nested submodules):
git submodule update --init --depth 1
```

The build fails fast with a clear message if the submodule (specifically
`lib/crypto/bip32.c`) is missing.

## Build & flash per target

Select the target once (this generates `sdkconfig` from the matching
`sdkconfig.defaults.<target>`), then build/flash/monitor. Replace the port as
needed.

### ESP32-S3 (recommended)

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### ESP32 (classic)

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### ESP32-C3

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Notes:

- Switching targets: run `idf.py set-target <t>` again (or delete `build/` and
  `sdkconfig`). The default flash size and partition table follow the per-target
  defaults (8 MB dual-OTA for ESP32/S3; 4 MB layout for C3).
- The serial monitor runs at **115200 baud**. Exit with `Ctrl+]`.
- Nothing secret is ever logged; release builds also lower log verbosity.

## Injecting the version

The firmware version is a compile definition, `DIBAVAULT_VERSION`. Local builds
fall back to `0.1.0-dev` (see [`CMakeLists.txt`](../CMakeLists.txt)). To stamp an
explicit version:

```bash
idf.py -DDIBAVAULT_VERSION="1.2.3" build
```

CI injects this automatically from the pushed git tag (`v1.2.3` → `1.2.3`).

## Flashing a clean-slate device

`idf.py flash` writes the bootloader, partition table and app at the right offsets
for you. To flash the individual artifacts by hand (e.g. from a release), use the
offsets ESP-IDF reports at the end of a build, for example on ESP32-S3:

```bash
esptool.py -p /dev/ttyUSB0 -b 460800 --chip esp32s3 write_flash \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x20000 build/dibavault.bin
```

> Offsets differ per chip. On the **classic ESP32** the bootloader is at `0x1000`,
> not `0x0`. Always trust the offsets printed by your build or the `FLASHING.txt`
> shipped with a release.

To wipe a device to factory (erasing NVS, including any stored seed) before
re-provisioning:

```bash
esptool.py -p /dev/ttyUSB0 erase_flash
```

> Erasing flash destroys the on-device seed. Make sure you have your recovery words
> first.

## Production: flash encryption + secure boot

Production builds enable **flash encryption**, **Secure Boot v2** and **NVS
encryption**. In the default dev config these are commented out in
[`sdkconfig.defaults`](../sdkconfig.defaults); enable them via `menuconfig` for a
production build:

```bash
idf.py menuconfig
#  Security features →
#    [*] Enable flash encryption on boot        (CONFIG_SECURE_FLASH_ENC_ENABLED)
#    [*] Enable hardware Secure Boot in bootloader (CONFIG_SECURE_BOOT)
#        Secure Boot version → Secure Boot V2    (CONFIG_SECURE_BOOT_V2_ENABLED)
#  and keep NVS Encryption enabled               (CONFIG_NVS_ENCRYPTION)
```

> ## ⚠️ IRREVERSIBLE — read before you burn fuses
>
> Enabling flash encryption and Secure Boot **burns eFuses**. This is a **one-way,
> permanent** operation:
> - It **cannot be undone**. The board can no longer be plain-flashed or freely
>   read back afterwards.
> - A mistake (wrong keys, wrong mode, interrupted burn) can **permanently brick**
>   the board.
> - You must also **disable JTAG / USB-Serial-JTAG** eFuses in production, or they
>   remain an extraction path.
>
> **Practise the entire flow on a throwaway board first.** Use **Release mode**
> flash encryption for production (Development mode is for testing only). Keep the
> keys you generate below backed up **offline and secret** — losing the secure-boot
> key means you can never sign an update for that device again.

Follow the official guides step by step; this document does not replace them:

- Flash encryption:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/flash-encryption.html>
- Secure Boot v2:
  <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html>

## Signing keys (secure boot + OTA)

DibaVault uses two independent key pairs. **Both private keys stay offline and out
of the repo** (`.gitignore` already excludes `*.pem`, `*.key`, and `keys/`).

### 1. Secure Boot v2 signing key

Signs the bootloader/app so the chip will boot only your firmware.

```bash
# Generate (RSA-3072 or ECDSA per your Secure Boot V2 scheme selection).
espsecure.py generate_signing_key --version 2 keys/secure_boot_signing_key.pem
```

Point the build at it in `menuconfig` (Secure Boot → signing key path), or let the
build sign automatically when Secure Boot is enabled. Guard this key: it is the
root of trust for the device.

### 2. OTA image signing key (pinned dibachain key)

DibaVault verifies every OTA image against a **pinned dibachain public key** before
installing it (see [`ota.h`](../components/connectivity/include/ota.h)). Generate an
ECDSA key pair for signing release images:

```bash
# Private signing key (keep offline & secret):
espsecure.py generate_signing_key --version 2 keys/ota_signing_key.pem

# Public key to pin into the firmware (extract and embed at build time):
espsecure.py extract_public_key --version 2 \
  --keyfile keys/ota_signing_key.pem keys/ota_public_key.pem
```

The public key is embedded in the firmware; the private key signs each release
image. An image not signed by this key is rejected and discarded by the device.

> If you enable Secure Boot, you can use the same key material for app signing per
> the Secure Boot V2 scheme; the OTA signature check is an additional
> application-level gate on top of it. Manage both keys as top-secret, offline
> assets, ideally on separate media with backups.

## Creating a GitHub release

Releases are produced by CI on a tag push and can also be built by hand.

### Automated (recommended)

Tag and push; the [`release` workflow](../.github/workflows/release.yml) builds all
three targets in the `espressif/idf:release-v5.3` container, injects the version
from the tag, collects `bootloader.bin`, `partition-table.bin` and `dibavault.bin`
per target, generates a `FLASHING.txt`, and publishes a GitHub Release with the
binaries attached.

```bash
git tag v1.2.3
git push origin v1.2.3
```

### Manual

```bash
# For each target:
idf.py set-target esp32s3
idf.py -DDIBAVAULT_VERSION="1.2.3" build

# Collect the three artifacts:
#   build/bootloader/bootloader.bin
#   build/partition_table/partition-table.bin
#   build/dibavault.bin

# Sign the app image with the OTA key before publishing (so devices accept it OTA):
espsecure.py sign_data --version 2 --keyfile keys/ota_signing_key.pem \
  --output dibavault-esp32s3-signed.bin build/dibavault.bin

# Then attach the binaries to a GitHub Release, e.g. with the gh CLI:
gh release create v1.2.3 \
  dibavault-esp32s3-signed.bin build/bootloader/bootloader.bin \
  build/partition_table/partition-table.bin \
  --title "DibaVault v1.2.3" --notes "See CHANGELOG."
```

Always include per-target flashing offsets (a `FLASHING.txt`) so users flash to the
correct addresses for their chip.
