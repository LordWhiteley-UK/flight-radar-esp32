#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined __has_include
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#endif
#endif

/* selected-aircraft info panel */
void UpdateSelectedAircraftUI(void);

/* radar settings (persisted) */
float GetRadarLat(void);
float GetRadarLon(void);
float GetRadarRange(void);

void SetRadarSettings(float lat, float lon, float rangeKm);
void SetRadarRange(float rangeKm);
void SaveRadarSettings(float lat, float lon, float rangeKm);
bool LoadRadarSettings(float *lat, float *lon, float *rangeKm);

/* units (persisted) */
bool GetUnitsImperial(void);
void SetUnits(bool imperial);
void SaveUnits(void);
void LoadUnits(void);

/* OpenSky fetch interval, seconds (persisted). Clamped to >= 5 on set. */
int  GetRefreshInterval(void);
void SetRefreshInterval(int seconds);
bool LoadRefreshInterval(void);

/* selected-aircraft trail toggle (persisted) */
void LoadTrail(void);

/* refresh the UI from the current settings (coords label, range label,
   units switch, selected-aircraft panel) */
void setUICoords(void);

/* fetch bounding box for a center + radius (km) */
void ComputeBBox(float centerLat, float centerLon, float radiusKm,
                 float *minLat, float *maxLat, float *minLon, float *maxLon);

/* data-age tracking + "Xs ago" label (stale warning >60s) */
void AppState_RecordApiUpdate(void);
uint32_t AppState_GetApiAgeSec(void);
void AppState_UpdateAgeLabel(void);

/* "Waiting for OpenSky API..." banner — shown on boot, hidden after the
   first successful fetch. */
void AppState_ShowWaitingBanner(void);
void AppState_HideWaitingBanner(void);

/* shared LVGL timer callbacks */
void AppState_SweepTimerCb(lv_timer_t *t);
void AppState_AircraftUiTimerCb(lv_timer_t *t);