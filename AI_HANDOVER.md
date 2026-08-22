# AI Handover Prompt — Flight Tracker ESP32-S3

> Copy the block below into a fresh Claude session to bring it fully up to
> speed on this project. It's a self-contained briefing — no need to read
> the codebase first.

---

## PROJECT BRIEF

You are working on a **live aircraft tracker** running on an **Elecrow 7"
ESP32-S3 HMI display** (800×480 RGB LCD, GT911 capacitive touch, ESP32-S3-
WROOM-1, 16 MB flash, 8 MB PSRAM, CH340 USB-UART bridge). The firmware
pulls real-time ADS-B state vectors from the **OpenSky Network** OAuth2
API every 22 seconds, plots them on a green radar dial, and lets the
user tap any aircraft to see its callsign, altitude, speed, heading,
climb rate, and origin country.

Project name: **Flight Tracker** (formerly "Flight Radar" — renamed
2026-08-21). GitHub: https://github.com/LordWhiteley-UK/flight-radar-esp32.git

## WORKING DIRECTORY

```
/Users/jps/Downloads/Flight-radar-7-main/
├── DEVELOPER.md       ← READ FIRST. Single source of truth for what the
│                        app is, how it's put together, build/flash,
│                        LVGL 8.4 pitfalls, amendment recipes.
├── README.md          ← short user-facing overview
├── ARCHITECTURE.md    ← deep design doc (tasks, locks, data flow)
├── FLASHING_GUIDE.md  ← erase + flash walkthrough
├── TOOLCHAIN.md       ← ESP-IDF + CH340 driver install
├── AI_HANDOVER.md     ← this file
├── src/
│   ├── main/          ← portable C, LVGL port, webserver
│   │   ├── radar.c    ← MOST EDITS GO HERE. radar_draw_cb +
│   │   │                DrawAircraft (icon shape, sizes, climb colour,
│   │   │                selection ring) + range rings + sweep.
│   │   ├── app_state.c
│   │   ├── opensky_client.c
│   │   ├── main.c     ← app_main, WiFi, OpenSky fetch task, refresh
│   │   │                interval (default 22 s)
│   │   ├── webserver.c
│   │   └── ui/screens/ui_Screen1.c  ← main radar screen layout
│   └── components/    ← vendored LVGL + GT911 touch
└── preview/           ← local side-by-side icon-size preview (HTML)
```

## BUILD / FLASH (macOS, this user's env)

```bash
cd /Users/jps/Downloads/Flight-radar-7-main/src
export IDF_PATH="/Users/jps/esp/esp-idf"
export PATH="/Users/jps/.espressif/python_env/idf5.3_py3.10_env/bin:$PATH"
source $IDF_PATH/export.sh
idf.py build
idf.py -p /dev/cu.wchusbserial1340 app-flash     # ALWAYS app-flash, never flash
```

**`app-flash` preserves the NVS partition** (WiFi creds, OpenSky
client_id/secret, user prefs). Plain `flash` wipes it. Monitor with
`idf.py -p /dev/cu.wchusbserial1340 monitor` (Ctrl-] to exit).

## CURRENT ICON SIZE SCHEME (anchored on medium = 12 px)

Three size classes, each +50% of the previous. Rescaled 2026-08-21 at the
user's request (small baseline, medium +50%, large +50% more):

| Class    | Unselected | Selected (+4) | OpenSky categories |
|----------|-----------|---------------|--------------------|
| SMALL    | **8 px**  | 12            | Light, Glider, UAV, etc. |
| MEDIUM   | **12 px** | 16            | Small, Large, High Performance, default |
| LARGE    | **18 px** | 22            | High Vortex, Heavy wide-body |

Source: `src/main/radar.c::DrawAircraft` lines ~511-519 (size switch) and
~550-559 (selection-ring radii 16/20/26).

## DRAWING GEOMETRY (radar.c::DrawAircraft)

Aircraft glyph is **three thin lines** (fuselage + wings + tail bar) drawn
with `lv_draw_line`. Reverted to this shape 2026-08-19 after several
attempts at sprites / chevrons / filled triangles did not match the
user's reference screenshot.

Math (copied verbatim into the preview page):

```c
float h = (float)a->heading * 0.0174532925f;
float ch = cosf(h), sh = sinf(h);
#define PX(R, F) (x + (int)((R) * ch + (F) * sh))
#define PY(R, F) (y - (int)((F) * ch - (R) * sh))
/* nose:    PX(0,        sf*0.8),   PY(0,        sf*0.8)
   tail:    PX(0,       -sf*0.8),   PY(0,       -sf*0.8)
   lwing:   PX(-sf*0.7,  0),        PY(-sf*0.7,  0)
   rwing:   PX( sf*0.7,  0),        PY( sf*0.7,  0)
   tleft:   PX(-sf*0.30,-sf*0.8),   PY(-sf*0.30,-sf*0.8)
   tright:  PX( sf*0.30,-sf*0.8),   PY( sf*0.30,-sf*0.8)
```

OpenSky heading is 0° = north, clockwise positive (matches `true_track`).

**Helicopter** (cls == AC_HELI) is drawn separately as a circle of
radius 13 with an "H" label centred on it (font `lv_font_montserrat_16`).

## RANGE RINGS (radar.c::radar_draw_cb)

**4 rings** at 25% / 50% / 75% / 100% of the radar radius.

- Outer ring: solid `lv_draw_arc(... 0, 360)`.
- Inner three rings: **dashed via 36 short arc segments** at 6° on / 4° off.

Why? LVGL 8.4's `lv_draw_arc_dsc_t` has no `dash_width` / `dash_gap`.
Many short segments is the workaround. Range labels along the 60°
azimuth are at 25/50/75/100% of the radius.

## KNOWN LVGL 8.4 PITFALLS

1. **No arc dashing.** See "range rings" workaround above.
2. **No rect rotation.** `lv_draw_rect_dsc_t` has no `.angle` /
   `.pivot`. Only `lv_draw_img_dsc_t` supports rotation. For rotated
   rectangles, draw as `lv_draw_line` segments with sin/cos, like the
   three-line aircraft glyph does.
3. **No native polygon API.** Emit `lv_draw_line` segments in a loop.
4. **`LV_COLOR_DEPTH=16` + `LV_IMG_CF_TRUE_COLOR_ALPHA`** = 3 bytes/pixel
   (RGB565 + alpha byte). When generating LVGL image assets by hand,
   honour this stride.
5. **`platform_lvgl_lock` is recursive.** Safe to call `Radar_Refresh`
   from inside another LVGL timer callback.

## VISIBLE USER STRINGS

All four visible "Flight Radar" labels were renamed to "Flight Tracker"
on 2026-08-21:

- `src/main/ui/screens/ui_Screen1.c:320` — radar title
- `src/main/ui/screens/ui_Screen2.c:192` — splash title
- `src/main/ui/screens/ui_Screen3.c:135` — settings title
- `src/simulator/sdl_display.c:71` — simulator window title

Internal header guards (`_FLIGHT_RADAR_UI_H` → `_FLIGHT_TRACKER_UI_H`)
also renamed. SquareLine's auto-generated `// Project name: Flight_radar`
comments were intentionally left as-is (overwritten on next SquareLine
export). GitHub repo name was NOT renamed (would break push URL).

## PREVIEW PAGE (icon-size preview)

A standalone HTML page at `preview/icons.html` plus a stdlib Python server
at `preview/serve.py`. Started in this session, runs at
http://127.0.0.1:8080/icons.html (server task ID `bj7pid5n6`,
background). Restart with `python3 preview/serve.py`.

The preview renders each icon at its real pixel size with the same
PX/PY math as `radar.c`, plus stroke widths scaled as `max(1, size/8)`
to keep the visual ratio honest. Use it to compare sizes without
flashing the board.

## STATE PERSISTENCE (NVS)

| Namespace | Keys                                       |
|-----------|--------------------------------------------|
| `wifi`    | `ssid`, `pass`                             |
| `opensky` | `client_id`, `client_secret`               |
| `radar`   | `lat`, `lon`, `range`, `units`, `refresh`, `trail` |

`app-flash` preserves NVS; `flash` or `erase-flash` wipes it.

## FIRST-BOOT PROVISIONING

SoftAP `FlightRadar-Setup` / password `flightradar` at
`http://192.168.4.1/upload`. Routes: `/` (status), `/upload` (paste
OpenSky client_id/secret), `/clear` (wipe NVS). OAuth2 token auto-
refreshed 60 s before expiry.

## RECENT COMMIT HISTORY

```
69fb925  Preview: revert side-by-side to fixed 80x80 cells, keep stroke scaling
58e40bc  Preview: render icons at real pixel size, scale stroke with icon
e1281a7  Add preview/icons.html: local side-by-side glyph size preview
4146503  Rename to Flight Tracker; rescale aircraft sizes 8/12/18 (+50% chain)
0e89251  Add DEVELOPER.md: goal, implementation, flashing, and pitfalls
37d65de  Remove unused Plane Icons SVG folder and sprite .c files
34f37b9  Radar: 3-size aircraft glyph, larger helicopter, 4 dashed range rings
550d747  Add docs/screenshot.png
2b1660a  Initial commit: flight radar firmware + docs
```

## COMMON TASKS — QUICK REFERENCE

**Change aircraft glyph shape:** edit `DrawAircraft` in `radar.c`. Keep
`color = ClimbColor(a)` so climb state (green/red/grey) is preserved.
Then `app-flash`.

**Change helicopter symbol:** the `if (cls == AC_HELI)` branch in
`DrawAircraft`. Circle radius is `heliR=13`; H label box is `x±9, y±11`
with `lv_font_montserrat_16`. Bump the selection ring radius too if you
make the symbol bigger.

**Change range rings:** `radar_draw_cb` in `radar.c`. Outer is solid;
inner three are dashed via 36 short segments at 6°/4°.

**Add/change a right-rail card:** edit `ui_Screen1.c`. Card heights:
36 px for one-line data, **40 px for the climb card** (tuned by hand to
avoid title collision). Data label y-offset: `-2` inside the card.

**Refresh interval:** `main.c::radar_update_timer_cb` — change the 22 s.
OpenSky auth limit is 4000 req/day; 22 s = ~3900/day.

**Always end with:** `idf.py build && idf.py -p /dev/cu.wchusbserial1340 app-flash`

## TROUBLESHOOTING

| Symptom | Fix |
|---------|-----|
| Board won't enumerate | Hold BOOT, tap RST, release BOOT. Check `/dev/cu.wchusbserial*`. |
| `Brownout detector was triggered` | Powered USB hub or wall adapter. 7" RGB LCD draws a lot. |
| NVS asks for credentials after flash | You ran `flash` instead of `app-flash`. |
| OpenSky returns 401 | Visit `/clear` then `/upload` on the softAP. |
| OpenSky returns 429 | Polling too fast or no OAuth2 credentials. |
| Stale aircraft | `RadarPredictTask` dead — check `app_state.c`. |

## IF YOU ARE LOST

1. Read `/Users/jps/Downloads/Flight-radar-7-main/DEVELOPER.md` — it
   tells you exactly where to look for any change.
2. The preview at http://127.0.0.1:8080/icons.html lets you compare icon
   sizes without flashing the board. Run
   `python3 preview/serve.py` if it's not already up.
3. When in doubt about LVGL, the project uses LVGL 8.4 — remember the
   pitfalls in §"KNOWN LVGL 8.4 PITFALLS" above.