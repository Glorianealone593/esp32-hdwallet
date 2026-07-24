# DibaVault — Hardware

_Copyright © dibachain. GPL-3.0._

DibaVault runs from a **single firmware image** on any of three ESP32 chips and on
whatever wiring you choose. The board layout is not compiled in — it is described
once during first-boot provisioning and stored in NVS. This document covers the
supported boards, the runtime HAL config concept, example wirings for each chip, and
which GPIOs to avoid.

## Contents

- [Supported boards](#supported-boards)
- [Supported peripherals](#supported-peripherals)
- [Runtime HAL configuration](#runtime-hal-configuration)
- [Example wiring — ESP32-S3](#example-wiring--esp32-s3)
- [Example wiring — ESP32 (classic)](#example-wiring--esp32-classic)
- [Example wiring — ESP32-C3](#example-wiring--esp32-c3)
- [GPIOs to avoid, per chip](#gpios-to-avoid-per-chip)
- [Choosing a build](#choosing-a-build)

## Supported boards

| Chip | Cores | Notes | Recommended flash |
|---|---|---|---|
| **ESP32-S3** (recommended) | 2 | Native USB (USB-Serial-JTAG), plenty of RAM, optional PSRAM. Best headroom. | 8 MB (dual-OTA) |
| **ESP32** (classic) | 2 | Widely available. No native USB (uses a UART bridge). | 8 MB (dual-OTA) |
| **ESP32-C3** | 1 (RISC-V) | Smallest/cheapest; single core, so both trust domains time-share one core. Native USB-Serial-JTAG. | 4 MB (reduced layout) |

Any dev board based on these chips works, provided you have free GPIOs for a display
and at least one button. A display + button is strongly recommended so transaction
review and confirmation happen on the device.

## Supported peripherals

- **Displays:** SSD1306 (128×64 / 128×32 OLED, I2C), SH1106 (1.3" OLED, I2C),
  ST7789 (color TFT, SPI, optional). Headless (`DV_DISPLAY_NONE`) is possible but
  weaker and is flagged in the UI.
- **Buttons / confirm modes:** 1-button (long-press = confirm), 2-button (OK /
  Cancel), or 3-button (Up / Down / OK for menu navigation). Buttons are typically
  wired to GND with the internal pull-up enabled (active-low).
- **Optional:** a single status LED.

Typical OLED I2C address is `0x3C` (some panels use `0x3D`).

## Runtime HAL configuration

The wiring is data, held in `dv_hal_config_t`
([`components/hal/include/hal_config.h`](../components/hal/include/hal_config.h)):
display type, I2C SDA/SCL/address, panel width/height, optional ST7789 SPI pins,
confirm mode and button GPIOs (`btn_ok`, `btn_cancel`, `btn_up`, `btn_down`,
`btn_active_low`), an optional status LED, and the confirmation timeout.

First-boot flow:

1. `hal_config_default_for_target()` prefills a sensible template for the detected
   chip so the setup UI has good defaults.
2. You edit the display type, I2C pins/address and button GPIOs in the setup UI
   (over the AP-only web UI at `http://192.168.4.1`, or the serial console).
3. `hal_config_validate()` checks the proposed config against the current chip —
   each pin must exist, must **not** be a strapping or SPI-flash/PSRAM pin, and pins
   must not collide — returning a human-readable `reason` if it rejects one.
4. `hal_config_save()` writes it to NVS; after that the layout is fixed for the
   device. (A factory reset clears it.)

Because of this, the GPIO numbers below are **examples** — pick any valid free pins
for your board and enter them at setup.

## Example wiring — ESP32-S3

128×64 SSD1306 OLED on I2C + two buttons (OK / Cancel). Recommended reference build.

| Function | Signal | GPIO | Notes |
|---|---|---|---|
| OLED (SSD1306) | SDA | **GPIO8** | I2C data |
| OLED (SSD1306) | SCL | **GPIO9** | I2C clock |
| OLED | VCC / GND | 3V3 / GND | addr `0x3C` |
| Button OK | to GND | **GPIO4** | active-low, pull-up |
| Button Cancel | to GND | **GPIO5** | active-low, pull-up |
| Status LED (opt.) | via resistor | **GPIO2** | optional |

Avoid GPIO0/3/45/46 (strapping), GPIO19/20 (native USB D-/D+ if you use USB), and
the flash/PSRAM pins (see table below).

## Example wiring — ESP32 (classic)

128×64 SSD1306 OLED on I2C + two buttons.

| Function | Signal | GPIO | Notes |
|---|---|---|---|
| OLED (SSD1306) | SDA | **GPIO21** | default I2C SDA |
| OLED (SSD1306) | SCL | **GPIO22** | default I2C SCL |
| OLED | VCC / GND | 3V3 / GND | addr `0x3C` |
| Button OK | to GND | **GPIO32** | active-low, pull-up |
| Button Cancel | to GND | **GPIO33** | active-low, pull-up |
| Status LED (opt.) | via resistor | **GPIO25** | optional |

GPIO34–39 are **input-only** (no internal pull-ups) — usable for buttons only if you
add an external pull-up; prefer regular GPIOs like 32/33 for buttons.

## Example wiring — ESP32-C3

128×64 SSD1306 OLED on I2C + two buttons.

| Function | Signal | GPIO | Notes |
|---|---|---|---|
| OLED (SSD1306) | SDA | **GPIO5** | I2C data |
| OLED (SSD1306) | SCL | **GPIO6** | I2C clock |
| OLED | VCC / GND | 3V3 / GND | addr `0x3C` |
| Button OK | to GND | **GPIO3** | active-low, pull-up |
| Button Cancel | to GND | **GPIO4** | active-low, pull-up |
| Status LED (opt.) | via resistor | **GPIO10** | optional |

Avoid GPIO2/8/9 (strapping), GPIO18/19 (native USB-Serial-JTAG), and the flash pins.

## GPIOs to avoid, per chip

These pins have boot-time (strapping) roles or are wired to internal SPI
flash/PSRAM. Do **not** use them for a display or buttons. `hal_config_validate()`
rejects the well-known ones, but confirm against your board's schematic — module
variants differ.

| Chip | Strapping pins | SPI flash / PSRAM (never use) | Native USB | Input-only |
|---|---|---|---|---|
| **ESP32** | GPIO0, GPIO2, GPIO5, GPIO12 (MTDI), GPIO15 | GPIO6–11 (and GPIO16/17 on WROVER PSRAM modules) | — (external UART bridge) | GPIO34–39 |
| **ESP32-S3** | GPIO0, GPIO3, GPIO45, GPIO46 | GPIO26–32; GPIO33–37 on Octal-flash/PSRAM modules | GPIO19 (D−), GPIO20 (D+) | — |
| **ESP32-C3** | GPIO2, GPIO8, GPIO9 | GPIO12–17 | GPIO18 (D−), GPIO19 (D+) | — |

Guidance:

- **Strapping pins** set boot mode/voltage; a pull from a button or panel at reset
  can prevent boot or select the wrong flash voltage. Avoid them for inputs.
- **Flash/PSRAM pins** are physically committed to the internal memory bus — using
  them will crash the device.
- **Native USB pins** (S3/C3): leave free if you rely on USB-Serial-JTAG for the
  console/flashing.
- **Input-only pins** (classic ESP32 GPIO34–39) have no internal pull-ups; add an
  external resistor if you must use them for a button.
- Leave I2C lines with pull-ups (many OLED breakouts include them); if not, enable
  internal pull-ups or add ~4.7 kΩ externally.

## Choosing a build

- For **real use**, provision a **display + at least one button** so the decoded
  transaction is reviewed and confirmed **on the device**. Headless mode relies on a
  software confirmation token and is explicitly weaker — the UI warns you.
- For **maximum security**, keep WiFi off (air-gapped) and drive signing from the
  serial console; see [`SECURITY.md`](SECURITY.md#air-gap-mode).
