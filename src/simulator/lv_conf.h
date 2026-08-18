/*
 * lv_conf.h — LVGL 8.4 configuration for the desktop SDL2 simulator.
 *
 * Mirrors the device: 16-bit colour, no byte swap (SquareLine UI asserts both).
 * Only the non-default values are set here; lv_conf_internal.h fills in the
 * rest from lv_conf_template.h defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ---- colour ---- */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* ---- resolution ---- */
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480

/* ---- memory: use the C library allocator ---- */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "stdlib.h"
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free

/* ---- logging to stdout ---- */
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* ---- theme ---- */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

/* ---- fonts: the set the UI uses ---- */
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_38 1
#define LV_FONT_MONTSERRAT_48 1

/* ---- drawing features (arcs, rounded rects, alpha) ---- */
#define LV_DRAW_COMPLEX 1
#define LV_USE_DRAW_MASKS 1

/* ---- disable stuff we don't need ---- */
#define LV_USE_GPU_SDL 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0

#endif /* LV_CONF_H */