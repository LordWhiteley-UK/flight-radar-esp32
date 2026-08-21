# DEVELOPER.md — Flight Tracker ESP32-S3

> **You are a future Claude session that has just been fired up to make
> amendments to this firmware. Read this file first. It is the single source
> of truth for what the app is, how it is put together, and how to build,
> flash, and modify it.** The companion files `README.md`, `ARCHITECTURE.md`,
> `FLASHING_GUIDE.md`, and `TOOLCHAIN.md` cover subsets of the same material
> from different angles — if you need the historical narrative or
> per-platform install steps, read those; if you need to **change something
> right now**, this file tells you what to touch.

---

## 1. What this app is

A live aircraft radar that runs on an **Elecrow 7" ESP32-S3 HMI display**
(1024×600 RGB LCD, GT911 capacitive touch, ESP32-S3-WROOM-1, 16 MB flash,
8 MB PSRAM, CH340 USB-UART bridge). It pulls real-time ADS-B state vectors
from the **OpenSky Network** OAuth2 API every 22 seconds, plots them on a
green radar dial with a sweeping arm, and lets you tap any aircraft to see
its callsign, origin country, altitude, speed, heading, and climb rate.

The display panel is **800×480 px** in this build (down-scaled from the
1024×600 native panel via the LVGL porting layer). The visible layout:

```
+---------------------------+----------------------------+
|  Left rail (320 px wide)  |  Radar (560 wide)          |
|  - aircraft count         |  - 4 range rings           |
|  - API-age                |  - N/E/S/W compass         |
|  - units switch           |  - 12-line sweep afterglow |
|  - trail toggle           |  - aircraft glyphs         |
|                           |  - tap-to-select ring      |
+---------------------------+----------------------------+
```

This document is current as of 2026-08-20. The most recent design changes
that you might not see in older docs are:

1. **Aircraft glyph restored to the original three-line shape** (fuselage +
   wings + tail bar) drawn as plain `lv_draw_line` segments, with **three
   visual size classes** (8 / 12 / 18 px, each +50% of the previous) — see §6.4.
2. **Helicopter symbol** is a circle with an "H" inside it, bigger than the
   original (radius 13, font 16) — see §6.5.
3. **Range rings**: **4 rings** at 25% / 50% / 75% / 100% of the radar
   radius. The outer ring is solid; the three inner rings are **dashed**
   drawn as 36 short arc segments per ring (LVGL 8.4 has no arc dash
   attribute) — see §6.3.

---

## 2. Repo layout

```
Flight-radar-7-main/
├── DEVELOPER.md            ← THIS FILE (start here)
├── README.md               ← short user-facing overview
├── ARCHITECTURE.md         ← full design doc (tasks, locks, data flow)
├── FLASHING_GUIDE.md       ← step-by-step erase+flash walkthrough
├── TOOLCHAIN.md            ← IDF install + CH340 driver for Mac/Win
├── LICENSE
├── docs/
│   └── screenshot.png      ← reference screenshot
└── src/
    ├── main/                         ← portable C + LVGL port + UI
    │   ├── main.c                    ← app_main, WiFi, OpenSky fetch task
    │   ├── main.h
    │   ├── app_state.c               ← shared state, LVGL timers, NVS
    │   ├── radar.c                   ← RADAR DRAWING (most edits go here)
    │   ├── radar.h
    │   ├── opensky_client.c          ← OAuth2 + JSON parser
    │   ├── opensky_client.h
    │   ├── webserver.c               ← first-boot HTTP captive portal
    │   ├── webserver.h
    │   ├── platform.h                ← portable API
    │   ├── platform_esp32.c          ← ESP32 implementation
    │   ├── plane_icon.c              ← unused (dead code)
    │   ├── plane_icon.h
    │   ├── bm8563_min.c              ← RTC driver
    │   ├── bm8563_min.h
    │   ├── lvgl_port.c               ← LVGL ↔ RGB LCD bridge
    │   ├── lvgl_port.h
    │   ├── waveshare_rgb_lcd_port.c  ← LCD init (untouched)
    │   ├── waveshare_rgb_lcd_port.h
    │   ├── ui/                       ← SquareLine Studio 1.5.3 UI
    │   │   ├── ui.h, ui.c
    │   │   ├── ui_events.c           ← toggleTrail, Radar_TapSelect
    │   │   ├── ui_helpers.c
    │   │   └── screens/
    │   │       ├── ui_Screen1.c      ← main radar screen (most edits)
    │   │       ├── ui_Screen1.h
    │   │       ├── ui_Screen2.c      ← WiFi picker
    │   │       └── ui_Screen3.c      ← settings (coords, units, refresh)
    │   ├── CMakeLists.txt
    │   ├── idf_component.yml
    │   └── Kconfig.projbuild
    ├── components/                   ← vendored LVGL + GT911 touch
    │   ├── espressif__esp_lcd_touch/
    │   ├── espressif__esp_lcd_touch_gt911/
    │   └── lvgl__lvgl/
    ├── simulator/                    ← SDL2 desktop build (optional)
    ├── partitions.csv                ← nvs / phy / factory / spiffs
    ├── sdkconfig                     ← generated; do not edit by hand
    └── sdkconfig.defaults            ← board defaults
```

`simulator/` is a parallel build against SDL2 + libcurl that runs the same
portable code. Useful for desktop iteration. Out of scope for routine
amendments.

---

## 3. The two-minute tour

If you need to find something specific, this is the file map:

| You want to change                          | File you edit                            |
|---------------------------------------------|------------------------------------------|
| Aircraft icon shape/size/colour             | `src/main/radar.c` (DrawAircraft)        |
| Helicopter symbol                           | `src/main/radar.c` (DrawAircraft, cls==AC_HELI branch) |
| Range rings / sweep / dial                  | `src/main/radar.c` (radar_draw_cb, draw_compass_and_scale) |
| Right-rail info card labels / spacing       | `src/main/ui/screens/ui_Screen1.c`       |
| Selected-aircraft card content              | `src/main/app_state.c` (UpdateSelectedAircraftUI) |
| Refresh interval (default 22 s)             | `src/main/main.c` (radar_update_timer_cb) |
| Default radar range / lat / lon             | `src/main/main.c` and `app_state.c` defaults |
| WiFi / OpenSky first-boot flow              | `src/main/webserver.c`                   |
| OpenSky JSON parsing / field mapping        | `src/main/opensky_client.c`              |
| New SquareLine widget / screen              | regenerate via SquareLine, then re-add the source files in `src/main/ui/CMakeLists.txt` and `filelist.txt` |
| Build/flash defaults                        | `src/sdkconfig.defaults` and `src/partitions.csv` |

---

## 4. Flashing routines

This is the canonical build/flash sequence for the user's development
environment (macOS, ESP-IDF v5.3.x, board at `/dev/cu.wchusbserial1340`).
For full install + driver steps, see `TOOLCHAIN.md`.

### 4.1 Activate the toolchain

```bash
export IDF_PATH="/Users/jps/esp/esp-idf"
export PATH="/Users/jps/.espressif/python_env/idf5.3_py3.10_env/bin:$PATH"
source $IDF_PATH/export.sh
```

> Always run `source $IDF_PATH/export.sh` in every new shell — `$IDF_PATH`,
> `idf.py`, and the IDF Python venv live behind that script. Without it,
> `idf.py: command not found`.

### 4.2 Build

```bash
cd /Users/jps/Downloads/Flight-radar-7-main/src
idf.py build
```

Cold build (first time): 5–10 min. Warm build (only `radar.c` changed):
~20 s.

The output binary is `build/lvgl_porting.bin` (≈ 1.5 MB). The bootloader,
partition table, and the app image are merged into it by the build.

### 4.3 Flash — preserve NVS

```bash
idf.py -p /dev/cu.wchusbserial1340 app-flash
```

**Always `app-flash`, never `flash`.** The plain `flash` command overwrites
the NVS partition that stores WiFi and OpenSky OAuth2 credentials — the
user has to re-provision after every flash. `app-flash` only writes the
app image at `0x10000`.

If `idf.py -p /dev/cu.wchusbserial1340 app-flash` ever fails to enumerate,
find the port with `ls /dev/cu.wchusbserial*` and substitute. On Windows
it's `idf.py -p COM<n> app-flash`.

### 4.4 Erase everything (factory reset)

```bash
idf.py -p /dev/cu.wchusbserial1340 erase-flash
idf.py -p /dev/cu.wchusbserial1340 flash      # full re-flash, wipes NVS
```

Use this when NVS has become corrupted, the user changed their OpenSky
password, or you're shipping a unit to someone else.

### 4.5 Watch the boot log

```bash
idf.py -p /dev/cu.wchusbserial1340 monitor
# Ctrl-] to exit; the monitor auto-resets the board on exit.
```

Useful when something doesn't appear on screen — the boot log shows WiFi
state, OAuth2 token fetch, OpenSky HTTP responses, and any LVGL assert.

### 4.6 Combined commands

```bash
# Build + flash + monitor in one go
idf.py -p /dev/cu.wchusbserial1340 app-flash monitor

# Erase + flash + monitor (factory reset + first run)
idf.py -p /dev/cu.wchusbserial1340 erase-flash flash monitor
```

### 4.7 Git remote

```bash
git remote -v
# origin  https://github.com/LordWhiteley-UK/flight-radar-esp32.git (fetch)
# origin  https://github.com/LordWhiteley-UK/flight-radar-esp32.git (push)
```

Push a change:

```bash
git add -A
git commit -m "Describe what changed and why"
git push origin main
```

---

## 5. Architectural mental model

A one-screen picture (full version in `ARCHITECTURE.md`):

```
+---------------------------+        +---------------------------+
|  HTTP webserver (task)    |        |  OpenSky fetch (task)     |
|  Port 80, softAP + STA    |        | 22 s polling w/ OAuth2    |
|  Stores creds in NVS      |        | Parses JSON → gAircraft[] |
+-------------+-------------+        +-------------+-------------+
              |                                    |
              v                                    v
        +------+----------------------------------+------+
        |                  NVS                          |
        |  WiFi creds, OpenSky OAuth2 creds,            |
        |  user prefs (units, trail, range, refresh)    |
        +------+-----------------------------------+----+
                       |                            |
                       v                            v
        +--------------+-------+        +----------+----------+
        | Radar draw (LVGL cb) |        | Dead-reckon (task)  |
        | 30 ms sweep timer    |        | 4 Hz integrate pos  |
        | Aircraft icons       |        | Updates predicted*  |
        +----------------------+        +---------------------+
```

### 5.1 Shared state

A single mutable struct in `app_state.c`:

```c
Aircraft gAircraft[MAX_AIRCRAFT];  // 200 × ~80 bytes ≈ 16 KB
int gAircraftCount;
char selectedIcao24[16];
int selectedAircraft;
bool showAircraftLabels;
bool showSelectedTrail;
```

`gAircraft[]` is **rebuilt from scratch** on every 22 s OpenSky fetch —
never edited in-place. The fetch task therefore does not need to lock
against LVGL. The lock is only held across the
`Radar_ReconcileSelection + UpdateSelectedAircraftUI + Radar_Refresh`
triple. See `radar.c` and `app_state.c` for the actual `platform_lvgl_lock`
calls.

### 5.2 What cannot happen

You cannot:

* Touch LVGL widgets from `RadarPredictTask` without taking
  `platform_lvgl_lock(0)` first.
* Call `Radar_Refresh()` from a non-LVGL thread.
* Read `gAircraft[]` from a thread without holding the lock during the
  read (the fetch task may be mid-`memset`).

### 5.3 LVGL timers

```c
lv_timer_create(AppState_SweepTimerCb,  30, NULL);   // radar sweep
lv_timer_create(AppState_AircraftUiTimerCb, 500, NULL); // status labels
```

Both are created inside `platform_lvgl_lock()` so creation happens on the
LVGL thread once we have a screen.

---

## 6. The radar drawing pipeline

`radar.c` registers one `LV_EVENT_DRAW_MAIN` callback on the radar
`lv_img`. The callback runs on every invalidation (the 30 ms sweep timer
invalidates ~33×/s; the predict task invalidates 4×/s for trail points).

### 6.1 Draw order (in `radar_draw_cb`)

```
1. Outer ring + 3 inner rings                (lv_draw_arc — outer solid, inner dashed)
2. Range km labels along the 60° azimuth     (draw_compass_and_scale)
3. N / E / S / W compass labels              (draw_compass_and_scale)
4. N-S / W-E dashed crosshair lines          (draw_compass_and_scale)
5. Centre "+" cross (the "you are here" marker)
6. Trail polyline (if showSelectedTrail)     (fading dashed white)
7. Sweep afterglow (12 radial lines, fading)
8. Aircraft glyphs (one per gAircraft[i])     (DrawAircraft)
9. Aircraft callsign labels (if showAircraftLabels)
```

### 6.2 Coordinate transform

Equirectangular projection (good enough for radii ≤ 100 km):

```c
float eastKm  = (lon - radarCenterLon) * 111.0f * cosf(radarCenterLat * π/180);
float northKm = (lat - radarCenterLat) * 111.0f;
int x = (eastKm  / radarRadiusKm) * radiusPixels;
int y = -(northKm / radarRadiusKm) * radiusPixels;   // y is inverted
```

OpenSky heading is 0° = north, clockwise positive. The aircraft glyph
uses `sin/cos` of the heading angle with screen-x = sin, screen-y = -cos.

### 6.3 Range rings — **4 rings, inner three dashed**

```c
/* Outer ring: solid. */
lv_draw_arc(draw_ctx, &arc, &center, radius, 0, 360);

/* Inner rings: dashed. 36 short segments of 6° with 4° gaps. */
const int DASH_COUNT = 36;
const int DASH_DEG   = 6;
const int GAP_DEG    = 4;
const int STEP_DEG   = DASH_DEG + GAP_DEG;
const int inner_radii[3] = { radius * 3 / 4, radius * 2 / 4, radius * 1 / 4 };
for (int ri = 0; ri < 3; ri++) {
    int r = inner_radii[ri];
    for (int k = 0; k < DASH_COUNT; k++) {
        int start = k * STEP_DEG;
        int end   = start + DASH_DEG;
        lv_draw_arc(draw_ctx, &arc, &center, r, start, end);
    }
}
```

Why? LVGL 8.4's `lv_draw_arc_dsc_t` does **not** expose `dash_width` or
`dash_gap`. Drawing many short arc segments is the workaround. Keep this
in mind if you ever need to change the ring style.

Range labels along the 60° azimuth are at 25% / 50% / 75% / 100% of the
radar radius.

### 6.4 Aircraft glyph — three-line silhouette, three sizes

`DrawAircraft` draws the **original three-line glyph** (fuselage + wings +
tail bar). The user explicitly reverted to this shape after several
attempts at filled sprites / chevrons / triangles.

```c
/* Class → glyph size. Each step is +50% of the previous (8 → 12 → 18).
 * Selected aircraft get an extra +4 px bump so the selection ring has
 * a clear gap. */
int size;
switch (cls) {
case AC_SMALL:  size = selected ? 12 : 8;  break;   /* light  — baseline */
case AC_MEDIUM: size = selected ? 16 : 12; break;   /* narrow — +50% */
case AC_LARGE:  size = selected ? 22 : 18; break;   /* wide   — +50% */
default:        size = selected ? 16 : 12; break;
}

float h  = (float)a->heading * 0.0174532925f;
float ch = cosf(h), sh = sinf(h);
#define PX(R, F) (x + (int)((R) * ch + (F) * sh))
#define PY(R, F) (y - (int)((F) * ch - (R) * sh))
float sf = (float)size;
lv_point_t nose    = { PX(0,  sf * 0.8f),            PY(0,  sf * 0.8f) };
lv_point_t tail    = { PX(0, -sf * 0.8f),            PY(0, -sf * 0.8f) };
lv_point_t lwing   = { PX(-sf * 0.7f, 0),            PY(-sf * 0.7f, 0) };
lv_point_t rwing   = { PX( sf * 0.7f, 0),            PY( sf * 0.7f, 0) };
lv_point_t tleft   = { PX(-sf * 0.30f, -sf * 0.8f),  PY(-sf * 0.30f, -sf * 0.8f) };
lv_point_t tright  = { PX( sf * 0.30f, -sf * 0.8f),  PY( sf * 0.30f, -sf * 0.8f) };
#undef PX
#undef PY

lv_draw_line_dsc_init(&ln);
ln.color = color;   // climb state — green / red / grey
ln.width = 2;
ln.opa   = LV_OPA_COVER;
lv_draw_line(draw_ctx, &ln, &nose,  &tail);
lv_draw_line(draw_ctx, &ln, &lwing, &rwing);
lv_draw_line(draw_ctx, &ln, &tleft, &tright);
```

The `color` is the climb colour: green climbing, red descending, grey
level (see `ClimbColor` in `radar.c`).

### 6.5 Helicopter symbol — **H in a circle, bigger**

The helicopter branch in `DrawAircraft` (when `cls == AC_HELI`) draws:

* An `lv_draw_arc` circle of radius 13 px in white.
* A label "H" centred at the same point, font `lv_font_montserrat_16`,
  white, drawn into a small bounding box (`x-9..x+9`, `y-11..y+11`).

The class is detected in `ClassifyAircraft` by callsign prefix (`PNTHR`,
`HLE`, `G-POL`, `IRGC`, etc.) **or** by OpenSky category 8.

### 6.6 Selection ring

Yellow ring around the selected aircraft. Radii by class:

| Class    | Ring radius |
|----------|-------------|
| AC_HELI  | 20          |
| AC_SMALL | 16          |
| AC_MEDIUM| 20          |
| AC_LARGE | 26          |

These clear the glyph by ~4 px. Bump them up if you make the glyph
bigger.

### 6.7 Trail (selected aircraft)

A 64-point ring buffer of `(predictedLat, predictedLon)` pairs, written
every 1 second. Drawn as a fading dashed white polyline. Reset on
selection change or trail toggle. See `draw_trail` in `radar.c`.

---

## 7. Right-rail card layout

`src/main/ui/screens/ui_Screen1.c` — `ui_PanelRight` holds 7 cards in a
column flex layout. **All data labels live at `y = -2` inside cards whose
heights were tuned to fit a 16-px font below the title.** The climb card
in particular must be height **40** (not 36) so the value doesn't touch
the title:

```c
ui_ContainerCardClimb = lv_obj_create(ui_PanelRight);
lv_obj_set_height(ui_ContainerCardClimb, 40);
...
lv_obj_set_y(ui_LabelCraftClimb, -2);
```

If you add a new card, copy the height + label-y pattern of an existing
sibling. Don't shrink cards to "make it fit" — the user is sensitive to
title/data collisions.

---

## 8. The OpenSky client

`src/main/opensky_client.c` — OAuth2 client_credentials against
`https://auth.opensky-network.org/.../token`, then a GET on
`https://opensky-network.org/api/states/all?lamin=...&lamax=...&lomin=...&lomax=...`
on a 22-second cadence. Bearer token is cached in memory and refreshed
60 s before expiry.

Field mapping (states-array indices):

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
| 17    | `category` (0–20)    | `gAircraft[i].category`                       |

OpenSky's anonymous limit is 400 req/day; authenticated is 4000/day. The
22 s poll is ~3900/day, so the board **must** have OAuth2 credentials or
it will be rate-limited within hours.

---

## 9. State persistence (NVS)

| Namespace | Keys                                       |
|-----------|--------------------------------------------|
| `wifi`    | `ssid`, `pass`                             |
| `opensky` | `client_id`, `client_secret`               |
| `radar`   | `lat`, `lon`, `range`, `units`, `refresh`, `trail` |

The `app_state.c` `Load*` functions run once at boot, after `ui_Screen1`
loads. They read NVS, set the in-memory state, and synchronise the LVGL
widgets.

The `Save*` functions are called by event handlers in `ui_events.c` and
`ui_Screen3.c` whenever the user changes a value.

> **NVS survives `app-flash`.** It only gets wiped by `erase-flash` or
> `flash`. Always use `app-flash` unless you actually want to wipe it.

---

## 10. First-boot provisioning

`src/main/webserver.c` — the board has no keyboard on first boot, so the
firmware starts an HTTP server. Three routes:

| Route     | Method | Purpose                                                                 |
|-----------|--------|-------------------------------------------------------------------------|
| `/`       | GET    | Status page: SSID, IP, current config, time-since-last-fetch.         |
| `/upload` | POST   | Form submit of `client_id` and `client_secret`. Saved to NVS. Restart. |
| `/clear`  | GET    | Wipes the WiFi and OpenSky credentials. Forces re-provisioning.        |

First boot brings up a softAP `FlightRadar-Setup` (password `flightradar`)
at `http://192.168.4.1/upload`.

---

## 11. Known LVGL 8.4 pitfalls (and how we worked around them)

These are things that will bite you if you don't know:

1. **No arc dashing.** `lv_draw_arc_dsc_t` has no `dash_width` /
   `dash_gap`. To draw a dashed ring, emit many short arc segments (see
   §6.3).
2. **No rect rotation.** `lv_draw_rect_dsc_t` has no `.angle` or
   `.pivot`. Only `lv_draw_img_dsc_t` supports rotation. If you need a
   rotated rectangle, draw it as a polygon or compute rotated line
   endpoints manually with `sinf`/`cosf`.
3. **No native polygon API.** To draw arbitrary polygons, emit
   `lv_draw_line` segments in a loop.
4. **`LV_COLOR_DEPTH=16` + `LV_IMG_CF_TRUE_COLOR_ALPHA`** = 3 bytes per
   pixel (RGB565 + alpha byte). When generating LVGL image assets by
   hand, this stride assumption must be honoured or you'll get
   `convert_cb` warnings and garbled output.
5. **`platform_lvgl_lock` is recursive** — it is safe to call
   `Radar_Refresh` from inside an LVGL timer callback.

---

## 12. Workflow for a typical amendment

A future session is most likely to be asked for one of these. The recipe
for each:

### "Change the aircraft glyph shape"

* Edit `DrawAircraft` in `src/main/radar.c`.
* Keep `color = ClimbColor(a)` so climb state is preserved.
* If you rotate something other than `lv_draw_line`, recall the LVGL 8.4
  rotation rules in §11.
* `idf.py build && idf.py -p /dev/cu.wchusbserial1340 app-flash`
* Watch the board — verify the new shape on multiple size classes.

### "Change the helicopter symbol"

* Edit the `if (cls == AC_HELI)` branch in `DrawAircraft`
  (`src/main/radar.c`).
* The circle radius is `heliR`; the H label bounding box is
  `x ± 9, y ± 11` with `lv_font_montserrat_16`.
* Bump the selection ring radius too (currently 20) if you make the
  symbol bigger.

### "Add / change a range ring"

* Edit `radar_draw_cb` in `src/main/radar.c`.
* The outer ring is `lv_draw_arc(... 0, 360)`.
* Inner rings are dashed via 36 short segments at 6° on / 4° off.
* Range labels live in `draw_compass_and_scale`.

### "Add / change a right-rail card"

* Edit `src/main/ui/screens/ui_Screen1.c`.
* Card heights: 36 px for one-line data, 40 px for the climb card (the
  climb card was tuned by hand to avoid title collision).
* Data label y-offset: `-2` inside the card.
* Mirror the data in `UpdateSelectedAircraftUI` in `app_state.c`.
* If the value is user-editable, add an NVS key in `app_state.c` and a
  Save handler in `ui_events.c`.

### "Change the refresh interval"

* `src/main/main.c` `radar_update_timer_cb` — change the 22 s.
* OpenSky's auth limit is 4000 req/day; at 22 s you're at 3900. Going
  faster than 15 s without a paid API tier will get you 429'd.

### "Change the default radar centre / range"

* Defaults live in `main.c` and are overridden by NVS if the user has
  saved a value. To force a new default, also clear the NVS key on the
  next boot, or just instruct the user to set the value via Screen3.

### "Add a new field from OpenSky"

* Add the field to the `Aircraft` struct in `radar.h`.
* Extend the cJSON parser in `OpenSky_ParseAircraft`
  (`opensky_client.c`).
* If the field should appear on the right rail, extend
  `UpdateSelectedAircraftUI` in `app_state.c`.
* If the field should change the glyph, extend `DrawAircraft` /
  `ClimbColor` / `ClassifyAircraft` in `radar.c`.

---

## 13. Verification checklist before you commit

1. **Build clean**: `idf.py build` exits with no warnings or errors.
2. **Flash clean**: `idf.py -p /dev/cu.wchusbserial1340 app-flash`
   reports `Hash of data verified` and `Leaving`.
3. **Boot log clean**: `idf.py -p /dev/cu.wchusbserial1340 monitor`
   shows WiFi connect → OAuth2 token fetch → first OpenSky 200 response
   within ~10 s.
4. **Visible UI clean**:
   * Radar dial shows 4 rings (outer solid, 3 inner dashed).
   * Aircraft glyphs are the three-line silhouette (not sprites, not
     triangles, not filled circles).
   * Helicopters are H-in-circle.
   * Selection ring is yellow, sized correctly per class.
   * Right-rail cards: title and data do not touch.
5. **NVS preserved**: After the flash, the board should reconnect to
   WiFi automatically and resume pulling OpenSky data without a
   re-provision prompt. If it asks for credentials again, you ran
   `flash` instead of `app-flash`.

---

## 14. Where to look when something goes wrong

| Symptom                                        | Look first                                  |
|------------------------------------------------|---------------------------------------------|
| Board doesn't enumerate                        | `ls /dev/cu.wchusbserial*` (macOS), Device Manager (Win), or hold BOOT+RST to force download mode |
| `Brownout detector was triggered`              | USB cable isn't powering; try a wall adapter |
| LCD is dark / garbage                          | `sdkconfig` flash settings reverted; rerun `idf.py set-target esp32s3` and check `FLASH_MODE_DIO`, `FLASH_FREQ_80M`, `FLASH_SIZE_16MB` |
| Touch doesn't work                             | GT911 I²C; check menuconfig didn't disable I²C |
| OpenSky returns 401                            | Bad credentials; visit `http://<board-ip>/clear` then `/upload` to re-enter |
| OpenSky returns 429 (rate-limited)             | Polling too fast or no OAuth2 credentials    |
| Aircraft icons missing                         | `gAircraftCount == 0` — check the OpenSky fetch in monitor; bbox may be too tight or centre is empty |
| Selection jumps to wrong aircraft              | `Radar_ReconcileSelection` regressed — make sure it does NOT silently fall back to `selectedAircraft = 0` when the chosen `icao24` is absent |
| `Guru Meditation Error` in `Radar_DrawAircraft` | Null pointer because `gAircraftCount == 0` and the loop body dereferences the index; guard the loop |
| Stale radar (aircraft frozen)                  | `RadarPredictTask` not running or `predictedLat/Lon` not being updated — check `app_state.c` |
| WiFi keeps disconnecting                       | 2.4 GHz only — ESP32-S3 does not support 5 GHz |

---

## 15. Style and conventions used in this codebase

* C99, four-space indent, Allman braces.
* `static` for everything that doesn't escape the file.
* LVGL calls always inside `platform_lvgl_lock`/`platform_lvgl_unlock`.
* Comments at the top of each file explain *why*, not *what*.
* Constants are named in `UPPER_SNAKE`, functions in `PascalCase`,
  locals in `camelCase` or `snake_case` (mixed — match the surrounding
  file).
* No `printf` for diagnostics in production — use `ESP_LOGI` /
  `ESP_LOGW` / `ESP_LOGE` with a tag.

---

## 16. Quick-reference: one command to build + flash + monitor

```bash
cd /Users/jps/Downloads/Flight-radar-7-main/src && \
  export IDF_PATH="/Users/jps/esp/esp-idf" && \
  export PATH="/Users/jps/.espressif/python_env/idf5.3_py3.10_env/bin:$PATH" && \
  source $IDF_PATH/export.sh && \
  idf.py build && \
  idf.py -p /dev/cu.wchusbserial1340 app-flash monitor
```

Press `Ctrl-]` to exit the monitor. The board reboots automatically on
exit.

That's the whole game. Read `ARCHITECTURE.md` if you want deeper
background; read this file when you're about to change something.
