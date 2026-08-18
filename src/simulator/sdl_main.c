/*
 * sdl_main.c — desktop entry point for the Flight-radar-7 simulator.
 *
 * Boots the SDL2 + LVGL display, builds the shared UI, loads the radar screen,
 * and runs the same sweep / predict / fetch timers as the ESP32 firmware — but
 * the "fetch" replays a recorded OpenSky JSON from disk so the UI can be
 * iterated with no flashing and no network.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sdl_display.h"
#include "replay.h"
#include "ui.h"
#include "radar.h"
#include "opensky_client.h"
#include "main.h"
#include "platform.h"

/* ui_Screen2 registers this as a click handler; on device it lives in main.c.
   The simulator stubs it (no WiFi stack). */
void wifi_connect_btn_cb(lv_event_t *e) { (void)e; }

static void predict_timer_cb(lv_timer_t *t)
{
    (void)t;
    Radar_PredictAircraft();
    Radar_Refresh();
    AppState_UpdateAgeLabel();
}

static void fetch_timer_cb(lv_timer_t *t)
{
    (void)t;
    OpenSky_ParseAircraft(replay_json());
    AppState_RecordApiUpdate();
    Radar_ReconcileSelection();
    UpdateSelectedAircraftUI();
    Radar_Refresh();
    /* re-arm with the current refresh interval (user may have changed it) */
    lv_timer_set_period(t, GetRefreshInterval() * 1000);
}

static const char *find_replay(int argc, char **argv)
{
    static char path[256];
    if (argc > 1) {
        snprintf(path, sizeof(path), "%s", argv[1]);
        if (replay_load(path)) return path;
    }
    const char *tries[] = {
        "recordings/sample.json",
        "simulator/recordings/sample.json",
        "../simulator/recordings/sample.json",
        NULL,
    };
    for (int i = 0; tries[i]; i++) {
        snprintf(path, sizeof(path), "%s", tries[i]);
        if (replay_load(path)) return path;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *replay_path = find_replay(argc, argv);
    if (!replay_path) {
        fprintf(stderr, "Could not find a replay JSON. Pass one as argv[1].\n");
        return 1;
    }
    printf("Replay: %s  (%d bytes)\n", replay_path, (int)strlen(replay_json()));

    sdl_display_init();

    if (platform_lvgl_lock(-1)) {
        ui_init();
        Radar_AttachToObject(uic_Imageradar);

        /* boot straight onto the radar screen (skip the WiFi onboarding screen) */
        lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);

        lv_timer_create(AppState_SweepTimerCb, 30, NULL);
        lv_timer_create(AppState_AircraftUiTimerCb, 500, NULL);
        lv_timer_create(predict_timer_cb, 250, NULL);

        platform_lvgl_unlock();
    }

    LoadUnits();
    LoadTrail();
    LoadRefreshInterval();

    float lat = 50.881130f, lon = -1.265500f, range = 50.0f;
    LoadRadarSettings(&lat, &lon, &range);
    SetRadarSettings(lat, lon, range);

    /* initial replay so aircraft appear immediately */
    OpenSky_ParseAircraft(replay_json());
    AppState_RecordApiUpdate();
    Radar_ReconcileSelection();
    UpdateSelectedAircraftUI();
    Radar_Refresh();

    /* re-parse every GetRefreshInterval() seconds (persisted across runs) to
       exercise selection persistence across the array rebuild */
    lv_timer_create(fetch_timer_cb, GetRefreshInterval() * 1000, NULL);

    while (sdl_keep_running()) {
        sdl_pump_events();
        lv_timer_handler();
        SDL_Delay(5);
    }

    return 0;
}