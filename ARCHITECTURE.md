# Architecture

This document describes the design of the flight-radar firmware that runs on
the Elecrow 7" ESP32-S3 HMI display. It is meant to be read by a developer
(including a different AI) who has not seen the code before and needs to
understand how the pieces fit together before changing anything.

The source tree is small (one ELF, ~1.5 MB). The architecture is also small:
two FreeRTOS tasks, three LVGL timers, one HTTP webserver, and a single
shared mutable state struct reachable from anywhere.

---

## 1. Bird's-eye view

```
+---------------------------+        +---------------------------+
|  HTTP webserver (task)    |        |  OpenSky fetch (task)     |
|  Port 80, softAP + STA    |        | 22s polling w/ OAuth2     |
|  Stores creds in NVS      |        | Parses JSON into gAircraft|
+-------------+-------------+        +-------------+-------------+
              |                                    |
              v                                    v
        +------+----------------------------------+------+
        |                   NVS                          |
        |  WiFi creds, OpenSky OAuth2 creds,             |
        |  user prefs (units, trail, range, refresh)     |
        +------+-----------------------------------+-----+
                       |                            |
                       v                            v
        +--------------+-------+        +----------+----------+
        | Radar draw (LVGL cb) |        | Dead-reckon (task)  |
        | 30ms sweep timer     |        | 4Hz integrate pos   |
        | Aircraft icons + trail|       | Updates predictedLat |
        +----------------------+        | /Lon /Altitude      |
                                        +---------------------+
```

There are two CPU cores on the ESP32-S3. Tasks are pinned as follows:

| Task                  | Core | Pin reason                                            |
|-----------------------|------|-------------------------------------------------------|
| `radar_update_timer_cb` (OpenSky fetch) | 0  | HTTP client / DNS — network-bound, not latency-sensitive. |
| `RadarPredictTask`    | 0  | Same core as fetch; integrates positions. |
| LVGL timer callbacks  | called from main loop on whichever core is doing `lv_timer_handler` | not pin-controllable. |

The LVGL porting layer runs the LVGL tick + handler on whatever core is
free. **All LVGL widget code is single-threaded** — that's why you'll see
`platform_lvgl_lock()` around widget-touching code in `main.c`.

---

## 2. Source layout

```
src/
├── main/                         # ESP-IDF app entry point + portable C
│   ├── main.c                    # app_main, WiFi, OpenSky fetch task, predict task
│   ├── main.h
│   ├── app_state.c               # Shared state, LVGL timer callbacks, NVS load/save
│   ├── radar.c                   # Radar drawing (LVGL draw callback)
│   ├── radar.h
│   ├── opensky_client.c          # OpenSky OAuth2 + JSON parsing
│   ├── opensky_client.h
│   ├── webserver.c               # HTTP server for first-boot provisioning
│   ├── webserver.h
│   ├── platform.h                # Platform abstraction (ESP32 vs SDL2)
│   ├── platform_esp32.c          # ESP32 implementation of platform.h
│   ├── plane_icon.c              # (unused — kept for future icon work)
│   ├── plane_icon.h
│   ├── bm8563_min.c              # RTC driver (board has a BM8563 on I2C)
│   ├── bm8563_min.h
│   ├── lvgl_port.c               # LVGL ↔ RGB LCD bridge
│   ├── lvgl_port.h
│   ├── waveshare_rgb_lcd_port.c  # Vendor-supplied LCD init (untouched)
│   ├── waveshare_rgb_lcd_port.h
│   ├── ui/                       # SquareLine Studio-generated UI
│   │   ├── ui.h
│   │   ├── ui.c
│   │   ├── ui_events.c           # Event handlers (toggleTrail, etc.)
│   │   ├── ui_helpers.c
│   │   └── screens/
│   │       ├── ui_Screen1.c      # Main radar screen
│   │       ├── ui_Screen1.h
│   │       ├── ui_Screen2.c      # WiFi picker
│   │       ├── ui_Screen3.c      # Settings (coords, units, refresh)
│   │       └── ...
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── components/                   # Vendored LVGL + touch drivers
│   ├── espressif__esp_lcd_touch/
│   ├── espressif__esp_lcd_touch_gt911/
│   └── lvgl__lvgl/
├── simulator/                    # SDL2 desktop simulator (build with `make`)
├── partitions.csv                # NVS + factory + spiffs layout
├── sdkconfig                     # ESP-IDF runtime config
├── sdkconfig.defaults
└── CMakeLists.txt
```

The `simulator/` subdirectory is a parallel implementation of the same
program using SDL2. It links the same `main/`, `app_state.c`, `radar.c`,
`opensky_client.c`, `webserver.c` files and provides a `platform_sdl.c`
implementation of `platform.h`. **All portable logic is in the shared
files** — the ESP32 target and the SDL simulator are bit-for-bit identical
on the application logic.

---

## 3. Concurrency model

### 3.1 What runs where

| Thread / task                       | What it does                                                                                                                     |
|-------------------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| FreeRTOS `radar_update_timer_cb`    | Every 22s: HTTP GET to OpenSky → POST events refreshed → JSON parsed into `gAircraft[]` → reconcile selection → `Radar_Refresh()`. |
| FreeRTOS `RadarPredictTask`         | Every 250ms: integrate `velocity` × `dt` into `predictedLat/Lon/Altitude` for every aircraft. Refreshes trail point if trail on. |
| LVGL timer `AppState_SweepTimerCb`  | Every 30ms: rotate `sweepAngle` by 3° → invalidate radar object → redraw.                                                        |
| LVGL timer `AppState_AircraftUiTimerCb` | Every 500ms: re-render the right-rail info card, the API-age label, and the clock. |
| LVGL event handlers (touch)         | Run on the LVGL thread, only when the user taps.                                                                                 |
| HTTP webserver (background)         | FreeRTOS task; only mutates NVS. Never touches widgets.                                                                          |

### 3.2 The shared state

There is a single mutable state struct declared at the top of `app_state.c`
and incremented only inside `platform_lvgl_lock()` + `Radar_Refresh()` calls:

```c
Aircraft gAircraft[MAX_AIRCRAFT];  // 200 × ~80 bytes = ~16 KB
int gAircraftCount;
char selectedIcao24[16];
int selectedAircraft;
bool showAircraftLabels;
bool showSelectedTrail;
```

`gAircraft[]` is rebuilt from scratch on every OpenSky fetch — *never* edited
in-place. The fetch task therefore does not need to lock against the LVGL
thread; the lock is only taken across the `Radar_ReconcileSelection`
+ `UpdateSelectedAircraftUI` + `Radar_Refresh` triple.

### 3.3 The lock

```c
if (platform_lvgl_lock(0)) {
    Radar_ReconcileSelection();
    UpdateSelectedAircraftUI();
    Radar_Refresh();
    platform_lvgl_unlock();
}
```

On ESP32, `platform_lvgl_lock` is a FreeRTOS mutex (`xSemaphoreTakeRecursive`).
On the SDL simulator, it is a `pthread_mutex_t`. The lock is **recursive** so
a callback may safely call `lv_obj_invalidate` from inside another
`lv_obj_invalidate` chain.

### 3.4 What CANNOT happen

You cannot:
* touch LVGL widgets from `RadarPredictTask` without taking the lock
* call `Radar_Refresh()` from a non-LVGL thread (the invalidate is enqueued
  onto the LVGL thread by the lock)
* read `gAircraft[]` from a thread without holding the lock during the
  read (the fetch task may be mid-`memset`)

---

## 4. Data flow on a single refresh tick

```
OpenSky fetch task (every 22s)
└── OpenSky_GetAircraftJson(...)
    ├── EnsureToken()                 // OAuth2 refresh if expired
    ├── HTTPS GET (mbedTLS inside platform_esp32.c)
    └── strncpy into response buffer
└── OpenSky_ParseAircraft(json)
    ├── memset gAircraft[0..N]
    ├── cJSON_Parse
    ├── for each state in array:
    │   └── gAircraft[i] = { icao24, callsign, lat, lon, ... }
    └── gAircraftCount = N
└── AppState_RecordApiUpdate()
    └── lastApiUpdateMs = platform_now_ms()
└── platform_lvgl_lock(0)
    ├── Radar_ReconcileSelection()
    │   ├── if selectedIcao24[0] == '\0' (first ever load)
    │   │   └── pick the aircraft closest to the radar centre
    │   ├── else if matching icao24 still present
    │   │   └── selectedAircraft = i
    │   └── else
    │       └── selection cleared (NEW behaviour — used to silently jump to 0)
    ├── UpdateSelectedAircraftUI()
    │   └── render right-rail info card for selectedAircraft
    └── Radar_Refresh()
        └── lv_obj_invalidate(radarObject)  // scheduled redraw
└── platform_lvgl_unlock()
└── AppState_HideWaitingBanner() (once, on first successful fetch)
```

Between fetches, `RadarPredictTask` runs at 4 Hz and integrates positions
forward by `velocity × dt` so the icons move smoothly between the 22s
updates. The `predictedLat/Lon` is what actually gets drawn (`AircraftToRadar`
uses `predictedX`, not the raw `X`). The raw `latitude/longitude` is
displayed in the right-rail info card.

---

## 5. The radar drawing loop

`radar.c` registers a single `LV_EVENT_DRAW_MAIN` callback on the radar
`lv_img` object. The callback runs on every invalidation (the 30 ms sweep
timer invalidates 33 times per second; the predict task invalidates 4 times
per second for trail points).

The draw order is **fixed** and matters:

```
1. Outer ring + 1/3-ring + 2/3-ring       (lv_draw_arc)
2. Range km labels along the 60° azimuth  (draw_compass_and_scale)
3. N / E / S / W compass labels           (draw_compass_and_scale)
4. N-S / W-E dashed crosshair lines       (draw_compass_and_scale)
5. Centre "+" cross (the "you are here" marker)
6. Trail polyline (if showSelectedTrail) (fading dashed white)
7. Sweep afterglow (12 radial lines, 0 to radius, fading with angle)
8. Aircraft icons (one per gAircraft[i])
9. Aircraft callsign labels (if showAircraftLabels)
```

The same `radarObject` is hit-tested for taps (see `Radar_TapSelect` in
`ui_events.c`). The radar area is 460×460 px, drawn from the centre of the
560×480 `ui_PanelRadar` parent.

### 5.1 Coordinate transforms

The radar uses an **equirectangular projection** (good enough for radii
≤ 100 km where the curvature is negligible):

```c
float eastKm  = (lon - radarCenterLon) * 111.0f * cosf(radarCenterLat * π/180);
float northKm = (lat - radarCenterLat) * 111.0f;
int x = (eastKm  / radarRadiusKm) * radiusPixels;
int y = -(northKm / radarRadiusKm) * radiusPixels;   // y is inverted because LVGL y grows downward
```

Aircraft icons are rendered in **three size classes** driven by the
OpenSky `category` field (states-array index 17), matching the
Flightradar24 convention — same plane silhouette in all three, just at
different pixel sizes:

| Class    | Pixel size | OpenSky categories |
|----------|------------|--------------------|
| SMALL    | 10 px      | 2 (Light), 8 (Rotorcraft), 9 (Glider), 10 (Lighter-than-air), 12 (Ultralight), 14 (UAV), 15 (Space) |
| MEDIUM   | 16 px      | 3 (Small), 4 (Large), 7 (High Performance), 11 (Parachutist), 16–20 (surface vehicles / obstacles), and unknown/missing (0, 1, 13) |
| LARGE    | 24 px      | 5 (High Vortex Large, e.g. B-757), 6 (Heavy > 300k lbs — wide-bodies like B777, A380, B747) |

The largest icon (24 px) is ~2.4× the smallest (10 px). Unknown / missing
category codes default to MEDIUM rather than SMALL so a payload without
category data still renders at a readable size rather than collapsing to
a tiny dot. Size is fixed across the selected / unselected states — only
the yellow ring distinguishes the selected aircraft, so the icon does
not appear to grow when tapped.

### 5.2 The trail

The trail is a 64-point ring buffer of `(predictedLat, predictedLon)` pairs,
written every 1 second when the selected aircraft is visible. It is drawn
as a fading dashed white polyline. The buffer is reset:

* when the selection changes (different `icao24`)
* when the user toggles the trail off (and again when they toggle on —
  the buffer is cleared on every state change to avoid inheriting stale
  positions)

The trail survives the 22s OpenSky refresh because `Radar_ReconcileSelection`
looks up the previously-selected `icao24` in the new `gAircraft[]` and
re-resolves `selectedAircraft`.

---

## 6. The selection model

`selectedAircraft` is an **index into `gAircraft[]`**, not a stable identifier.
The fetch task rebuilds the array on every tick, so the index is meaningless
between ticks. The stable identifier is `selectedIcao24` (a 6-char lowercase
hex string).

`Radar_ReconcileSelection()` is the bridge between the two:

```c
if (selectedIcao24[0] == '\0') {
    // First-ever load. Auto-pick the closest aircraft to the radar centre.
}
for (int i = 0; i < gAircraftCount; i++) {
    if (strcmp(gAircraft[i].icao24, selectedIcao24) == 0) {
        selectedAircraft = i;  // match found
        return;
    }
}
// No match → clear selection. Do NOT silently jump to aircraft 0.
selectedAircraft = -1;
selectedIcao24[0] = '\0';
```

This is what fixed the "after API refresh, selection jumps to another
aircraft" bug. The previous code did `if (selectedAircraft >= gAircraftCount)
selectedAircraft = 0;` which masked the case where the selected aircraft
disappeared between fetches.

---

## 7. The OpenSky client

`opensky_client.c` is the only file that knows how OpenSky works. It
implements OAuth2 password-grant client_credentials flow against
`https://auth.opensky-network.org/.../token`, caches the bearer token in
memory, and refreshes it 60 seconds before expiry (`tokenExpiry =
time(NULL) + expires_in - 60`).

The actual data fetch is the OpenSky "states" endpoint:

```
https://opensky-network.org/api/states/all?lamin=...&lamax=...&lomin=...&lomax=...
```

The bounding box is computed from `GetRadarLat`, `GetRadarLon`, `GetRadarRange`
in `app_state.c::ComputeBBox`. The aircraft categorisation field is **index
17** in the OpenSky `states` array — this is the field we use for icon size.

The JSON parser pulls the following fields:

| Index | Field                | Where it ends up                              |
|-------|----------------------|-----------------------------------------------|
| 0     | `icao24`             | `gAircraft[i].icao24`                         |
| 1     | `callsign`           | `gAircraft[i].callsign`                       |
| 2     | `origin_country`     | `gAircraft[i].originCountry`                  |
| 5     | `longitude`          | `gAircraft[i].longitude` (+ `predictedLon`)   |
| 6     | `latitude`           | `gAircraft[i].latitude` (+ `predictedLat`)    |
| 8     | `on_ground`          | `gAircraft[i].onGround`                       |
| 9     | `velocity` (m/s)     | `gAircraft[i].velocity`                       |
| 10    | `heading` (° from N) | `gAircraft[i].heading`                        |
| 11    | `vertical_rate` (m/s)| `gAircraft[i].verticalRate`                   |
| 13    | `baro_altitude` (m)  | `gAircraft[i].altitude` (+ `predictedAltitude`) |
| 17    | `category` (0-20)    | `gAircraft[i].category`                       |

The cJSON parser is vendored as part of `components/lvgl__lvgl/` for the
target — it does not need an extra component dependency.

### 7.1 HTTP and TLS

On ESP32, `platform_http_get` and `platform_http_post` use the IDF
`esp_http_client` with mbedTLS. The CA bundle is built into the firmware
via `esp_crt_bundle_attach()` — there is no certificate check at runtime.
On the SDL simulator, the same functions use `libcurl`.

---

## 8. The UI

The UI is generated by **SquareLine Studio 1.5.3** (LVGL 8.4) and lives in
`src/main/ui/screens/`. The three screens are:

| Screen       | Purpose                                                                |
|--------------|------------------------------------------------------------------------|
| `ui_Screen1` | Main radar screen. The radar hero panel + left rail (info cards) + right rail (info cards) + bottom status bar. |
| `ui_Screen2` | WiFi picker / softAP (used on first boot when no credentials).        |
| `ui_Screen3` | Settings (radar lat/lon, range, refresh interval, units).              |

`Screen1` is the only one loaded by default. The others are loaded via
`_ui_screen_change()` from event handlers.

### 8.1 Widget hierarchy (Screen1)

```
ui_Screen1 (LV_OBJ, 1024×600)
├── ui_PanelLeft (320 px wide, left rail)
│   ├── ui_ContainerCard1 (aircraft count card)
│   ├── ui_ContainerCard2 (API-age card)
│   ├── ui_ContainerCardUnits (imperial/metric switch)
│   └── ui_ContainerTrailRowMain (trail toggle on/off switch)
├── ui_PanelRadar (560 px wide, radar hero)
│   ├── ui_Imageradar (460×460, the actual radar canvas)
│   ├── ui_RadarTitle (top-left)
│   ├── ui_LabelClock (top-left, below title)
│   ├── ui_RadarLoc (bottom-left, "Loc: 0.0000, 0.0000")
│   ├── ui_LabelAPIRefreshBig (bottom-right, "API: 12s")
│   ├── ui_LabelWaitingBanner (centred, hidden after first fetch)
│   └── ui_ContainerLegend (top-right, 4 colour swatches)
├── ui_PanelRight (320 px wide, right rail)
│   ├── ui_ContainerCard4 (aircraft name)
│   ├── ui_ContainerCard5 (altitude)
│   ├── ui_ContainerCard6 (speed)
│   ├── ui_ContainerCard7 (origin)
│   ├── ui_ContainerCard9 (category)
│   ├── ui_ContainerCard10 (heading)
│   └── ui_ContainerCardClimb (vertical rate)
└── ui_PanelBottom (status bar, bottom)
    ├── uic_LabelWifiName
    ├── uic_LabelIPData
    └── uic_LabelClock
```

The `uic_` prefix denotes a "custom variable" — it's a pointer that is
re-set on every entry to `ui_Screen1_screen_init()` so the rest of the
code can refer to the widget by name without a global search.

### 8.2 The two LVGL timers

```c
// UI status bar updates (clock, IP, API age) every 500 ms
lv_timer_create(AppState_AircraftUiTimerCb, 500, NULL);

// Radar sweep — rotate the leading arm angle every 30 ms
lv_timer_create(AppState_SweepTimerCb, 30, NULL);
```

Both timers are created inside `platform_lvgl_lock()` so the create
happens on the LVGL thread once we have a screen.

---

## 9. State persistence (NVS)

The board uses the **NVS partition** (24 KB at `0x9000`) as a key-value
store. The key namespaces are:

| Namespace | Keys                                       |
|-----------|--------------------------------------------|
| `wifi`    | `ssid`, `pass`                             |
| `opensky` | `client_id`, `client_secret`               |
| `radar`   | `lat`, `lon`, `range`, `units`, `refresh`, `trail` |

The `app_state.c` `Load*` functions are called exactly once at boot, after
`ui_Screen1` is loaded. They read the saved values, set the in-memory
state, and synchronise the LVGL widgets to reflect the loaded values.

`Save*` functions are written by the event handlers in `ui_events.c` and
`ui_Screen3.c` whenever the user changes a value.

> **Critical**: the NVS partition is **preserved across `app-flash`**.
> If you ever run `idf.py flash` instead, the NVS is wiped and the user
> has to re-enter WiFi and OpenSky credentials.

---

## 10. The webserver (first-boot provisioning)

The HTTP webserver (in `webserver.c`) is needed because the board has no
keyboard on the first boot. Three routes:

| Route     | Method | Purpose                                                                 |
|-----------|--------|-------------------------------------------------------------------------|
| `/`       | GET    | Status page: SSID, IP, current config, time-since-last-fetch.         |
| `/upload` | POST   | Form submit of `client_id` and `client_secret`. Saved to NVS. Restart.|
| `/clear`  | GET    | Wipes the WiFi and OpenSky credentials. Forces re-provisioning.        |

The webserver runs only on the softAP (when not connected to a WiFi
network) OR on the STA interface (when connected). The first boot brings
up the softAP `FlightRadar-Setup` with password `flightradar` so the user
can connect a phone and visit `http://192.168.4.1/upload`.

---

## 11. The platform abstraction

`platform.h` is the only file with `#ifdef` branches between the ESP32
target and the SDL simulator. The portable API is:

```c
uint32_t platform_now_ms(void);              // monotonic ms
bool     platform_storage_get_u8(ns, key, *out);   // NVS read
bool     platform_storage_set_u8(ns, key, value); // NVS write
bool     platform_storage_get_str(ns, key, buf, len);
bool     platform_storage_set_str(ns, key, str);
size_t   platform_storage_get_blob(ns, key, buf, len);
size_t   platform_storage_set_blob(ns, key, buf, len);
bool     platform_lvgl_lock(timeout_ms);      // recursive mutex
void     platform_lvgl_unlock(void);
int      platform_http_get(url, headers, nheaders, body, len, *outlen);
int      platform_http_post(url, headers, nheaders, body, body_len, response, len, *outlen);
void     platform_restart(void);
void     platform_forget_wifi_creds(void);
void     platform_forget_opensky_creds(void);
void     platform_log(level, tag, fmt, ...);
```

This is the contract for any new platform. To port the firmware to a new
target (e.g. an ESP32-S3 with a different LCD, or a Raspberry Pi), provide
a `platform_<target>.c` that implements every function in `platform.h`.

---

## 12. Build system

The IDF build is standard for any ESP-IDF project:

```bash
idf.py set-target esp32s3       # do NOT run this again — it wipes sdkconfig
idf.py build                    # 5–10 min cold, ~20 s warm
idf.py -p <port> app-flash      # preserve NVS
idf.py -p <port> monitor        # serial monitor
```

The `partitions.csv` declares:

```
nvs,        data, nvs,     0x9000,   0x6000
phy_init,   data, phy,     0xf000,   0x1000
factory,    app,  factory, 0x10000,  0x600000
spiffs,     data, spiffs,  0x610000, 0x9F0000
```

The app image is at `0x10000` and is 1.5 MB. The spiffs partition is
reserved for future use (e.g. weather overlay tiles).

---

## 13. The simulator

`simulator/` is a parallel build that compiles the same portable files
against SDL2 and libcurl. It produces a desktop executable that runs
the same UI on a windowed canvas. The simulator is invaluable for:

* rapid iteration without flashing the board
* CI testing (no hardware required)
* screenshot capture for documentation

To build:

```bash
cd simulator
make
./flightradar
```

The simulator uses an in-memory `platform_sdl.c` rather than the ESP32
NVS — credentials are loaded from `../flightradar_sim_config.json`.

---

## 14. Known gotchas

1. **NVS is wiped by `idf.py flash`**. Always use `app-flash`.
2. **The 22-second refresh is not configurable**. The OpenSky client polls
   on a fixed interval determined by the firmware build. To change it,
   add a "refresh interval" config to NVS and read it in `main.c`.
3. **The trail stops at the radar rim**. The trail polyline is clipped to
   the radar disc; if the aircraft leaves the radar area, the trail tail
   stops at the rim and resumes when the aircraft comes back.
4. **The selection is cleared if the selected aircraft disappears**. This
   is intentional — it prevents the user from looking at info for an
   aircraft they can't see.
5. **OpenSky public rate limit is 400 req/day anonymous, 4000/day
   authenticated**. The 22-second poll is 3900/day, so the board must
   always have OAuth2 credentials.
6. **The Elecrow panel needs 16 MB flash mode**. The `sdkconfig` defaults
   include `FLASH_MODE_DIO`, `FLASH_FREQ_80M`, `FLASH_SIZE_16MB`. If you
   ever reset the config, run `idf.py menuconfig` and check
   *Serial flasher config*.

---

## 15. Where to start reading

If you want to understand the radar:

1. `radar.c::radar_draw_cb` — the entire pipeline in one function.
2. `radar.c::DrawAircraft` — how a single icon is rendered.
3. `radar.c::Radar_ReconcileSelection` — the selection model.

If you want to understand the data pipeline:

1. `opensky_client.c::OpenSky_GetAircraftJson` — the HTTP fetch.
2. `opensky_client.c::OpenSky_ParseAircraft` — the JSON-to-struct mapping.
3. `main.c::radar_update_timer_cb` — the fetch task.

If you want to understand the UI:

1. `ui/screens/ui_Screen1.c::ui_Screen1_screen_init` — the widget tree.
2. `ui/ui_events.c::toggleTrail` — a typical event handler.
3. `app_state.c::UpdateSelectedAircraftUI` — the right-rail renderer.

If you want to understand the persistence:

1. `app_state.c::LoadTrail` — how NVS is read at boot.
2. `ui/ui_events.c::toggleTrail` — how NVS is written on a user action.
