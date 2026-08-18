#include "opensky_client.h"
#include "platform.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cJSON.h"

#define OPENSKY_NAMESPACE "opensky"
#define TAG "OpenSky"

static char clientId[128];
static char clientSecret[256];

static char accessToken[2048];

static time_t tokenExpiry = 0;

static char *responseBuffer = NULL;
static size_t responseCapacity = 0;

#define MAX_AIRCRAFT 200

Aircraft gAircraft[MAX_AIRCRAFT];
int gAircraftCount = 0;

bool OpenSky_ParseAircraft(
    const char *json)
{
    gAircraftCount = 0;

    cJSON *root =
        cJSON_Parse(json);

    if (!root)
        return false;

    cJSON *states =
        cJSON_GetObjectItem(
            root,
            "states");

    if (!cJSON_IsArray(states))
    {
        cJSON_Delete(root);
        return false;
    }

    int count =
        cJSON_GetArraySize(states);

    for (int i = 0;
         i < count &&
         gAircraftCount < MAX_AIRCRAFT;
         i++)
    {
        cJSON *state =
            cJSON_GetArrayItem(
                states,
                i);

        if (!cJSON_IsArray(state))
            continue;

        Aircraft *a =
            &gAircraft[gAircraftCount];

        memset(
            a,
            0,
            sizeof(Aircraft));

        cJSON *icao =
            cJSON_GetArrayItem(state, 0);

        cJSON *callsign =
            cJSON_GetArrayItem(state, 1);

        cJSON *lon =
            cJSON_GetArrayItem(state, 5);

        cJSON *lat =
            cJSON_GetArrayItem(state, 6);

        cJSON *vel =
            cJSON_GetArrayItem(state, 9);

        cJSON *hdg =
            cJSON_GetArrayItem(state, 10);

        cJSON *alt =
            cJSON_GetArrayItem(state, 13);
        cJSON *category =
            cJSON_GetArrayItem(state, 17);

        cJSON *country =
            cJSON_GetArrayItem(state, 2);

        cJSON *onGround =
            cJSON_GetArrayItem(state, 8);

        cJSON *vrate =
            cJSON_GetArrayItem(state, 11);

        if (!icao ||
            !lat ||
            !lon)
        {
            continue;
        }

        if (country &&
            cJSON_IsString(country))
        {
            strncpy(
                a->originCountry,
                country->valuestring,
                sizeof(a->originCountry) - 1);
        }

        a->category = 0;

        if (category &&
            cJSON_IsNumber(category))
        {
            a->category =
                category->valueint;
        }

        strncpy(
            a->icao24,
            icao->valuestring,
            sizeof(a->icao24) - 1);
        a->icao24[sizeof(a->icao24) - 1] = '\0';

        if (callsign &&
            cJSON_IsString(callsign))
        {
            strncpy(
                a->callsign,
                callsign->valuestring,
                sizeof(a->callsign) - 1);
        }

        if (cJSON_IsNumber(lat))
            a->latitude = lat->valuedouble;

        if (cJSON_IsNumber(lon))
            a->longitude = lon->valuedouble;

        if (cJSON_IsNumber(vel))
            a->velocity = vel->valuedouble;

        if (cJSON_IsNumber(hdg))
            a->heading = hdg->valuedouble;

        if (cJSON_IsNumber(alt))
            a->altitude = alt->valuedouble;

        if (onGround)
            a->onGround = cJSON_IsTrue(onGround);

        if (vrate && cJSON_IsNumber(vrate))
            a->verticalRate = (float)vrate->valuedouble;

        a->valid = true;

        a->latitude = lat->valuedouble;
        a->longitude = lon->valuedouble;

        a->predictedLat = a->latitude;
        a->predictedLon = a->longitude;
        a->predictedAltitude = a->altitude;

        a->lastUpdateMs = platform_now_ms();

        gAircraftCount++;
    }

    cJSON_Delete(root);

    return true;
}

static bool LoadCredentials(void)
{
    bool id = platform_storage_get_str(
        OPENSKY_NAMESPACE, "client_id", clientId, sizeof(clientId));

    bool sec = platform_storage_get_str(
        OPENSKY_NAMESPACE, "client_secret", clientSecret, sizeof(clientSecret));

    platform_log(PLAT_LOG_INFO, TAG,
                 "LoadCredentials id=%d secret=%d", id, sec);

    return id && sec;
}

static bool RequestToken(void)
{
    time_t now;
    time(&now);

    platform_log(PLAT_LOG_INFO, TAG, "RequestToken epoch=%lld", (long long)now);

    char postBody[512];

    snprintf(
        postBody,
        sizeof(postBody),
        "grant_type=client_credentials"
        "&client_id=%s"
        "&client_secret=%s",
        clientId,
        clientSecret);

    const char *headers[] = { "Content-Type: application/x-www-form-urlencoded" };

    size_t len = 0;
    int status = platform_http_post(
        "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token",
        headers, 1,
        postBody, strlen(postBody),
        responseBuffer, responseCapacity, &len);

    if (status != 200)
    {
        platform_log(PLAT_LOG_ERROR, TAG, "Token request HTTP status=%d", status);
        return false;
    }

    cJSON *root =
        cJSON_Parse(responseBuffer);

    if (!root)
        return false;

    cJSON *token =
        cJSON_GetObjectItem(root, "access_token");

    cJSON *expires =
        cJSON_GetObjectItem(root, "expires_in");

    if (!token || !expires)
    {
        cJSON_Delete(root);
        return false;
    }

    strncpy(
        accessToken,
        token->valuestring,
        sizeof(accessToken) - 1);

    accessToken[sizeof(accessToken) - 1] = '\0';

    tokenExpiry =
        time(NULL) +
        expires->valueint -
        60;

    cJSON_Delete(root);

    platform_log(PLAT_LOG_INFO, TAG, "Token acquired");

    return true;
}

static bool EnsureToken(void)
{
    time_t now =
        time(NULL);

    if (accessToken[0] &&
        now < tokenExpiry)
    {
        return true;
    }

    return RequestToken();
}

bool OpenSky_Init(void)
{
    responseCapacity = 65536;

    responseBuffer =
        malloc(responseCapacity);

    if (!responseBuffer)
        return false;

    responseBuffer[0] = '\0';

    return LoadCredentials();
}

bool OpenSky_HasCredentials(void)
{
    return strlen(clientId) > 0 &&
           strlen(clientSecret) > 0;
}

bool OpenSky_GetAircraftJson(
    float minLat,
    float maxLat,
    float minLon,
    float maxLon,
    char *buffer,
    size_t bufferSize)
{
    if (!EnsureToken())
    {
        platform_log(PLAT_LOG_ERROR, TAG, "Token unavailable");
        return false;
    }

    char bearer[2200];

    snprintf(
        bearer,
        sizeof(bearer),
        "Authorization: Bearer %s",
        accessToken);

    char url[512];

    snprintf(
        url,
        sizeof(url),
        "https://opensky-network.org/api/states/all?"
        "lamin=%.6f&lamax=%.6f&"
        "lomin=%.6f&lomax=%.6f",
        minLat,
        maxLat,
        minLon,
        maxLon);

    platform_log(PLAT_LOG_INFO, TAG, "Request URL: %s", url);

    const char *headers[] = { bearer };

    size_t len = 0;
    int status = platform_http_get(
        url, headers, 1,
        responseBuffer, responseCapacity, &len);

    if (status != 200)
    {
        platform_log(PLAT_LOG_ERROR, TAG, "HTTP GET status=%d", status);
        return false;
    }

    strncpy(
        buffer,
        responseBuffer,
        bufferSize - 1);

    buffer[bufferSize - 1] = '\0';

    return true;
}