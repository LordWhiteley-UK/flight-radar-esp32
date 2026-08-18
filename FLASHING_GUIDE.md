# Beginner's Guide: Erase & Program the ELECROW ESP32 7" Display (800×480)

This guide walks you through setting up the toolchain, erasing the board, and flashing the **Flight-radar-7** firmware onto an **ELECROW ESP32 7" HMI Advanced IPS Touch Display (800×480)**.

The board is an **ESP32-S3** module with:
- 16 MB QIO flash
- 8 MB octal PSRAM
- RGB parallel LCD (800×480)
- GT911 capacitive touch controller
- An RTC (BM8563) on I²C (SDA=GPIO15, SCL=GPIO16)

The firmware is an **ESP-IDF** project (not Arduino), built with ESP-IDF **v5.1 or newer**, LVGL 8.4.x, and the `esp_lcd_touch_gt911` driver. The ELECROW 7" panel is the same RGB panel/driver family as the Waveshare 7" RGB LCD, which is why the code uses `waveshare_rgb_lcd_port.c`.

---

## 0. What you'll need

| Item | Notes |
|------|-------|
| ELECROW 7" ESP32 display | USB-C power + data cable connected |
| A USB-C data cable | Must carry data, not charge-only. Ideally one that can supply ~2 A. |
| A computer | macOS, Windows, or Linux |
| ESP-IDF v5.1+ toolchain | See Section 1 |
| This project folder | `Flight-radar-7-main/src` |

> ⚠️ Power tip: a 7" RGB LCD draws a lot of current. If your computer's USB port can't keep the board alive during flashing (random resets, "Brownout detector was triggered"), use a powered USB hub or a USB-C wall charger that also passes data, or an external 5 V supply.

---

## 1. Install ESP-IDF (one time only)

You need ESP-IDF **v5.1.x or newer** (the project requires `>=5.1.0`). Recommended: **v5.1.5** or **v5.3.x** (stable, well-tested with the RGB LCD + PSRAM combo).

### macOS (Apple Silicon / Intel)

```bash
# Install prerequisites
brew install cmake ninja dfu-util python3

# Download ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3.3
cd esp-idf
./install.sh esp32s3
```

Then **activate** the environment in every new terminal you use to build/flash:

```bash
. ~/esp/esp-idf/export.sh
```

(That leading `.` and space are required — it sources the script.)

### Windows

Download the official installer: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/windows-setup.html>

Use the **"ESP-IDF Tools Installer"**. After install, use the **"ESP-IDF PowerShell"** or **"ESP-IDF CMD"** shortcut from the Start Menu — it runs `export.bat` for you automatically.

### Linux

```bash
sudo apt install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3.3
cd esp-idf && ./install.sh esp32s3
. ./export.sh
```

### Verify the install

```bash
idf.py --version
```

You should see a version line. If `idf.py: command not found`, you forgot to run `export.sh` (macOS/Linux) or open the ESP-IDF terminal (Windows).

---

## 2. Find your board's serial port

Plug the board in via USB-C.

**macOS:**
```bash
ls /dev/cu.*
```
Look for something like `/dev/cu.usbmodem*` or `/dev/cu.SLAB_USBtoUART`.

**Linux:**
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```
Usually `/dev/ttyUSB0`. If you get permission errors, add yourself to the `dialout` group:
```bash
sudo usermod -aG dialout $USER
# then log out and back in
```

**Windows:** Open Device Manager → *Ports (COM & LPT)* → note the `COMxx`.

> If **nothing** shows up: try a different USB-C cable (charge-only cables are the #1 cause), try a different port, and make sure the board's USB power switch (if it has one) is set to USB/ON.

---

## 3. Configure the project for the board

In a terminal with ESP-IDF active:

```bash
cd /Users/jps/Downloads/Flight-radar-7-main/src
idf.py set-target esp32s3
```

This writes `sdkconfig` for the ESP32-S3 using the defaults in `sdkconfig.defaults` (16 MB QIO flash, octal PSRAM, RGB LCD tearing-avoidance, etc.). Run it once; you only need to re-run it if you delete `sdkconfig`.

### Optional: review settings in menuconfig

```bash
idf.py menuconfig
```

The relevant menu is **Example Configuration → Display**. The defaults in `sdkconfig.defaults` already match the ELECROW 7" panel, so you usually don't need to change anything. Press `Q` then `Y` to save/exit.

> The radar range (50 km) and update interval (22 s) are **not** in menuconfig — they're already baked into the code in `main.c` and `radar.c`.

---

## 4. (Optional) Build first, to catch errors early

```bash
idf.py build
```

This takes a few minutes the first time (it compiles LVGL, the LCD driver, ESP-IDF, and your code). A successful build ends with:
```
Project build complete. To flash, run:
 idf.py flash
or
 idf.py -p <PORT> flash
```

If the build fails, the most common causes are:
- ESP-IDF not activated (`idf.py: command not found`) → run `export.sh`.
- Wrong target → run `idf.py set-target esp32s3`.
- Out-of-space / missing submodule → `git -C $IDF_PATH submodule update --init --recursive`.

---

## 5. Erase the flash (factory reset)

This wipes **everything** on the chip: your old app, the WiFi/OpenSky credentials saved in NVS, the saved radar range/lat/lon, and any stored settings. Do this before flashing if you want a truly clean state (recommended the first time you load this firmware, since it also clears any stale `range` NVS value that would otherwise override the new 50 km default).

```bash
idf.py -p <PORT> erase-flash
```

Replace `<PORT>` with the port you found in Section 2, e.g.:
- macOS:   `idf.py -p /dev/cu.usbmodem1101 erase-flash`
- Linux:   `idf.py -p /dev/ttyUSB0 erase-flash`
- Windows: `idf.py -p COM5 erase-flash`

You should see:
```
Erasing flash (this may take a while)...
Chip erase completed successfully in Xs
```

If you see `A fatal error occurred: Failed to connect to ESP32-S3`, see **Troubleshooting** below — the board is likely in a state that needs bootloader-mode entry.

---

## 6. Flash the firmware

```bash
idf.py -p <PORT> flash
```

This builds (if needed) and writes the firmware to the correct partition offsets from `partitions.csv`:

| Partition | Offset    | Size     | Purpose |
|-----------|-----------|----------|---------|
| nvs       | 0x9000    | 0x6000   | WiFi/credentials/saved radar settings |
| phy_init  | 0xf000    | 0x1000   | PHY calibration |
| factory   | 0x10000   | 0x600000 | Your app (6 MB) |
| spiffs    | 0x610000  | 0x9F0000| Filesystem (assets, etc.) |

Flashing takes ~30–90 s. On success:
```
Hash of data verified.
Hard resetting via RTS pin...
```

The board reboots automatically and the radar UI should appear on the LCD within a few seconds.

> Tip — combine erase + flash + monitor in one command:
> ```bash
> idf.py -p <PORT> erase-flash flash monitor
> ```

---

## 7. Watch what the board is doing (serial monitor)

```bash
idf.py -p <PORT> monitor
```

This opens a live log stream from the ESP32-S3. You'll see WiFi connect attempts, OpenSky API requests, and radar updates. Useful for confirming the new **22 s** interval and **50 km** range are active — you'll see an API fetch roughly every 22 seconds.

Useful monitor shortcuts:
- `Ctrl-]` — exit the monitor
- `Ctrl-T Ctrl-H` — help / list all shortcuts
- The monitor auto-resets the board on exit

Exit with `Ctrl-]` when done.

---

## 8. First-run setup on the device

After flashing and erasing, NVS is empty, so the app starts with:
- Default center: lat `13.1993`, lon `77.7067`
- Default range: **50 km**
- No WiFi credentials (until you set them)
- Update interval: **22 s**

The firmware includes a web server (`webserver.c`) for entering WiFi and OpenSky credentials over a captive-style page — see the project's own README/UI for the exact flow, since that part is device-specific. Once WiFi is up and OpenSky credentials are saved, the radar will start pulling live traffic every 22 s.

---

## 9. Common workflows (quick reference)

```bash
# Activate ESP-IDF (each new terminal)
. ~/esp/esp-idf/export.sh

# Go to project
cd /Users/jps/Downloads/Flight-radar-7-main/src

# One-time: set target
idf.py set-target esp32s3

# Clean erase + flash + watch
idf.py -p <PORT> erase-flash flash monitor

# Just rebuild after editing code
idf.py build && idf.py -p <PORT> flash

# Monitor only
idf.py -p <PORT> monitor
```

---

## 10. Troubleshooting

### "A fatal error occurred: Failed to connect to ESP32-S3"
The chip isn't responding to the auto-reset. Fixes, in order:
1. Hold **BOOT** on the board, tap **RESET**, then release **BOOT** — this forces download mode. Retry the command.
2. Try `idf.py -p <PORT> erase-flash --baud 115200` (slower is more reliable on flaky cables).
3. Swap the USB-C cable for one you **know** carries data.

### Brownout / random resets during flash or boot
The RGB LCD + PSRAM pull a lot of current. Use a powered USB hub or a USB-C charger that also passes data. Brownout logs look like:
```
Brownout detector was triggered
```

### The LCD stays dark / shows garbage
- Confirm `CONFIG_IDF_TARGET="esp32s3"` is set (run `idf.py set-target esp32s3` again).
- The ELECROW 7" uses the Waveshare-compatible RGB pinout; the defaults in `sdkconfig.defaults` already match. If you changed LCD options in menuconfig, revert them or re-apply defaults from `sdkconfig.defaults`.

### Touch doesn't work
The GT911 touch driver (`esp_lcd_touch_gt911`) needs I²C and an interrupt/RESET pin wired per the board. If touch is dead but the display is fine, double-check the board's I²C pull-ups and that no menuconfig option disabled I²C. On a clean re-flash it should enumerate automatically.

### NVS keeps loading the old range
Erasing the flash (Section 5) clears NVS. If you skipped the erase and the app still shows 100 km, run `idf.py -p <PORT> erase-flash` then re-flash. You can also set 50 km through the on-screen UI's range control so the new value is saved.

### `idf.py: command not found`
You didn't activate ESP-IDF in *this* terminal. Run `. ~/esp/esp-idf/export.sh` (macOS/Linux) or open the ESP-IDF terminal shortcut (Windows).

### Port disappears / flaky enumeration
- Avoid USB hubs without power; use a direct port or powered hub.
- On macOS, kill any other process holding the port (e.g. a previous `monitor`).
- Re-plug the cable.

### Flash size / partition errors
This project uses a 16 MB flash layout. If your specific ELECROW board has a different flash size, the build/flash will warn. Confirm with:
```bash
esptool.py --port <PORT> flash_id
```
It prints `Detected flash size: 16 MB` (or similar). If it's not 16 MB, edit `sdkconfig.defaults` → `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` and the `factory`/`spiffs` sizes in `partitions.csv` to match.

---

## 11. Where things live in this project (cheat sheet)

| File | Role |
|------|------|
| `src/sdkconfig.defaults` | Board defaults: ESP32-S3, 16 MB QIO flash, octal PSRAM, RGB tearing-avoidance |
| `src/partitions.csv` | Flash layout (nvs / phy / factory / spiffs) |
| `src/main/main.c` | App entry, WiFi, radar update task (interval), range default |
| `src/main/radar.c` | Radar drawing + display radius |
| `src/main/opensky_client.c` | OpenSky API client |
| `src/main/waveshare_rgb_lcd_port.c` | RGB LCD + touch init (ELECROW-compatible) |
| `src/main/lvgl_port.c` | LVGL porting layer |
| `src/main/webserver.c` | Web config server (WiFi/credentials) |

Changed in this session:
- Update interval: 15 s → **22 s** (`main.c`)
- Radar range: 100 km → **50 km** (`main.c` defaults + `radar.c` display radius)

---

Happy flashing! If a step fails, the serial monitor (`idf.py monitor`) is your best friend — it tells you exactly what the board is doing and where it got stuck.