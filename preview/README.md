# Icon preview

A self-contained HTML page that renders the radar aircraft glyphs at the
exact pixel sizes the firmware draws them. Use it to compare the three
size classes (small / medium / large) and the helicopter symbol without
having to flash the ESP32.

## Open it directly (no server)

The page has no external dependencies, so you can just open it in a
browser:

```bash
open preview/icons.html      # macOS
xdg-open preview/icons.html  # Linux
start preview/icons.html    # Windows
```

## Or serve it locally

```bash
python3 preview/serve.py
# then open http://127.0.0.1:8080/icons.html
```

Useful when:
- you want to view it from a phone on the same WiFi (`--host 0.0.0.0`)
- you want to share the URL with someone on your network
- you want cache-disabled reloads while iterating

Stop the server with `Ctrl-C`.

## What the page shows

1. **Side-by-side comparison** — all four glyphs (small, medium, large,
   helicopter) at 1:1 with their pixel size, plus a faint crosshair
   through the centre. Toggle "Unselected" / "Selected (+4 px)" in the
   header to see how each icon grows when selected (the selection ring
   follows).
2. **In context — radar dial slice** — the four glyphs placed on a
   representative 460×460 radar canvas (scaled to fit the page) with
   the dashed inner range rings drawn the same way `radar.c` draws them.
3. **Heading rotation** — a single medium aircraft at 0°, 45°, 90° …
   315° to confirm the icon rotates around its centre.

## Geometry is pixel-true

The endpoint math in the page's `<script>` is a direct translation of
the `PX`/`PY` macros in `src/main/radar.c::DrawAircraft`:

```c
#define PX(R, F) (x + (int)((R) * ch + (F) * sh))
#define PY(R, F) (y - (int)((F) * ch - (R) * sh))
```

```js
const PX = (R, F) => cx + (R * ch + F * sh);
const PY = (R, F) => cy - (F * ch - R * sh);
```

The size table and selection-ring radii are pulled from the same constants
(`AIRCRAFT_SIZES = { SMALL: 8, MEDIUM: 12, LARGE: 18 }`,
`SELECTED_BUMP = 4`, `HELI_RADIUS = 13`).

So if you change a size in `radar.c`, edit the matching constant near
the top of the `<script>` block and reload.