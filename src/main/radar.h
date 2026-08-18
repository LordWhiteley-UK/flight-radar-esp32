#pragma once

#include "opensky_client.h"
#include "ui.h"

extern int selectedAircraft;
extern char selectedIcao24[16];

Aircraft *Radar_GetSelectedAircraft(void);

void Radar_ReconcileSelection(void);

void Radar_Init(void);

void Radar_SetCenter(
    float lat,
    float lon,
    float radiusKm);

void Radar_Refresh(void);

void Radar_AttachToObject(
    lv_obj_t *obj);

/* Hit-test: given an absolute screen point, return the index of the
   nearest displayed aircraft within a finger-tap tolerance, or -1. */
int Radar_PickAircraft(
    int screenX,
    int screenY);

void Radar_SweepTick(void);

void Radar_PredictAircraft(void);
extern bool showAircraftLabels;

/* Selected-aircraft trail (toggle). A single ring buffer holds the recent
   path of whichever aircraft is selected; it survives OpenSky refreshes and
   resets when the selection changes. */
extern bool showSelectedTrail;
void Radar_SetTrail(bool on);
void Radar_ClearTrail(void);