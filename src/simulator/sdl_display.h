#ifndef SDL_DISPLAY_H
#define SDL_DISPLAY_H

#include <stdbool.h>

/* Initialise an SDL2 window + LVGL display + mouse indev (800x480, RGB565). */
void sdl_display_init(void);

/* Pump SDL events into LVGL and advance the LVGL tick. Call each loop iter. */
void sdl_pump_events(void);

/* Return false when the simulator window has been asked to close. */
bool sdl_keep_running(void);

#endif /* SDL_DISPLAY_H */