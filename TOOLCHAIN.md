# Toolchain: building and flashing the flight tracker

This document covers everything you need to compile the firmware and put it on
the Elecrow 7" ESP32-S3 HMI display. It is written for both macOS and Windows
because the user's development environment is split between the two.

> **Performance note**: the Elecrow 7" panel is a 1024×600 RGB LCD driven by an
> ESP32-S3 at 240 MHz. The firmware can build in 5–7 minutes on an M-series Mac
> and 10–15 minutes on Windows. Be patient the first time — incremental builds
> after that are < 30 seconds.

---

## 1. Hardware

### 1.1 The board

* **Elecrow 7" ESP32-S3 HMI display** (CrowPanel 7.0", ESP32-S3-WROOM-1, 16 MB
  flash, 8 MB PSRAM, 1024×600 RGB LCD, GT911 capacitive touch, CH340 USB-UART
  bridge).
* USB-C cable — must be a **data** cable, not a charge-only cable. The cable
  that ships with the board is data-capable.
* The board exposes a single USB-C port that powers the board AND carries the
  CH340 UART. There is no separate programming header.

### 1.2 The CH340 driver

The on-board USB-UART bridge is a WCH CH340. The ESP-IDF `esptool.py` needs
direct access to the serial port, so macOS and Windows both need a CH340
driver installed. Most Linux distros already have `ch341.ko` in the kernel.

#### macOS

1. Apple's default macOS install does NOT include the CH340 driver.
2. Download the WCH CH340 driver from the official WCH site:
   <https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html>
3. Run the installer (`CH341SER_MAC.pkg`). It requires a reboot.
4. After reboot, plug in the board and check `System Information → USB` for
   `USB Serial` (the CH340 enumerates as "USB Serial" on macOS).
5. The serial port will appear at `/dev/cu.wchusbserial<NN>` where `<NN>` is
   some integer the OS picks. Common values: `1410`, `3110`, `5110`.

> **macOS 13+ kext approval**: Apple's TCC system may block the CH340 kext
> on first install. Open **System Settings → Privacy & Security**, scroll to
> the bottom, and click "Allow" next to the WCH developer entry. You may need
> to reboot a second time.

#### Windows

1. Windows 10/11 does NOT include the CH340 driver by default.
2. Download the WCH driver from <https://www.wch-ic.com/downloads/CH341SER_EXE.html>.
3. Extract the ZIP and run `SETUP.EXE`. Click "Install". The driver binds to
   `USB\VID_1A86&PID_7523`.
4. Plug in the board. **Device Manager → Ports (COM & LPT)** should show
   `USB-SERIAL CH340 (COM<n>)` where `<n>` is a number (often COM3, COM4, COM5).
5. Note the COM port number — you will pass it to `idf.py` as `-p COM<n>`.

> **Driver signing**: the WCH installer ships an unsigned `.inf` that's been
> accepted by Windows for years. If Windows shows a "Windows cannot verify
> the publisher" prompt, click "Install this driver software anyway".

#### Linux

No driver needed — `ch341` is in the kernel. The board appears at
`/dev/ttyUSB0` (or similar). Add yourself to the `dialout` group:

```bash
sudo usermod -aG dialout $(whoami)
# log out and back in
```

---

## 2. ESP-IDF installation

The project builds against **ESP-IDF v5.x** (v5.1 and v5.3 both tested). Older
v4.x will NOT work — the WiFi API and `esp_lcd` panel API changed.

### 2.1 macOS

There are two install paths. The official `install.sh` is recommended.

#### Recommended: `install.sh`

```bash
# System prerequisites
xcode-select --install           # Xcode command-line tools
brew install cmake ninja dfu-util ccache python3

# Clone ESP-IDF v5.3 (stable)
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf
./install.sh esp32s3

# Activate the IDF environment (do this in every new shell)
source ~/esp/esp-idf/export.sh
```

After `export.sh`, the `idf.py` command is on your PATH and `$IDF_PATH` is set.

#### Adding to your shell profile

Add this to `~/.zshrc` (or `~/.bashrc` if you use bash):

```bash
alias get_idf='source ~/esp/esp-idf/export.sh'
```

Then `get_idf` activates IDF in the current shell.

### 2.2 Windows

#### Prerequisites

* **Python 3.10 or later** — from <https://www.python.org/downloads/windows/>.
  In the installer, tick "Add Python to PATH".
* **Git for Windows** — from <https://git-scm.com/download/win>. Use the
  default options.
* **CMake** — install via the official MSI from
  <https://cmake.org/download/>. Choose "Add CMake to the system PATH".
* **Ninja** — download from <https://github.com/ninja-build/ninja/releases>
  and put `ninja.exe` somewhere on `PATH`.

#### Recommended: `install.bat`

Open **Git Bash** (Git for Windows ships Git Bash), then:

```bash
# ESP-IDF requires C: drive and a path without spaces
mkdir -p /c/esp
cd /c/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf
./install.bat esp32s3
```

After `install.bat`, IDF is installed and the offline environment files are
generated. To use it in a Git Bash shell:

```bash
source /c/esp/esp-idf/export.sh
```

> **Windows Command Prompt users**: run `C:\esp\esp-idf\export.bat` instead.
> **PowerShell users**: run `C:\esp\esp-idf\export.ps1`.

#### Visual Studio Build Tools (only if installation asks)

ESP-IDF can use the MSVC compiler OR the MinGW-w64 toolchain. The recommended
MSVC path requires **Visual Studio Build Tools 2022** with the "Desktop
development with C++" workload. The full installer is ~6 GB.

MinGW-w64 is a lightweight alternative (no VS install needed). The
`install.sh` / `install.bat` script will offer this choice on Windows.

---

## 3. The CH340 boot-mode dance

The ESP32-S3 needs to be in **download mode** to receive a new image. The
Elecrow board has a USB-UART bridge that can put the ESP32 into download mode
automatically using the **DTR/RTS** lines — no button press required.

`esptool.py` (and thus `idf.py`) handles this automatically when invoked with
`--before default_reset --after hard_reset`. This is the default for both
`flash` and `app-flash`. You should rarely need to hold the BOOT button.

### 3.1 If the auto-reset dance fails

If the board is stuck in a non-flashing state (e.g. the previous firmware
crashed during boot before releasing the RTS line), do this:

1. **Hold the BOOT button** (small tactile button on the board, often labelled
   "BOOT" or "IO0").
2. While holding BOOT, **press and release the RST button** (small tactile
   button, often labelled "RST" or "EN").
3. **Release the BOOT button**.
4. The board is now in download mode. Run your `idf.py` command.
5. After flashing, press RST once to exit download mode and boot the new
   firmware.

### 3.2 The NVS partition is the catch

The board stores the WiFi credentials and the OpenSky API client_id/secret in
the **NVS partition** (`nvs` in `partitions.csv`). They survive a normal
`idf.py flash` ONLY if you flash the app image at offset `0x10000` and the
NVS partition at `0x9000` is left untouched.

ESP-IDF has two flash commands:

| Command       | What it does                                                                |
|---------------|-----------------------------------------------------------------------------|
| `idf.py flash`        | Writes bootloader + partition table + **app image + NVS**. Wipes NVS.       |
| `idf.py app-flash`    | Writes bootloader + partition table + **app image only**. Preserves NVS.    |

**Always use `app-flash`**, not `flash`. The `flash` command will wipe your
WiFi credentials and OpenSky API client_id/secret, forcing you to re-enter
them via the web UI on the next boot.

```bash
# Correct — preserves NVS
idf.py -p /dev/cu.wchusbserial3110 app-flash

# macOS — wrong port name? Find it:
ls /dev/cu.wchusbserial*

# Windows — correct form
idf.py -p COM3 app-flash          # adapt to your COM port number
```

If you ever need to do a full erase (e.g. to fix a corrupted NVS):

```bash
idf.py -p /dev/cu.wchusbserial3110 erase-flash
# Then re-flash everything from scratch with `idf.py flash`
# (and re-enter your WiFi/OpenSky credentials via the web UI)
```

---

## 4. Building

```bash
# 1. Activate the IDF environment
source ~/esp/esp-idf/export.sh          # macOS / Git Bash
# or: C:\esp\esp-idf\export.bat         # Windows CMD

# 2. cd into the project's `src` directory
cd /Users/jps/Downloads/Flight-radar-7-main/src
# Windows: cd /c/Users/jps/Downloads/Flight-radar-7-main/src

# 3. Configure (only needed once; subsequent builds keep the config)
idf.py set-target esp32s3

# 4. Build
idf.py build
```

The first build takes 5–10 minutes. Subsequent builds are incremental and
take ~20 seconds if only a few files changed.

### 4.1 Build output

The build writes the final image to:

```
build/lvgl_porting.bin
```

The app image is exactly **1.5 MB** and takes ~20 seconds to flash over the
CH340 at 460800 baud.

---

## 5. Flashing

```bash
# macOS
idf.py -p /dev/cu.wchusbserial3110 app-flash

# Windows
idf.py -p COM3 app-flash
```

If you don't know the port name:

```bash
# macOS
ls /dev/cu.usb* /dev/cu.wch*

# Windows
# Look in Device Manager → Ports (COM & LPT) for USB-SERIAL CH340 (COM<n>)
```

A successful flash ends with:

```
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
Done
```

The board will reboot into the new firmware automatically.

---

## 6. Monitoring serial output

ESP-IDF includes a serial monitor that reads the UART and pretty-prints
log output. **This is the single most useful debugging tool.**

```bash
# macOS
idf.py -p /dev/cu.wchusbserial3110 monitor

# Windows
idf.py -p COM3 monitor
```

To flash AND monitor in one command:

```bash
idf.py -p /dev/cu.wchusbserial3110 app-flash monitor
# Press Ctrl-] to exit the monitor
```

The firmware emits `LVGL` INFO logs, `WIFI` / `OpenSky` logs, and assertion
messages. To increase verbosity:

```bash
# In menuconfig
idf.py menuconfig
# → Component config → Log output → Default log verbosity → Debug
```

---

## 7. First-boot configuration

The board has no UI on the first boot because it has no WiFi credentials
and no OpenSky API client_id/secret. The flow is:

1. **Boot**: the firmware starts the WiFi manager in default-nothing mode
   and starts an HTTP webserver on `http://192.168.4.1/` (the board's
   softAP).
2. **Connect to the softAP** from your phone or laptop. The SSID is
   `FlightRadar-Setup` and the password is `flightradar`.
3. The captive portal opens at `http://192.168.4.1/`. Three sub-pages:
   * `/` — Status (IP address, current configuration)
   * `/upload` — Paste your OpenSky `client_id` and `client_secret`. These
     are stored in NVS and used on every subsequent boot.
   * `/clear` — Wipe the stored NVS credentials (forces re-provisioning).
4. Once the credentials are in NVS, the board connects to your home WiFi
   and starts fetching from OpenSky.

### 7.1 OpenSky API credentials

You need a free OpenSky Network account and OAuth2 client credentials:

1. Register at <https://opensky-network.org/> (free).
2. Sign in and go to **Account → API Access → My API access**.
3. Click **Create client** and copy the resulting `client_id` and
   `client_secret`.
4. Paste them into the `/upload` page on the board's softAP.

The OAuth2 token has a 1-hour lifetime; the firmware handles refresh
internally.

> **Rate limits**: the OpenSky public API allows 400 requests/day for
> anonymous users and 4000 requests/day for authenticated users. The
> firmware polls every 22 seconds by default, which is well within the
> authenticated limit.

---

## 8. Troubleshooting

| Symptom                                          | Fix                                                                                                                                  |
|--------------------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------|
| `A fatal error occurred: Failed to connect`     | Driver not installed; check `ls /dev/cu.wchusbserial*` (macOS) or Device Manager (Windows).                                          |
| `Timed out waiting for packet header`           | Board not in download mode. Hold BOOT, tap RST, release BOOT, then retry.                                                          |
| `Invalid head of packet`                         | Baud rate. ESP-IDF tries 460800 then falls back to 115200 automatically. If that fails, hold BOOT+RST and retry.                   |
| `Returned writing 0x00000000` then `Verify failed` | Power issue. Use a different USB-C cable (data-capable). Plug into a wall adapter, not a hub.                                    |
| Board boots but shows garbled LCD                | Wrong `sdkconfig` flash mode. The Elecrow panel needs `FLASH_MODE_DIO`, `FLASH_FREQ_80M`, `FLASH_SIZE_16MB`. Run `idf.py menuconfig` and check *Serial flasher config*. |
| Board boots but loop-crashes with `Guru Meditation Error` | Check `idf.py monitor` for the offending task. Common culprits: out-of-memory in the OpenSky fetch task (heap < 30 KB), or a NULL pointer in `Radar_DrawAircraft` when `gAircraftCount = 0`. |
| WiFi keeps disconnecting                        | 2.4 GHz network only. The ESP32-S3 does NOT support 5 GHz.                                                                        |
| OpenSky returns 401 Unauthorized                 | Bad `client_id` / `client_secret`. Visit `/clear` on the softAP, then `/upload` to re-enter.                                       |

---

## 9. Where to get help

* ESP-IDF programming guide: <https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32s3/>
* LVGL documentation: <https://docs.lvgl.io/8.4/>
* OpenSky Network API: <https://opensky-network.org/apidoc/>
* Elecrow 7" ESP32-S3 wiki: <https://www.elecrow.com/wiki/7.0_inch_ESP32-S3_HMI_Display.html>

The project's `ARCHITECTURE.md` explains the codebase. Read that
before diving into the source.
