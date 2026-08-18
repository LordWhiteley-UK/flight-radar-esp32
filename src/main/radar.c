#include "radar.h"
#include "main.h"
#include "platform.h"

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

static int CategorySize(
    int category,
    bool selected)
{
    int base;

    switch (category)
    {
    case 2:  /* Light */
    case 8:  /* Rotorcraft */
    case 9:  /* Glider */
    case 14: /* UAV */
        base = 7;
        break;
    case 3: /* Small */
        base = 10;
        break;
    case 4: /* Large */
    default:
        base = 14;
        break;
    case 5: /* Heavy vortex */
    case 6: /* Heavy */
        base = 20;
        break;
    }

    return base;   /* size is fixed per category; the yellow ring is the
                      only "selected" indicator so the icon does not
                      appear to grow when tapped. */
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

static void DrawAircraft(
    lv_draw_ctx_t *draw_ctx,
    int x,
    int y,
    const Aircraft *a,
    bool selected)
{
    float h =
        a->heading *
        0.0174532925f;

    int size =
        CategorySize(a->category, selected);

    /* Every aircraft — selected or not — draws the full icon (fuselage +
       wings + tail bar) in its climb-state colour. The selected aircraft
       is distinguished only by the yellow ring around it and the +4 size
       bump from CategorySize. */
    lv_color_t color = ClimbColor(a);

    float ch = cosf(h);
    float sh = sinf(h);

    /* selected: full aircraft icon in climb colour */
#define PX(R, F) (x + (int)((R) * ch + (F) * sh))
#define PY(R, F) (y - (int)((F) * ch - (R) * sh))

    /* Symmetric fuselage: nose and tail are the same distance from the
       wing centre so the body reads as a plane shape, not a long line
       sticking out in front of the wings. Wings are narrower than the
       fuselage is long, so the silhouette reads as "plane shape" rather
       than "cross". */
    int sf = size;
    lv_point_t nose    = {PX(0,  sf * 0.8f),            PY(0,  sf * 0.8f)};
    lv_point_t tail    = {PX(0, -sf * 0.8f),            PY(0, -sf * 0.8f)};
    lv_point_t lwing   = {PX(-size * 0.7f, 0),           PY(-size * 0.7f, 0)};
    lv_point_t rwing   = {PX( size * 0.7f, 0),           PY( size * 0.7f, 0)};
    lv_point_t tleft   = {PX(-size * 0.30f, -sf * 0.8f),PY(-size * 0.30f, -sf * 0.8f)};
    lv_point_t tright  = {PX( size * 0.30f, -sf * 0.8f),PY( size * 0.30f, -sf * 0.8f)};

#undef PX
#undef PY

    lv_draw_line_dsc_t ln;

    lv_draw_line_dsc_init(&ln);

    ln.color = color;
    ln.width = 2;

    /* fuselage */
    lv_draw_line(draw_ctx, &ln, &nose, &tail);
    /* wings */
    lv_draw_line(draw_ctx, &ln, &lwing, &rwing);
    /* tail bar */
    lv_draw_line(draw_ctx, &ln, &tleft, &tright);

    if (selected)
    {
        lv_draw_arc_dsc_t ring;

        lv_draw_arc_dsc_init(&ring);

        ring.color = lv_palette_main(LV_PALETTE_YELLOW);
        ring.width = 2;

        lv_point_t center = {.x = x, .y = y};

        lv_draw_arc(
            draw_ctx,
            &ring,
            &center,
            size + 6,
            0,
            360);
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
    const lv_font_t *f_small = &lv_font_montserrat_12;
    const lv_font_t *f_compass = &lv_font_montserrat_14;

    /* ring distance labels along the 60° azimuth (upper-right), one per ring */
    const float fracs[] = {1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
    char buf[16];
    for (int k = 0; k < 3; k++)
    {
        int r = (int)(radius * fracs[k]);
        float rad = 60.0f * 0.0174532925f;   /* 0° = north, clockwise */
        int px = cx + (int)(sinf(rad) * r);
        int py = cy - (int)(cosf(rad) * r);
        int km = (int)(radarRadiusKm * fracs[k] + 0.5f);
        snprintf(buf, sizeof(buf), "%d", km);
        draw_centered_label(draw_ctx, px, py, buf, dim, f_small);
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

    lv_draw_arc_dsc_t arc;

    lv_draw_arc_dsc_init(
        &arc);

    arc.color =
        lv_palette_main(
            LV_PALETTE_GREEN);

    arc.width = 2;

    lv_point_t center =
        {
            .x = cx,
            .y = cy};

    lv_draw_arc(
        draw_ctx,
        &arc,
        &center,
        radius,
        0,
        360);
    lv_draw_arc(
        draw_ctx,
        &arc,
        &center,
        radius * 2 / 3,
        0,
        360);

    lv_draw_arc(
        draw_ctx,
        &arc,
        &center,
        radius / 3,
        0,
        360);

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

    /* finger-tap tolerance (squared) */
    const int tolPx = 22;
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