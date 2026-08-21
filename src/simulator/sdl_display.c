/*
 * sdl_display.c — minimal SDL2 display + mouse HAL for the LVGL simulator.
 *
 * LVGL 8.4 has no in-tree SDL *window* driver (only an SDL GPU draw backend),
 * so this is a small hand-written HAL: an SDL window/renderer/streaming
 * texture, a flush callback that uploads the invalidated area, a mouse pointer
 * indev fed from SDL mouse events, and a tick driven by SDL_GetTicks.
 * Software renderer only (LV_DRAW_COMPLEX on, no GPU backend).
 */
#include "sdl_display.h"

#include <SDL2/SDL.h>
#include <stdlib.h>

#include "lvgl.h"

#define DISP_W 800
#define DISP_H 480

static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture  = NULL;

/* LVGL renders into this single full-screen buffer; flush_cb uploads the
   invalidated area from it into the streaming texture. */
static lv_color_t *framebuf = NULL;

static lv_disp_drv_t  disp_drv;
static lv_indev_drv_t indev_drv;

/* mouse state, updated from SDL events */
static int  mouse_x = 0, mouse_y = 0;
static bool mouse_pressed = false;
static bool want_quit = false;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    (void)drv;
    if (!area) { lv_disp_flush_ready(drv); return; }

    SDL_Rect r = { .x = area->x1, .y = area->y1,
                   .w = area->x2 - area->x1 + 1,
                   .h = area->y2 - area->y1 + 1 };

    int pitch = DISP_W * (int)sizeof(lv_color_t);
    const void *src = (const uint8_t *)framebuf + r.y * pitch + r.x * (int)sizeof(lv_color_t);

    SDL_UpdateTexture(texture, &r, src, pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    lv_disp_flush_ready(drv);
}

static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point.x = mouse_x;
    data->point.y = mouse_y;
    data->state = mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void sdl_display_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        exit(1);
    }

    window = SDL_CreateWindow("Flight Tracker 7 (simulator)",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              DISP_W, DISP_H, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING,
                                DISP_W, DISP_H);

    framebuf = malloc((size_t)DISP_W * DISP_H * sizeof(lv_color_t));

    /* LVGL init + display */
    lv_init();

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, framebuf, NULL, DISP_W * DISP_H);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_W;
    disp_drv.ver_res = DISP_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* mouse as the touch indev */
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read_cb;
    lv_indev_drv_register(&indev_drv);
}

void sdl_pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            want_quit = true;
            break;
        case SDL_MOUSEMOTION:
            mouse_x = e.motion.x;
            mouse_y = e.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouse_pressed = true;
                mouse_x = e.button.x;
                mouse_y = e.button.y;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT)
                mouse_pressed = false;
            break;
        default:
            break;
        }
    }

    /* advance LVGL tick */
    static uint32_t last = 0;
    uint32_t now = SDL_GetTicks();
    if (last == 0) last = now;
    lv_tick_inc(now - last);
    last = now;
}

bool sdl_keep_running(void)
{
    return !want_quit;
}