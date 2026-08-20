#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_AIRCRAFT 200

typedef struct
{
    char icao24[12];
    char callsign[16];
    char originCountry[64];

    int category;

    float longitude;
    float latitude;

    float altitude;
    float velocity;
    float heading;

    float verticalRate;  /* m/s, + = climbing, - = descending */
    bool  onGround;     /* true when on the ground */

    /* True when this fetch populated `altitude` with a real barometric
       reading. OpenSky often returns null for altitude (older transponders,
       aircraft on the ground with no altitude reporting, etc.). When
       false, `altitude` is left at the last known value (or 0 on first
       fetch) and `predictedAltitude` MUST NOT be used to classify the
       aircraft — a missing altitude is not the same as "0 m, on the
       ground". */
    bool altitudeKnown;

    /* True when this fetch populated `category` with a real OpenSky
       category integer (0..20). OpenSky rarely populates this field —
       most aircraft return null. When false, `category` is 0 (which
       is also the "no information" sentinel, so we can't tell them
       apart without this flag) and the classifier falls back to
       altitude-based sizing. */
    bool categoryKnown;

    bool valid;

    float predictedLat;
    float predictedLon;
    float predictedAltitude;

    uint32_t lastUpdateMs;

} Aircraft;

extern Aircraft gAircraft[MAX_AIRCRAFT];
extern int gAircraftCount;

bool OpenSky_Init(void);
bool OpenSky_HasCredentials(void);

bool OpenSky_GetAircraftJson(
    float minLat,
    float maxLat,
    float minLon,
    float maxLon,
    char *buffer,
    size_t bufferSize);

bool OpenSky_ParseAircraft(
    const char *json);