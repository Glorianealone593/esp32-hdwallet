# DibaVault installer

A single interactive tool that installs DibaVault onto an ESP32 — the same way
on **Windows, Linux, and macOS**. It can download a prebuilt release from GitHub
or build from source, finds the serial port for you, and flashes the firmware.

## Requirements

- **Python 3.7+** ( https://python.org — on Windows tick *"Add Python to PATH"* ).
- A USB cable to your ESP32 board.
- `esptool` — the tool installs it automatically if it's missing.

## Run it

**Windows:** double-click `flash.bat` (or run `py dibavault_flash.py`).

**Linux / macOS:**

```bash
./flash.sh
# or:  python3 dibavault_flash.py
```

You don't even need to clone the repo to *download-and-flash* — just grab
`dibavault_flash.py` on its own and run it.

## What it asks you

1. **What to do** — download a release & flash, build from source & flash, or
   just erase the chip.
2. **Which chip** — `esp32`, `esp32s3`, or `esp32c3`.
3. **Serial port** — it lists the ports it detects; pick one (or type it, e.g.
   `COM5` on Windows, `/dev/ttyUSB0` on Linux).
4. **Baud rate** — default `460800`.

It then downloads/builds the right binaries, shows exactly what it will write,
asks for confirmation, and flashes. On Linux you may need permission for the
serial port: `sudo usermod -aG dialout $USER` (then log out/in), or run with
`sudo`.

## After flashing

Open a serial monitor at **115200 baud**, or join the device's WiFi hotspot and
browse to **http://192.168.4.1** to set your PIN, choose your hardware, and write
down your recovery words.

---

© dibachain — GPL-3.0
