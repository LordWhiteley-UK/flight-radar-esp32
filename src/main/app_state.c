/*
 * app_state.c — portable application logic shared by the ESP32 firmware and
 * the SDL2 simulator. Extracted from main.c: radar settings/units persistence,
 * the selected-aircraft info panel, the UI coordinate refresh, the fetch
 * bounding-box math, and the shared LVGL timer callbacks + data-age display.
 *
 * No ESP-IDF APIs here — everything hardware/OS-specific goes through
 * platform.h, so this file compiles unchanged on both targets.
 */
#include "main.h"
#include "radar.h"
#include "opensky_client.h"
#include "ui.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define RADAR_NAMESPACE "radar"

#define REFRESH_DEFAULT_S 22
#define REFRESH_MIN_S     5

static float radarLat = 50.881130f;
static float radarLon = -1.265500f;
static float radarRangeKm = 50.0f;

/* OpenSky fetch interval, seconds. Persisted under radar:refresh.
   Clamped to [REFRESH_MIN_S, +inf) on set; default REFRESH_DEFAULT_S. */
static int refreshIntervalS = REFRESH_DEFAULT_S;

static bool gUnitsImperial = true;  /* true = ft + mph, false = m + km/h */

static uint32_t lastApiUpdateMs = 0;

static const char *GetCategoryName(int category)
{
    switch (category) {
    case 2:  return "Light";
    case 3:  return "Small";
    case 4:  return "Large";
    case 5:  return "Heavy Vortex";
    case 6:  return "Heavy";
    case 8:  return "Rotorcraft";
    case 9:  return "Glider";
    case 14: return "UAV";
    default: return "Unknown";
    }
}

void UpdateSelectedAircraftUI(void)
{
    Aircraft *a = Radar_GetSelectedAircraft();

    lv_label_set_text_fmt(uic_LabelPlaneCount, "%d aircraft tracked", gAircraftCount);

    if (!a) {
        /* No aircraft selected: neutral placeholders (not design-time stubs). */
        lv_label_set_text(uic_LabelCraftName, "—");
        lv_label_set_text(uic_LabelCraftOrigin, "—");
        lv_label_set_text(uic_LabelCraftSpeed, "—");
        lv_label_set_text(uic_LabelCraftAlt, "—");
        lv_label_set_text(uic_LabelCraftHeading, "—");
        if (uic_LabelCraftClimb)
            lv_label_set_text(uic_LabelCraftClimb, "—");
        return;
    }

    lv_label_set_text(
        uic_LabelCraftName,
        (a->callsign[0] != '\0') ? a->callsign : a->icao24);

    lv_label_set_text(
        uic_LabelCraftOrigin,
        (a->originCountry[0] != '\0') ? a->originCountry : "Unknown");

    char buf[64];

    if (a->onGround) {
        lv_label_set_text(uic_LabelCraftSpeed, "GND");
        lv_label_set_text(uic_LabelCraftAlt, "GND");
        if (uic_LabelCraftClimb)
            lv_label_set_text(uic_LabelCraftClimb, "On ground");
    } else {
        if (gUnitsImperial)
            snprintf(buf, sizeof(buf), "%.0f mph", a->velocity * 2.23694f);
        else
            snprintf(buf, sizeof(buf), "%.0f km/h", a->velocity * 3.6f);
        lv_label_set_text(uic_LabelCraftSpeed, buf);

        if (gUnitsImperial)
            snprintf(buf, sizeof(buf), "%.0f ft", a->predictedAltitude * 3.28084f);
        else
            snprintf(buf, sizeof(buf), "%.0f m", a->predictedAltitude);
        lv_label_set_text(uic_LabelCraftAlt, buf);

        if (uic_LabelCraftClimb) {
            if (fabsf(a->verticalRate) < 0.5f) {
                lv_label_set_text(uic_LabelCraftClimb, "Level");
            } else {
                const char *arrow = a->verticalRate > 0 ? "^" : "v";
                if (gUnitsImperial)
                    snprintf(buf, sizeof(buf), "%.0f ft/min %s",
                             fabsf(a->verticalRate) * 196.850f, arrow);
                else
                    snprintf(buf, sizeof(buf), "%.1f m/s %s",
                             fabsf(a->verticalRate), arrow);
                lv_label_set_text(uic_LabelCraftClimb, buf);
            }
        }
    }

    snprintf(buf, sizeof(buf), "%.0f°", a->heading);
    lv_label_set_text(uic_LabelCraftHeading, buf);

    lv_label_set_text(uic_LabelCraftCategory, GetCategoryName(a->category));
}

void setUICoords(void)
{
    if (platform_lvgl_lock(-1)) {
        UpdateSelectedAircraftUI();

        char buf[64];
        snprintf(buf, sizeof(buf), "%.4f\n%.4f", (double)radarLat, (double)radarLon);
        lv_label_set_text(uic_LabelCoords, buf);

        if (uic_LabelRange)
            lv_label_set_text_fmt(uic_LabelRange, "Range: %d km", (int)radarRangeKm);

        /* keep the units switch knob in sync with the stored value */
        if (uic_SwitchUnits) {
            if (gUnitsImperial)
                lv_obj_add_state(uic_SwitchUnits, LV_STATE_CHECKED);
            else
                lv_obj_clear_state(uic_SwitchUnits, LV_STATE_CHECKED);
        }

        platform_lvgl_unlock();
    }
}

void SaveRadarSettings(float lat, float lon, float rangeKm)
{
    platform_storage_set_blob(RADAR_NAMESPACE, "lat", &lat, sizeof(lat));
    platform_storage_set_blob(RADAR_NAMESPACE, "lon", &lon, sizeof(lon));
    platform_storage_set_blob(RADAR_NAMESPACE, "range", &rangeKm, sizeof(rangeKm));
    platform_log(PLAT_LOG_WARN, "RADAR", "Saved %.4f %.4f %.1f", lat, lon, rangeKm);
}

bool LoadRadarSettings(float *lat, float *lon, float *rangeKm)
{
    size_t len = sizeof(float);
    bool a = platform_storage_get_blob(RADAR_NAMESPACE, "lat", lat, &len);
    len = sizeof(float);
    bool b = platform_storage_get_blob(RADAR_NAMESPACE, "lon", lon, &len);
    len = sizeof(float);
    bool c = platform_storage_get_blob(RADAR_NAMESPACE, "range", rangeKm, &len);
    if (a && b && c)
        platform_log(PLAT_LOG_WARN, "RADAR",
                     "Loaded %.4f %.4f %.1f", *lat, *lon, *rangeKm);
    return a && b && c;
}

float GetRadarLat(void)   { return radarLat; }
float GetRadarLon(void)   { return radarLon; }
float GetRadarRange(void) { return radarRangeKm; }

void SetRadarSettings(float lat, float lon, float rangeKm)
{
    radarLat = lat;
    radarLon = lon;
    radarRangeKm = rangeKm;
    SaveRadarSettings(radarLat, radarLon, radarRangeKm);
    Radar_SetCenter(radarLat, radarLon, radarRangeKm);
    setUICoords();
}

void SetRadarRange(float rangeKm)
{
    if (rangeKm < 10.0f) rangeKm = 10.0f;
    if (rangeKm > 500.0f) rangeKm = 500.0f;
    radarRangeKm = rangeKm;
    SaveRadarSettings(radarLat, radarLon, radarRangeKm);
    Radar_SetCenter(radarLat, radarLon, radarRangeKm);
    setUICoords();
    Radar_Refresh();
}

bool GetUnitsImperial(void) { return gUnitsImperial; }

/* ---- refresh interval (seconds) ---- */
int GetRefreshInterval(void) { return refreshIntervalS; }

void SetRefreshInterval(int seconds)
{
    if (seconds < REFRESH_MIN_S) seconds = REFRESH_MIN_S;
    refreshIntervalS = seconds;
    platform_storage_set_i32(RADAR_NAMESPACE, "refresh", refreshIntervalS);
}

bool LoadRefreshInterval(void)
{
    int32_t v = REFRESH_DEFAULT_S;
    if (platform_storage_get_i32(RADAR_NAMESPACE, "refresh", &v))
        refreshIntervalS = (v < REFRESH_MIN_S) ? REFRESH_MIN_S : (int)v;
    else
        refreshIntervalS = REFRESH_DEFAULT_S;
    return true;
}

void SaveUnits(void)
{
    platform_storage_set_u8(RADAR_NAMESPACE, "units", gUnitsImperial ? 1 : 0);
}

void LoadUnits(void)
{
    uint8_t v = 1;
    if (platform_storage_get_u8(RADAR_NAMESPACE, "units", &v))
        gUnitsImperial = (v != 0);
    else
        gUnitsImperial = true;
}

void SetUnits(bool imperial)
{
    gUnitsImperial = imperial;
    SaveUnits();

    if (uic_SwitchUnits) {
        if (gUnitsImperial)
            lv_obj_add_state(uic_SwitchUnits, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(uic_SwitchUnits, LV_STATE_CHECKED);
    }

    UpdateSelectedAircraftUI();
    Radar_Refresh();
}

void LoadTrail(void)
{
    uint8_t v = 0;
    if (platform_storage_get_u8(RADAR_NAMESPACE, "trail", &v))
        Radar_SetTrail(v != 0);
    else
        Radar_SetTrail(false);

    if (uic_SwitchTrail) {
        if (showSelectedTrail)
            lv_obj_add_state(uic_SwitchTrail, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(uic_SwitchTrail, LV_STATE_CHECKED);
    }

    /* Sync the right-rail trail switch on Screen1 to the same state. */
    syncTrailSwitchState();
}

/* Fetch bounding box for a center + radius (km). */
void ComputeBBox(float centerLat, float centerLon, float radiusKm,
                 float *minLat, float *maxLat, float *minLon, float *maxLon)
{
    float latDelta = radiusKm / 111.0f;
    float lonDelta = radiusKm / (111.0f * cosf(centerLat * (float)M_PI / 180.0f));
    *minLat = centerLat - latDelta;
    *maxLat = centerLat + latDelta;
    *minLon = centerLon - lonDelta;
    *maxLon = centerLon + lonDelta;
}

/* Called right after a successful OpenSky fetch+parse so data-age is measured
   from the refresh, not from boot. */
void AppState_RecordApiUpdate(void)
{
    lastApiUpdateMs = platform_now_ms();
}

uint32_t AppState_GetApiAgeSec(void)
{
    if (lastApiUpdateMs == 0) return 9999;
    return (platform_now_ms() - lastApiUpdateMs) / 1000;
}

/* Refresh the bottom-of-radar API-age label and turn it red when the
   data is stale (>60s). */
void AppState_UpdateAgeLabel(void)
{
    uint32_t age = AppState_GetApiAgeSec();

    extern lv_obj_t * ui_LabelAPIRefreshBig;
    if (ui_LabelAPIRefreshBig) {
        lv_label_set_text_fmt(ui_LabelAPIRefreshBig, "API: %us", (unsigned)age);
        lv_color_t c = (age > 60) ? lv_palette_main(LV_PALETTE_RED)
                                  : lv_color_hex(0x7CFFB0);
        lv_obj_set_style_text_color(ui_LabelAPIRefreshBig, c, LV_PART_MAIN);
    }
}

/* Show the centred "Waiting for OpenSky API..." banner on the radar panel.
   Called once at boot — see AppState_HideWaitingBanner() for the off-switch. */
void AppState_ShowWaitingBanner(void)
{
    extern lv_obj_t * ui_LabelWaitingBanner;
    if (ui_LabelWaitingBanner)
        lv_obj_clear_flag(ui_LabelWaitingBanner, LV_OBJ_FLAG_HIDDEN);
}

/* Hide the banner. Called after the first successful OpenSky fetch. */
void AppState_HideWaitingBanner(void)
{
    extern lv_obj_t * ui_LabelWaitingBanner;
    if (ui_LabelWaitingBanner)
        lv_obj_add_flag(ui_LabelWaitingBanner, LV_OBJ_FLAG_HIDDEN);
}

/* ---- shared LVGL timer callbacks (used by both targets) ---- */

void AppState_SweepTimerCb(lv_timer_t *t)
{
    (void)t;
    Radar_SweepTick();
}

void AppState_AircraftUiTimerCb(lv_timer_t *t)
{
    (void)t;
    UpdateSelectedAircraftUI();
    AppState_UpdateAgeLabel();

    /* clock in the status bar */
    if (uic_LabelClock) {
        char tm[8];
        platform_localtime_hm(tm, sizeof(tm));
        lv_label_set_text(uic_LabelClock, tm);
    }
}