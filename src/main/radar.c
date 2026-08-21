#include "radar.h"
#include "main.h"
#include "platform.h"
#include "ui.h"   /* LV_IMG_DECLARE(ui_img_aircraft_*_png) */

#include <math.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *radarObject = NULL;

static float radarCenterLat = 12.9716f;
static float radarCenterLon = 77.5946f;
static float radarRadiusKm = 50.0f;

static float sweepAngle = 0.0f;

bool showAircraftLabels = true;

/* Selected-aircraft trail: one ring buffer for whichever aircraft is
   selected, so it survives the 22s OpenSky refresh (which rebuilds the
   gAircraft array) and resets only when the selection changes. */
bool showSelectedTrail = false;

#define TRAIL_MAX 64
static float trailLat[TRAIL_MAX];
static float trailLon[TRAIL_MAX];
static int    trailHead  = 0;   /* next write index */
static int    trailCount = 0;
static uint32_t trailLastMs = 0;
static char   trailIcao[16] = "";

char selectedIcao24[16] = "";

int selectedAircraft = -1;

void Radar_ClearTrail(void)
{
    trailHead  = 0;
    trailCount = 0;
    trailLastMs = 0;
    trailIcao[0] = '\0';
}

void Radar_SetTrail(bool on)
{
    showSelectedTrail = on;
    /* Always start with an empty trail so the toggle has a clear visual
       effect: switching ON does not inherit stale positions from a previous
       selection, and switching OFF clears the buffer immediately. */
    Radar_ClearTrail();
}

void Radar_ReconcileSelection(void)
{
    if (gAircraftCount <= 0)
    {
        selectedAircraft = -1;
        selectedIcao24[0] = '\0';
        return;
    }

    // First aircraft ever loaded (or selection just cleared). Auto-pick the
    // aircraft closest to the radar centre so the user has something to look
    // at immediately on startup.
    if (selectedIcao24[0] == '\0')
    {
        int best = 0;
        float bestD2 = 1.0e30f;
        for (int i = 0; i < gAircraftCount; i++)
        {
            if (!gAircraft[i].valid) continue;
            float dLat = gAircraft[i].predictedLat - radarCenterLat;
            /* crude equirectangular — fine for ranking, accurate at small
               radar radii. */
            float dLon = (gAircraft[i].predictedLon - radarCenterLon) *
                         cosf(radarCenterLat * 3.14159265f / 180.0f);
            float d2 = dLat * dLat + dLon * dLon;
            if (d2 < bestD2) { bestD2 = d2; best = i; }
        }

        selectedAircraft = best;

        strncpy(
            selectedIcao24,
            gAircraft[best].icao24,
            sizeof(selectedIcao24) - 1);
        selectedIcao24[sizeof(selectedIcao24) - 1] = '\0';

        return;
    }

    // Try to find previously selected aircraft (exact string match).
    // ICAO hex is 6 chars and OpenSky always returns lowercase, but use
    // strncmp against the stored length to avoid any trailing-NUL hazards
    // if upstream parsing ever returns an unpadded buffer.
    size_t wantLen = strnlen(selectedIcao24, sizeof(selectedIcao24));
    for (int i = 0; i < gAircraftCount; i++)
    {
        const char *got = gAircraft[i].icao24;
        size_t gotLen = strnlen(got, sizeof(gAircraft[i].icao24));
        if (gotLen == wantLen && memcmp(got, selectedIcao24, wantLen) == 0)
        {
            selectedAircraft = i;
            return;
        }
    }

    // Previously selected aircraft disappeared (out of radar range, or the
    // OpenSky response omitted it this cycle). Leave selection cleared so
    // the next radar tap or refresh makes a fresh choice; do NOT silently
    // jump to aircraft 0.
    selectedAircraft = -1;
    selectedIcao24[0] = '\0';
}

Aircraft *Radar_GetSelectedAircraft(void)
{
    if (selectedAircraft < 0 ||
        selectedAircraft >= gAircraftCount)
    {
        return NULL;
    }

    return &gAircraft[selectedAircraft];
}

void Radar_PredictAircraft(void)
{
    for (int i = 0;
         i < gAircraftCount;
         i++)
    {
        Aircraft *a =
            &gAircraft[i];

        if (!a->valid)
            continue;

        uint32_t now =
            platform_now_ms();

        float dt =
            (now - a->lastUpdateMs) /
            1000.0f;

        a->lastUpdateMs = now;

        /* Integrate vertical rate (m/s) into predicted altitude so the
           displayed altitude climbs/descends smoothly between OpenSky
           fetches, matching the climb indicator. Reset to the fetched
           altitude on each parse. */
        if (!a->onGround)
        {
            a->predictedAltitude +=
                a->verticalRate * dt;
        }

        float speedKmh =
            a->velocity * 3.6f;

        float distanceKm =
            speedKmh * dt / 3600.0f;

        float headingRad =
            a->heading *
            0.0174532925f;

        float northKm =
            cosf(headingRad) *
            distanceKm;

        float eastKm =
            sinf(headingRad) *
            distanceKm;

        a->predictedLat +=
            northKm / 111.0f;

        float lonScale =
            111.0f *
            cosf(
                a->predictedLat *
                0.0174532925f);

        if (lonScale > 0.001f)
        {
            a->predictedLon +=
                eastKm / lonScale;
        }
    }

    /* Record a trail point for the selected aircraft (throttled to ~1s so the
       64-point buffer covers ~1 minute of flight). Reset when the selection
       changes so the trail always belongs to the currently-selected craft. */
    if (showSelectedTrail && selectedAircraft >= 0 &&
        selectedAircraft < gAircraftCount)
    {
        Aircraft *sel = &gAircraft[selectedAircraft];

        if (strcmp(trailIcao, sel->icao24) != 0)
        {
            strncpy(trailIcao, sel->icao24, sizeof(trailIcao) - 1);
            trailIcao[sizeof(trailIcao) - 1] = '\0';
            trailHead  = 0;
            trailCount = 0;
        }

        uint32_t now = platform_now_ms();
        if (trailLastMs == 0 || (now - trailLastMs) >= 1000)
        {
            trailLat[trailHead] = sel->predictedLat;
            trailLon[trailHead] = sel->predictedLon;
            trailHead = (trailHead + 1) % TRAIL_MAX;
            if (trailCount < TRAIL_MAX) trailCount++;
            trailLastMs = now;
        }
    }
}

void Radar_SweepTick(void)
{
    sweepAngle -= 3.0f;

    if (sweepAngle < 0.0f)
    {
        sweepAngle += 360.0f;
    }

    if (radarObject)
    {
        lv_obj_invalidate(
            radarObject);
    }
}

static bool LatLonToRadar(
    float lat,
    float lon,
    int radiusPixels,
    int *x,
    int *y,
    float *outDistKm)
{
    float dx = lon - radarCenterLon;
    float dy = lat  - radarCenterLat;

    float kmPerDegLat = 111.0f;
    float kmPerDegLon = 111.0f *
        cosf(radarCenterLat * 3.14159265f / 180.0f);

    float eastKm  = dx * kmPerDegLon;
    float northKm = dy * kmPerDegLat;

    float distance = sqrtf(eastKm * eastKm + northKm * northKm);

    if (outDistKm) *outDistKm = distance;

    if (distance > radarRadiusKm)
        return false;

    *x = (int)((eastKm  / radarRadiusKm) * radiusPixels);
    *y = (int)((-northKm / radarRadiusKm) * radiusPixels);

    return true;
}

static bool AircraftToRadar(
    Aircraft *a,
    int radiusPixels,
    int *x,
    int *y)
{
    return LatLonToRadar(a->predictedLat, a->predictedLon,
                         radiusPixels, x, y, NULL);
}

/* Three size classes with three different glyph shapes (tiny plane,
   medium plane silhouette, big plane silhouette). The class is driven
   EXCLUSIVELY by the OpenSky `category` field when it carries real
   data. When OpenSky returns no category (which is the case for the
   vast majority of real traffic), the aircraft is MEDIUM.

   We deliberately do NOT fall back to barometric altitude to classify
   aircraft, because altitude is unreliable as a size proxy:
     - Narrow-bodies (B737, A320) and wide-bodies (B777, A380) cruise
       at overlapping altitudes (FL300-410). A B737 at FL350 cannot be
       told apart from a B777 at FL350 by altitude alone.
     - Regional jets and turboprops cruise far lower than airliners
       (3,000-7,000 m vs 9,000-12,000 m), so an altitude-based scheme
       would mis-classify a regional jet at FL100 as "light aircraft".
     - The OpenSky barometric altitude field is itself sometimes null,
       which would mean a real airliner suddenly drops to a tiny icon.

   Two identical aircraft types with the same OpenSky data will now
   always classify the same way. Aircraft without category data default
   to MEDIUM so they remain visible rather than collapsing to a dot.

   OpenSky category reference (states-array index 17):
     0  No information at all
     1  No ADS-B Emitter Category Information
     2  Light (< 15500 lbs)                           -> SMALL
     3  Small (15500 to 75000 lbs)                    -> MEDIUM
     4  Large (75000 to 300000 lbs)                   -> MEDIUM
     5  High Vortex Large (e.g. B-757)                -> LARGE
     6  Heavy (> 300000 lbs) — wide-bodies           -> LARGE
     7  High Performance (> 5g, > 400 kts)            -> MEDIUM
     8  Rotorcraft                                    -> SMALL
     9  Glider / sailplane                            -> SMALL
    10  Lighter-than-air                              -> SMALL
    11  Parachutist / Skydiver                        -> SMALL
    12  Ultralight / hang-glider / paraglider         -> SMALL
    13  Reserved                                      -> MEDIUM
    14  Unmanned Aerial Vehicle                       -> SMALL
    15  Space / Trans-atmospheric vehicle             -> SMALL
    16-20  Surface vehicles + obstacles               -> MEDIUM */
typedef enum
{
    AC_SMALL  = 0,   /* tiny plane silhouette — light aircraft, glider, UAV, etc. */
    AC_HELI   = 1,   /* helicopter silhouette — OpenSky category 8 only */
    AC_MEDIUM = 2,   /* standard plane silhouette — narrow-body jets */
    AC_LARGE  = 3,   /* big plane silhouette — wide-body jets */
} AircraftClass;

/* Callsign-prefix lookup. OpenSky rarely populates the category field,
   so for many aircraft we have no way to know if they're a helicopter
   or a wide-body unless we recognise the callsign. This table is a
   small subset of well-known operators whose aircraft type is
   essentially fixed by their callsign prefix.

   The prefix is matched case-insensitively against the first N
   characters of `callsign`. Trimmed callsigns are used because OpenSky
   sometimes pads with trailing spaces.

   Entries are deliberately conservative — only operators whose fleet
   is overwhelmingly one type get a fixed mapping. Mixed-fleet
   operators (e.g. Jet Airways, which flew both narrow-bodies and
   wide-bodies) are intentionally omitted. */
static int CallsignClassOrUnset(const Aircraft *a)
{
    const char *cs = a->callsign;

    /* Helicopter operators. Pawan Hans (PN / PNTHR) is overwhelmingly
       rotorcraft in Indian airspace. UK Police, Irish Coast Guard,
       and German ADAC are all helicopter fleets. */
    if (strncmp(cs, "PNTHR", 5) == 0) return AC_HELI;
    if (strncmp(cs, "PN",    2) == 0) return AC_HELI;
    if (strncmp(cs, "HLE",   3) == 0) return AC_HELI;   /* e.g. HLE01 */
    if (strncmp(cs, "G-POL", 5) == 0) return AC_HELI;   /* UK police */
    if (strncmp(cs, "IRGC",  4) == 0) return AC_HELI;   /* Irish coast guard */

    /* Wide-body operators. Emirates, Singapore, Cathay, Qatar, Etihad,
       Qantas, Lufthansa's LH long-haul, ANA, JAL — essentially always
       operate wide-body aircraft. */
    if (strncmp(cs, "UAE", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "SIA", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "CPA", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "QTR", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "ETD", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "QFA", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "ANA", 3) == 0) return AC_LARGE;
    if (strncmp(cs, "JAL", 3) == 0) return AC_LARGE;

    return -1;
}

static AircraftClass ClassifyAircraft(const Aircraft *a)
{
    /* 1. Callsign-prefix lookup first — most reliable for known
          operators (helicopter fleets, wide-body airlines). */
    int cs = CallsignClassOrUnset(a);
    if (cs >= 0)
    {
        return (AircraftClass)cs;
    }

    /* 2. OpenSky category, when populated. */
    if (!a->categoryKnown)
    {
        return AC_MEDIUM;
    }

    switch (a->category)
    {
    case 8:   /* Rotorcraft — gets its own helicopter silhouette so it's
                  clearly identifiable instead of looking like a tiny
                  plane. */
        return AC_HELI;

    case 2:   /* Light */
    case 9:   /* Glider */
    case 10:  /* Lighter-than-air */
    case 11:  /* Parachutist */
    case 12:  /* Ultralight */
    case 14:  /* UAV */
    case 15:  /* Space / trans-atmospheric */
        return AC_SMALL;

    case 5:   /* High Vortex Large (B-757) */
    case 6:   /* Heavy (B777, A380, B747) */
        return AC_LARGE;

    case 3:   /* Small */
    case 4:   /* Large (75k-300k lbs — narrow-body jets) */
    case 7:   /* High Performance */
    case 13:  /* Reserved */
    case 16:  /* Surface vehicle — emergency */
    case 17:  /* Surface vehicle — service */
    case 18:  /* Point obstacle */
    case 19:  /* Cluster obstacle */
    case 20:  /* Line obstacle */
        return AC_MEDIUM;

    /* OpenSky "no information" codes (0, 1) — treat as MEDIUM so an
       aircraft without category data stays visible. */
    case 0:
    case 1:
    default:
        return AC_MEDIUM;
    }
}

static lv_color_t ClimbColor(
    const Aircraft *a)
{
    if (a->onGround)
        return lv_palette_main(LV_PALETTE_GREY);

    if (a->verticalRate > 0.5f)
        return lv_palette_main(LV_PALETTE_GREEN);

    if (a->verticalRate < -0.5f)
        return lv_palette_main(LV_PALETTE_RED);

    /* level flight — use the radar's own dim green so the selected aircraft
       reads as a "highlighted dim green" rather than a stark white line
       that can be mistaken for a trail. */
    return lv_palette_main(LV_PALETTE_GREEN);
}

/* Rotate (px, py) by `heading_deg` (0 = up / north, clockwise positive)
   around the centre point (cx, cy), in screen space. Uses LVGL's
   trigonometry helpers so the math matches what the rest of LVGL does.
   The result is stored in `out_x` / `out_y`. */

/* Draw a top-down plane glyph at (cx, cy), rotated by `heading_deg`.
   Composed of three filled rectangles in `dsc`:
     - wings: horizontal bar centred on the aircraft, span = 2*wing_half_span
     - fuselage: vertical bar from -fuselage_length/2 to +fuselage_length/2
     - tail: small horizontal bar at the rear (back of the fuselage)
   All rectangles are drawn in their rotated positions so the whole
   aircraft points in the direction of `heading_deg`. */


static void DrawAircraft(
    lv_draw_ctx_t *draw_ctx,
    int x,
    int y,
    const Aircraft *a,
    bool selected)
{
    /* Three size classes — the simple coloured arrow used before the
       SVG-sprite iteration. A filled triangle (chevron) plus a small
       fuselage rectangle, rotated by heading. The colour comes from
       ClimbColor so climb state is shown by the arrow colour directly
       — no separate dot needed. The selected ring and the helicopter
       H-in-circle stay as before. */
    AircraftClass cls = ClassifyAircraft(a);
    lv_color_t color = ClimbColor(a);

    if (cls == AC_HELI)
    {
        /* Helicopter: H-in-circle (FAA/ICAO convention). Larger than
           the original (heliR 11) so it reads at the same visual
           weight as a medium-class aircraft — about 25% bigger than
           the light-aircraft icon. The "H" letter and font scale to
           match. */
        int heliR = 13;

        lv_draw_arc_dsc_t heli_ring;
        lv_draw_arc_dsc_init(&heli_ring);
        heli_ring.color = color;
        heli_ring.width = 2;
        lv_point_t heli_center = {.x = x, .y = y};
        lv_draw_arc(draw_ctx, &heli_ring, &heli_center, heliR, 0, 360);

        lv_draw_label_dsc_t hd;
        lv_draw_label_dsc_init(&hd);
        hd.color = color;
        hd.font  = &lv_font_montserrat_16;
        hd.align = LV_TEXT_ALIGN_CENTER;

        lv_area_t h_area = {
            .x1 = x - 9, .y1 = y - 11,
            .x2 = x + 9, .y2 = y + 11};
        lv_draw_label(draw_ctx, &hd, &h_area, "H", NULL);
    }
    else
    {
        /* Aircraft icon — top-down plane glyph. Three thin lines:
             - fuselage (along the heading axis)
             - wings (perpendicular through the centre)
             - tail bar (perpendicular at the tail end)

           Three size classes. Each step is +50% of the previous:
             - small  (light / UAV)            = baseline 8 px
             - medium (B737 / A320 narrow)     = +50% = 12 px
             - large  (wide-body / heavy)      = +50% = 18 px
           Selected aircraft get an extra +4 bump so the ring has
           a clear gap. */

        int size;
        switch (cls)
        {
        case AC_SMALL:  size = selected ? 12 : 8;  break;   /* light  — baseline */
        case AC_MEDIUM: size = selected ? 16 : 12; break;   /* narrow — +50% */
        case AC_LARGE:  size = selected ? 22 : 18; break;   /* wide-body — +50% more */
        default:        size = selected ? 16 : 12; break;
        }

        float h = (float)a->heading * 0.0174532925f;
        float ch = cosf(h);
        float sh = sinf(h);

        /* heading 0° = north. screen-x = sh, screen-y = -ch. */
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

        lv_draw_line_dsc_t ln;
        lv_draw_line_dsc_init(&ln);
        ln.color = color;
        ln.width = 2;
        ln.opa   = LV_OPA_COVER;

        lv_draw_line(draw_ctx, &ln, &nose,  &tail);
        lv_draw_line(draw_ctx, &ln, &lwing, &rwing);
        lv_draw_line(draw_ctx, &ln, &tleft, &tright);
    }

    if (selected)
    {
        int ringRadius;
        switch (cls)
        {
        case AC_HELI:   ringRadius = 20; break;
        case AC_SMALL:  ringRadius = 16; break;   /* clears size=8 → 12 */
        case AC_MEDIUM: ringRadius = 20; break;   /* clears size=12 → 16 */
        case AC_LARGE:  ringRadius = 26; break;   /* clears size=18 → 22 */
        default:        ringRadius = 20; break;
        }

        lv_draw_arc_dsc_t ring;
        lv_draw_arc_dsc_init(&ring);
        ring.color = lv_palette_main(LV_PALETTE_YELLOW);
        ring.width = 2;

        lv_point_t center = {.x = x, .y = y};
        lv_draw_arc(draw_ctx, &ring, &center, ringRadius, 0, 360);
    }
}

/* Draw a short text label centered on (px,py) in the given color/font. */
static void draw_centered_label(
    lv_draw_ctx_t *draw_ctx,
    int px, int py,
    const char *text,
    lv_color_t color,
    const lv_font_t *font)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = color;
    d.font  = font;
    d.align = LV_TEXT_ALIGN_CENTER;

    lv_area_t a = {
        .x1 = px - 60, .y1 = py - 10,
        .x2 = px + 60, .y2 = py + 10};
    lv_draw_label(draw_ctx, &d, &a, text, NULL);
}

/* Draw the 3 range-ring distance labels and the N/E/S/W compass marks. */
static void draw_compass_and_scale(
    lv_draw_ctx_t *draw_ctx,
    int cx, int cy, int radius)
{
    lv_color_t dim  = lv_palette_main(LV_PALETTE_GREEN);
    lv_color_t bold = lv_color_hex(0x7CFFB0);
    /* Range-distance labels (the "17 / 33 / 50 km" text along the 60°
       azimuth) are drawn in white rather than green so they're easier
       to read against the dim-green dial background. Crosshair lines
       and the centre cross keep `dim` green so they read as grid, not
       text. */
    lv_color_t range_label = lv_color_hex(0xFFFFFF);
    const lv_font_t *f_small = &lv_font_montserrat_12;
    const lv_font_t *f_compass = &lv_font_montserrat_14;

    /* ring distance labels along the 60° azimuth (upper-right), one per ring.
       Four rings at quarter-radius spacing (matches the dashed inner
       rings + solid outer). */
    const float fracs[] = { 0.25f, 0.50f, 0.75f, 1.0f };
    char buf[16];
    for (int k = 0; k < 4; k++)
    {
        int r = (int)(radius * fracs[k]);
        float rad = 60.0f * 0.0174532925f;   /* 0° = north, clockwise */
        int px = cx + (int)(sinf(rad) * r);
        int py = cy - (int)(cosf(rad) * r);
        int km = (int)(radarRadiusKm * fracs[k] + 0.5f);
        snprintf(buf, sizeof(buf), "%d", km);
        draw_centered_label(draw_ctx, px, py, buf, range_label, f_small);
    }

    /* N/E/S/W inside the outer ring (0/90/180/270°) so they never get
       clipped by the panel edge on a 460×460 radar image — 12px outside
       the rim pushed "S" off the bottom. */
    struct { const char *t; float deg; int dy; } card[4] = {
        {"N",   0.0f, -2}, {"E",  90.0f,  0},
        {"S", 180.0f,  2}, {"W", 270.0f,  0},
    };
    int edge = radius - 12;
    for (int k = 0; k < 4; k++)
    {
        float rad = card[k].deg * 0.0174532925f;
        int px = cx + (int)(sinf(rad) * edge);
        int py = cy - (int)(cosf(rad) * edge) + card[k].dy;
        draw_centered_label(draw_ctx, px, py, card[k].t, bold, f_compass);
    }

    /* N-S / W-E dashed crosshair lines. Stop 8px short of the rim so the
       dashed segments don't crash into the N/E/S/W labels, and start 6px
       out from the centre cross so they don't overlap the "you are here"
       cross. */
    lv_draw_line_dsc_t dash;
    lv_draw_line_dsc_init(&dash);
    dash.color = dim;
    dash.width = 1;
    dash.opa   = 150;
    dash.dash_width = 4;
    dash.dash_gap   = 4;

    int inner = 6;
    int outer = radius - 8;

    /* N-S (vertical) */
    lv_point_t ns1 = {.x = cx, .y = cy - outer};
    lv_point_t ns2 = {.x = cx, .y = cy - inner};
    lv_draw_line(draw_ctx, &dash, &ns1, &ns2);

    lv_point_t ns3 = {.x = cx, .y = cy + inner};
    lv_point_t ns4 = {.x = cx, .y = cy + outer};
    lv_draw_line(draw_ctx, &dash, &ns3, &ns4);

    /* W-E (horizontal) */
    lv_point_t we1 = {.x = cx - outer, .y = cy};
    lv_point_t we2 = {.x = cx - inner, .y = cy};
    lv_draw_line(draw_ctx, &dash, &we1, &we2);

    lv_point_t we3 = {.x = cx + inner, .y = cy};
    lv_point_t we4 = {.x = cx + outer, .y = cy};
    lv_draw_line(draw_ctx, &dash, &we3, &we4);

    /* "you are here" cross at the centre */
    lv_draw_line_dsc_t cross;
    lv_draw_line_dsc_init(&cross);
    cross.color = dim;
    cross.width = 1;
    lv_point_t h1 = {.x = cx - 5, .y = cy};
    lv_point_t h2 = {.x = cx + 5, .y = cy};
    lv_draw_line(draw_ctx, &cross, &h1, &h2);
    lv_point_t v1 = {.x = cx, .y = cy - 5};
    lv_point_t v2 = {.x = cx, .y = cy + 5};
    lv_draw_line(draw_ctx, &cross, &v1, &v2);
}

static void radar_draw_cb(
    lv_event_t *e)
{
    lv_obj_t *obj =
        lv_event_get_target(e);

    lv_draw_ctx_t *draw_ctx =
        lv_event_get_draw_ctx(e);

    lv_area_t area;

    lv_obj_get_content_coords(
        obj,
        &area);

    int width =
        area.x2 - area.x1;

    int height =
        area.y2 - area.y1;

    int cx =
        area.x1 + width / 2;

    int cy =
        area.y1 + height / 2;

    int radius =
        LV_MIN(
            width,
            height) /
        2;

    /* Range rings — four circles at quarter-radius spacing. The
       outer ring is solid (it's the boundary of the radar); the
       three inner rings are dashed so they read as range guides
       rather than competing with the outer boundary for attention.
       Dashed arcs are approximated by drawing many short arc
       segments around the circle (lv_draw_arc_dsc_t in this LVGL
       has no dash support). */

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_palette_main(LV_PALETTE_GREEN);
    arc.width = 2;

    lv_point_t center = { .x = cx, .y = cy };

    /* Outer ring: solid. */
    lv_draw_arc(draw_ctx, &arc, &center, radius, 0, 360);

    /* Inner rings: dashed. Each ring is drawn as 36 short segments
       of 6° with 4° gaps (total 360° = 10° per dash group, 36 groups). */
    const int DASH_COUNT = 36;
    const int DASH_DEG   = 6;   /* on */
    const int GAP_DEG    = 4;   /* off */
    const int STEP_DEG   = DASH_DEG + GAP_DEG;  /* 10° per group */
    const int inner_radii[3] = {
        radius * 3 / 4,
        radius * 2 / 4,
        radius * 1 / 4,
    };
    for (int ri = 0; ri < 3; ri++) {
        int r = inner_radii[ri];
        for (int k = 0; k < DASH_COUNT; k++) {
            int start = k * STEP_DEG;
            int end   = start + DASH_DEG;
            lv_draw_arc(draw_ctx, &arc, &center, r, start, end);
        }
    }

    /* range-ring distance labels + N/E/S/W compass + centre cross */
    draw_compass_and_scale(draw_ctx, cx, cy, radius);

    /* selected-aircraft trail: fading dashed polyline of recent positions.
       Dashed so it reads as a trail, not as the aircraft's own body line. */
    if (showSelectedTrail && trailCount >= 2)
    {
        lv_draw_line_dsc_t tl;
        lv_draw_line_dsc_init(&tl);
        tl.width = 2;
        tl.dash_width = 5;
        tl.dash_gap   = 3;

        for (int k = 1; k < trailCount; k++)
        {
            /* oldest entry is at (trailHead - trailCount) mod TRAIL_MAX */
            int ia = (trailHead - trailCount + (k - 1) + TRAIL_MAX) % TRAIL_MAX;
            int ib = (trailHead - trailCount + k       + TRAIL_MAX) % TRAIL_MAX;

            int ax, ay, bx, by;
            if (!LatLonToRadar(trailLat[ia], trailLon[ia], radius, &ax, &ay, NULL) ||
                !LatLonToRadar(trailLat[ib], trailLon[ib], radius, &bx, &by, NULL))
                continue;

            /* fade from dim (oldest) to bright (newest) */
            int opa = 60 + (195 * k / trailCount);
            if (opa > 255) opa = 255;
            tl.opa   = (lv_opa_t)opa;
            tl.color = lv_color_hex(0xFFFFFF);   /* white */

            lv_point_t pa = {.x = cx + ax, .y = cy + ay};
            lv_point_t pb = {.x = cx + bx, .y = cy + by};
            lv_draw_line(draw_ctx, &tl, &pa, &pb);
        }
    }

    lv_draw_line_dsc_t line;

    lv_draw_line_dsc_init(
        &line);

    line.color =
        lv_palette_main(
            LV_PALETTE_GREEN);

    line.width = 2;

    for (int i = 0; i < 12; i++)
    {
        float angle =
            sweepAngle -
            (i * 4);

        while (angle < 0)
        {
            angle += 360;
        }

        float rad =
            angle *
            0.0174532925f;

        /* Sweep afterglow: radial lines from the centre outwards, fading
           with angular distance from the leading sweep arm. r1 starts at
           0 so the sweep crosses the entire dial (centre -> rim), not
           just the outer ring. */
        int r1 = 0;
        int r2 = radius;

        int x1 =
            cx +
            (int)(cosf(rad) * r1);

        int y1 =
            cy -
            (int)(sinf(rad) * r1);

        int x2 =
            cx +
            (int)(cosf(rad) * r2);

        int y2 =
            cy -
            (int)(sinf(rad) * r2);

        lv_draw_line_dsc_t d;

        lv_draw_line_dsc_init(
            &d);

        d.color =
            lv_palette_main(
                LV_PALETTE_GREEN);

        d.width = 1;

        d.opa =
            255 -
            (i * 20);

        lv_point_t p1 =
            {
                .x = x1,
                .y = y1};

        lv_point_t p2 =
            {
                .x = x2,
                .y = y2};

        lv_draw_line(
            draw_ctx,
            &d,
            &p1,
            &p2);
    }

    for (int i = 0;
         i < gAircraftCount;
         i++)
    {
        Aircraft *a =
            &gAircraft[i];

        if (!a->valid)
        {
            continue;
        }

        int px;
        int py;

        if (!AircraftToRadar(
                a,
                radius,
                &px,
                &py))
        {
            continue;
        }

        DrawAircraft(
            draw_ctx,
            cx + px,
            cy + py,
            a,
            i == selectedAircraft);

        /* speed vector removed — the heading projection added noise
           without telling the user anything the trail doesn't already
           show over time. */

        if (showAircraftLabels && strlen(a->callsign) > 0)
        {
            lv_draw_label_dsc_t label;

            lv_draw_label_dsc_init(&label);

            label.color =
                lv_palette_main(
                    LV_PALETTE_GREEN);

            label.font =
                &lv_font_montserrat_12;

            lv_area_t txt_area =
                {
                    .x1 = cx + px + 6,
                    .y1 = cy + py - 8,
                    .x2 = cx + px + 80,
                    .y2 = cy + py + 8};

            lv_draw_label(
                draw_ctx,
                &label,
                &txt_area,
                a->callsign,
                NULL);
        }
    }
}

void Radar_AttachToObject(
    lv_obj_t *obj)
{
    radarObject = obj;

    /* The radar is a draw canvas (no child objects per aircraft), so make
       the whole area clickable and drop ADV_HITTEST (it would block taps
       on the transparent image). Touch selection is resolved in the
       LV_EVENT_CLICKED handler via Radar_PickAircraft. */
    lv_obj_add_flag(radarObject, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(radarObject, LV_OBJ_FLAG_ADV_HITTEST);

    lv_obj_add_event_cb(
        radarObject,
        radar_draw_cb,
        LV_EVENT_DRAW_MAIN,
        NULL);
}

int Radar_PickAircraft(
    int screenX,
    int screenY)
{
    if (!radarObject)
        return -1;

    lv_area_t area;

    lv_obj_get_content_coords(
        radarObject,
        &area);

    int width = area.x2 - area.x1;
    int height = area.y2 - area.y1;

    int cx = area.x1 + width / 2;
    int cy = area.y1 + height / 2;

    int radius =
        LV_MIN(
            width,
            height) /
        2;

    /* finger-tap tolerance (squared). The aircraft sprites are now
       up to 96 px across for AC_LARGE, so the tap-tolerance needs to
       be at least half the largest sprite diagonal (~68 px). 60 px
       gives a comfortable hit area without making the radar feel
       inaccurate. */
    const int tolPx = 60;
    int bestDist = tolPx * tolPx;
    int best = -1;

    for (int i = 0;
         i < gAircraftCount;
         i++)
    {
        Aircraft *a =
            &gAircraft[i];

        if (!a->valid)
            continue;

        int px;
        int py;

        if (!AircraftToRadar(
                a,
                radius,
                &px,
                &py))
        {
            continue;
        }

        int sx = cx + px;
        int sy = cy + py;

        int dx = sx - screenX;
        int dy = sy - screenY;

        int d2 = dx * dx + dy * dy;

        if (d2 <= bestDist)
        {
            bestDist = d2;
            best = i;
        }
    }

    return best;
}

void Radar_SetCenter(
    float lat,
    float lon,
    float radiusKm)
{
    radarCenterLat = lat;
    radarCenterLon = lon;
    radarRadiusKm = radiusKm;
}

void Radar_Refresh(void)
{
    if (radarObject)
    {
        lv_obj_invalidate(
            radarObject);
    }
}