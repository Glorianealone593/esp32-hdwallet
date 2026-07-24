#!/usr/bin/env python3
# DibaVault flashing & install tool — dibachain
# Cross-platform (Windows / Linux / macOS). Interactive.
#
# What it does:
#   * Download a prebuilt release from GitHub and flash it, OR build from source
#     and flash, OR just erase the chip.
#   * Auto-detect the serial port the ESP32 is on (or let you type it).
#   * Install esptool automatically if it is missing.
#
# Requirements: Python 3.7+. Everything else is handled for you.
#
# Run it:
#   python dibavault_flash.py            (Windows: py dibavault_flash.py)
# or use the flash.sh / flash.bat launchers next to this file.

import os
import sys
import ssl
import json
import shutil
import tempfile
import subprocess
import urllib.request

REPO = "AliAkrami1375/esp32-hdwallet"
API = f"https://api.github.com/repos/{REPO}/releases"

# Flash layout per target: (label -> (asset-suffix, offset)).
# Matches the release workflow (.github/workflows/release.yml).
LAYOUT = {
    "esp32": [
        ("bootloader.bin",        "0x1000"),
        ("partition-table.bin",   "0x8000"),
        ("ota_data_initial.bin",  "0xf000"),
        ("dibavault.bin",         "0x20000"),
        ("storage.bin",           "0x3c0000"),
    ],
    "esp32s3": [
        ("bootloader.bin",        "0x0"),
        ("partition-table.bin",   "0x8000"),
        ("ota_data_initial.bin",  "0xf000"),
        ("dibavault.bin",         "0x20000"),
        ("storage.bin",           "0x3c0000"),
    ],
    "esp32c3": [
        ("bootloader.bin",        "0x0"),
        ("partition-table.bin",   "0x8000"),
        ("ota_data_initial.bin",  "0xe000"),
        ("dibavault.bin",         "0x20000"),
        ("storage.bin",           "0x3a0000"),
    ],
}
TARGETS = list(LAYOUT.keys())


# ----------------------------- small UI helpers -----------------------------
def hr():
    print("-" * 64)


def banner():
    hr()
    print("  DibaVault installer  —  dibachain")
    print("  ESP32 hardware wallet firmware flasher")
    hr()


def ask(prompt, default=None):
    d = f" [{default}]" if default is not None else ""
    try:
        v = input(f"{prompt}{d}: ").strip()
    except EOFError:
        v = ""
    return v or (default if default is not None else "")


def choose(prompt, options, default_index=0):
    print(prompt)
    for i, o in enumerate(options):
        mark = "*" if i == default_index else " "
        print(f"  {mark} {i + 1}) {o}")
    while True:
        raw = ask("Enter number", str(default_index + 1))
        if raw.isdigit() and 1 <= int(raw) <= len(options):
            return int(raw) - 1
        print("  invalid choice, try again")


def confirm(prompt, default=True):
    d = "Y/n" if default else "y/N"
    v = ask(f"{prompt} ({d})", "").lower()
    if not v:
        return default
    return v.startswith("y")


def die(msg, code=1):
    print("\nERROR: " + msg)
    sys.exit(code)


# ----------------------------- dependencies ---------------------------------
def ensure_esptool():
    try:
        import esptool  # noqa: F401
        return
    except ImportError:
        pass
    print("esptool is not installed.")
    if not confirm("Install it now with pip?", True):
        die("esptool is required. Install with:  pip install esptool")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", "esptool"])
    try:
        import esptool  # noqa: F401
    except ImportError:
        die("esptool installed but not importable. Try: pip install esptool")


def list_serial_ports():
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    ports = []
    for p in list_ports.comports():
        desc = f"{p.device}  ({p.description})" if p.description else p.device
        ports.append((p.device, desc))
    return ports


def pick_port():
    ports = list_serial_ports()
    if ports:
        labels = [d for (_, d) in ports] + ["Enter a port manually"]
        idx = choose("\nSerial ports detected:", labels, 0)
        if idx < len(ports):
            return ports[idx][0]
    hint = "e.g. COM5" if os.name == "nt" else "e.g. /dev/ttyUSB0"
    port = ask(f"\nSerial port ({hint})")
    if not port:
        die("no serial port given")
    return port


# ----------------------------- GitHub download ------------------------------
def http_get_json(url):
    ctx = ssl.create_default_context()
    req = urllib.request.Request(url, headers={"User-Agent": "dibavault-flasher"})
    with urllib.request.urlopen(req, context=ctx, timeout=30) as r:
        return json.loads(r.read().decode())


def download(url, dest):
    ctx = ssl.create_default_context()
    req = urllib.request.Request(url, headers={"User-Agent": "dibavault-flasher"})
    with urllib.request.urlopen(req, context=ctx, timeout=120) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)


def fetch_release(target):
    print("\nQuerying GitHub releases...")
    try:
        releases = http_get_json(API)
    except Exception as e:
        die(f"could not reach GitHub ({e}). Check your internet connection.")
    releases = [r for r in releases if not r.get("draft")]
    if not releases:
        die("no releases published yet. Choose 'Build from source' instead, or "
            "wait for the release workflow to finish.")
    tags = [f"{r['tag_name']}{' (pre-release)' if r.get('prerelease') else ''}" for r in releases]
    idx = choose("\nAvailable releases:", tags, 0)
    rel = releases[idx]

    wanted = [suffix for (suffix, _) in LAYOUT[target]]
    assets = {}
    for a in rel.get("assets", []):
        name = a["name"]
        if f"-{target}-" not in name:
            continue
        for suffix in wanted:
            if name.endswith(suffix):
                assets[suffix] = a["browser_download_url"]
    missing = [s for s in wanted if s not in assets]
    if missing:
        die(f"release {rel['tag_name']} is missing {missing} for {target}. "
            "The build may still be running — try again shortly.")

    tmp = tempfile.mkdtemp(prefix="dibavault_")
    files = []
    print(f"\nDownloading {rel['tag_name']} for {target} ...")
    for suffix, off in LAYOUT[target]:
        dest = os.path.join(tmp, suffix)
        print(f"  {off:>10}  {suffix}")
        download(assets[suffix], dest)
        files.append((off, dest))
    return files


# ----------------------------- build from source ----------------------------
def build_from_source(target):
    if not shutil.which("idf.py"):
        die("idf.py not found. Install ESP-IDF v5.x and run its export script "
            "first (see docs/BUILD.md), then re-run this tool.")
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if not os.path.isfile(os.path.join(repo_root, "CMakeLists.txt")):
        die("run this from inside the esp32-hdwallet repository to build from source.")
    print(f"\nBuilding for {target} (this can take a few minutes)...")
    subprocess.check_call(["idf.py", "set-target", target], cwd=repo_root)
    subprocess.check_call(["idf.py", "build"], cwd=repo_root)
    b = os.path.join(repo_root, "build")
    files = []
    for suffix, off in LAYOUT[target]:
        if suffix == "bootloader.bin":
            path = os.path.join(b, "bootloader", "bootloader.bin")
        elif suffix == "partition-table.bin":
            path = os.path.join(b, "partition_table", "partition-table.bin")
        else:
            path = os.path.join(b, suffix)
        if not os.path.isfile(path):
            die(f"expected build output missing: {path}")
        files.append((off, path))
    return files


# ----------------------------- flashing -------------------------------------
def run_esptool(args):
    cmd = [sys.executable, "-m", "esptool"] + args
    print("\n$ " + " ".join(cmd))
    subprocess.check_call(cmd)


def erase(target, port, baud):
    run_esptool(["--chip", target, "-p", port, "-b", str(baud), "erase_flash"])


def flash(target, port, baud, files):
    args = ["--chip", target, "-p", port, "-b", str(baud),
            "--before", "default_reset", "--after", "hard_reset",
            "write_flash", "-z"]
    for off, path in files:
        args += [off, path]
    run_esptool(args)


# ----------------------------- main flow ------------------------------------
def main():
    banner()
    mode = choose(
        "\nWhat do you want to do?",
        ["Download a release from GitHub and flash it",
         "Build from source and flash",
         "Erase the chip only (wipes everything, incl. any stored seed)"],
        0)

    target = TARGETS[choose("\nWhich chip?", TARGETS, 0)]
    ensure_esptool()
    port = pick_port()
    baud = ask("Baud rate", "460800")

    if mode == 2:
        if not confirm(f"\nERASE the entire {target} on {port}? This is irreversible", False):
            die("cancelled", 0)
        erase(target, port, baud)
        print("\nDone. The chip is erased.")
        return

    files = fetch_release(target) if mode == 0 else build_from_source(target)

    print("\nReady to flash:")
    print(f"  chip : {target}")
    print(f"  port : {port}")
    print(f"  baud : {baud}")
    for off, path in files:
        print(f"  {off:>10}  {os.path.basename(path)}")

    if confirm("\nErase the chip first? (recommended for a clean install)", False):
        erase(target, port, baud)

    if not confirm("\nProceed with flashing?", True):
        die("cancelled", 0)

    flash(target, port, baud, files)

    hr()
    print("  Flashing complete.")
    print("  Next: open the serial monitor at 115200 baud, OR join the device's")
    print("  WiFi hotspot and browse to http://192.168.4.1 to set your PIN,")
    print("  choose your hardware, and write down your recovery words.")
    hr()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\naborted.")
        sys.exit(130)
    except subprocess.CalledProcessError as e:
        die(f"a command failed (exit {e.returncode}). See the output above.")
