# Flight Radar

A live aircraft radar for the **Elecrow 7" ESP32-S3 HMI display**. Pulls
real-time ADS-B data from the [OpenSky Network](https://opensky-network.org/),
plots aircraft on a green radar dial with a sweeping arm, and lets you tap
to select any aircraft in range and see its callsign, altitude, speed,
heading, climb rate, and origin country.

> SquareLine Studio 1.5.3 (LVGL 8.4) on the front end, ESP-IDF v5.x on
> the back, two FreeRTOS tasks, three LVGL timers, one HTTP webserver for
> first-boot provisioning.

![overview](docs/screenshot.png)

---

## What it does

* Live aircraft plot on a 460×460 px radar dial with three range rings and
  N/E/S/W compass labels.
* The icon size scales with the OpenSky `category` field — heavy jets
  draw larger than light aircraft.
* Tap any aircraft to highlight it (yellow ring) and read its info card
  on the right rail.
* Selected aircraft gets a fading dashed white trail of its recent path.
* "API: 22s" label at the bottom of the radar shows how stale the data
  is (turns red after 60s).
* Adjustable radar radius (10–250 km), adjustable refresh interval (22s
  default), switchable imperial/metric units.
* First-boot WiFi + OpenSky credential provisioning via the device's
  built-in softAP and captive portal — no serial cable required.

---

## Hardware

* **Elecrow 7" ESP32-S3 HMI display** (CrowPanel 7.0"). 1024×600 RGB LCD,
  GT911 capacitive touch, ESP32-S3-WROOM-1, 16 MB flash, 8 MB PSRAM.
* Single USB-C cable for power + programming (CH340 USB-UART bridge).
* The board has a built-in BM8563 RTC on I2C; the firmware uses it for
  the clock display.

---

## Quick start

### 1. Install the toolchain

See **[TOOLCHAIN.md](TOOLCHAIN.md)** for the full instructions. Both
macOS and Windows are supported. The short version:

```bash
# macOS
brew install cmake ninja dfu-util python3
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf && ./install.sh esp32s3
source ~/esp/esp-idf/export.sh
```

```bash
# Windows (Git Bash)
mkdir -p /c/esp && cd /c/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf && ./install.bat esp32s3
source /c/esp/esp-idf/export.sh
```

### 2. Install the CH340 driver

The board's USB-UART bridge needs the WCH CH340 driver. macOS and
Windows don't ship it. Download from
<https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html> (macOS) or
<https://www.wch-ic.com/downloads/CH341SER_EXE.html> (Windows). See
TOOLCHAIN.md §1.2 for details.

### 3. Build and flash

```bash
cd src
idf.py set-target esp32s3       # only on first run
idf.py build
idf.py -p /dev/cu.wchusbserial3110 app-flash    # macOS
# or: idf.py -p COM3 app-flash                  # Windows
```

> **Always use `app-flash`, not `flash`**. The `flash` command wipes
> the NVS partition that stores your WiFi and OpenSky credentials.
> See TOOLCHAIN.md §3.2.

### 4. Configure on first boot

1. Connect to the softAP `FlightRadar-Setup` (password: `flightradar`).
2. Open `http://192.168.4.1/upload` in a browser.
3. Paste your OpenSky `client_id` and `client_secret`. (Register for
   free at <https://opensky-network.org/>.)
4. The board reboots, connects to your home WiFi, and starts drawing
   aircraft.

### 5. Monitor serial output

```bash
idf.py -p /dev/cu.wchusbserial3110 monitor
# Or, chained:
idf.py -p /dev/cu.wchusbserial3110 app-flash monitor
```

---

## Architecture

See **[ARCHITECTURE.md](ARCHITECTURE.md)** for the big read. The short
version:

* **Two FreeRTOS tasks** — OpenSky fetch (22s polling) and dead-reckon
  (4 Hz position integration).
* **Three LVGL timers** — radar sweep (30ms), UI status updates (500ms),
  and the LVGL tick.
* **One shared state struct** (`gAircraft[200]`) — rebuilt from scratch
  on every fetch. Selection is by ICAO24 hex, not by index.
* **One HTTP webserver** — for first-boot OpenSky credential
  provisioning on the softAP.
* **NVS partition** — stores WiFi creds, OpenSky OAuth2 creds, user
  prefs (units, trail, range, refresh).

The `src/simulator/` directory is a parallel desktop build that runs
the same portable code on SDL2/libcurl. Useful for testing without the
board.

---

## Repo layout

```
.
├── README.md            ← you are here
├── ARCHITECTURE.md      ← design doc: how it works
├── TOOLCHAIN.md         ← build + flash + driver install for Mac & Windows
├── LICENSE
├── src/
│   ├── main/            ← Portable C + LVGL port + SquareLine UI
│   ├── components/      ← Vendored LVGL + touch drivers
│   ├── simulator/       ← SDL2 desktop build
│   ├── partitions.csv
│   ├── sdkconfig
│   └── sdkconfig.defaults
└── docs/
    └── screenshot.png
```

---

## Features

| Feature                                | Where it lives                                                                |
|----------------------------------------|-------------------------------------------------------------------------------|
| Radar drawing (rings, sweep, aircraft) | `src/main/radar.c::radar_draw_cb`                                            |
| Aircraft icons (size by category)      | `src/main/radar.c::DrawAircraft`                                             |
| Selection model (auto-pick closest)    | `src/main/radar.c::Radar_ReconcileSelection`                                 |
| Trail (fading dashed white)            | `src/main/radar.c::draw_trail` + `Radar_PredictAircraft`                      |
| OpenSky OAuth2 + JSON parsing          | `src/main/opensky_client.c`                                                  |
| Position dead-reckon between fetches   | `src/main/radar.c::Radar_PredictAircraft`                                    |
| Capture UI handler                     | `src/main/ui/ui_events.c::Radar_TapSelect`                                   |
| Right-rail info card                   | `src/main/app_state.c::UpdateSelectedAircraftUI`                              |
| API-age label                          | `src/main/app_state.c::AppState_UpdateAgeLabel`                               |
| First-boot webserver                   | `src/main/webserver.c`                                                       |
| CH340 reboot + app-flash               | `src/main/main.c::app_main` (then `idf.py -p ... app-flash`)                |

---

## License

See [LICENSE](LICENSE). MIT.
