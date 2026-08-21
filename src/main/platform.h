/*
 * platform.h — platform abstraction layer (HAL) for Flight-Tracker-7.
 *
 * The flight-tracker core (radar, opensky_client, app_state, ui) calls only this
 * API for anything hardware/OS-specific. Each target links its own
 * implementation:
 *   - ESP32-S3: platform_esp32.c  (NVS, esp_http_client, esp_wifi, esp_timer)
 *   - Desktop : platform_sdl.c    (file-backed config, libcurl, OS time, stubs)
 *
 * This keeps the core compiling and running unchanged on both targets, so the
 * UI/logic can be iterated in the SDL2 simulator without flashing the ESP32.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- time ---------------- */

/* Monotonic milliseconds (for dead-reckoning dt / data-age). */
uint32_t platform_now_ms(void);

/* Write local wall-clock time as "HH:MM" into out. Returns true on success. */
bool platform_localtime_hm(char *out, size_t out_size);

/* ---------------- logging ---------------- */

enum {
    PLAT_LOG_INFO = 0,
    PLAT_LOG_WARN = 1,
    PLAT_LOG_ERROR = 2,
};

void platform_log(int level, const char *tag, const char *fmt, ...);

/* ---------------- LVGL lock ---------------- */

/* Recursive LVGL mutex. timeout_ms == -1 waits forever. On the (single
   threaded) simulator these are no-ops that return true. */
bool platform_lvgl_lock(int timeout_ms);
void platform_lvgl_unlock(void);

/* ---------------- namespaced key-value storage ---------------- */

bool platform_storage_get_str(const char *ns, const char *key, char *out, size_t out_size);
bool platform_storage_set_str(const char *ns, const char *key, const char *val);

/* blob: inout_size holds buffer size on input, written size on output. */
bool platform_storage_get_blob(const char *ns, const char *key, void *out, size_t *inout_size);
bool platform_storage_set_blob(const char *ns, const char *key, const void *val, size_t size);

bool platform_storage_get_u8(const char *ns, const char *key, uint8_t *out);
bool platform_storage_set_u8(const char *ns, const char *key, uint8_t val);

bool platform_storage_get_i32(const char *ns, const char *key, int32_t *out);
bool platform_storage_set_i32(const char *ns, const char *key, int32_t val);

bool platform_storage_erase_key(const char *ns, const char *key);

/* ---------------- HTTP ---------------- */

/* Synchronous HTTPS GET/POST. headers is an array of "Name: value" strings
   (n_headers of them). The response body is written into buf (NUL-terminated),
   and *out_len receives the body length. Returns the HTTP status code (>=0) on
   a completed exchange, or -1 on a transport/timeout failure. */
int platform_http_get(const char *url, const char *const *headers, int n_headers,
                      char *buf, size_t buf_size, size_t *out_len);

int platform_http_post(const char *url, const char *const *headers, int n_headers,
                       const char *body, size_t body_len,
                       char *buf, size_t buf_size, size_t *out_len);

/* ---------------- WiFi (ESP32-only; simulator stubs) ---------------- */

void platform_wifi_connect(const char *ssid, const char *password);
void platform_wifi_disconnect(void);

/* Erase stored WiFi credentials and disconnect. */
void platform_forget_wifi_creds(void);

/* ---------------- app lifecycle ---------------- */

/* Erase stored OpenSky API credentials. */
void platform_forget_opensky_creds(void);

/* ESP32: esp_restart(). Simulator: no-op (log) — the UI refreshes on next
   interaction. */
void platform_restart(void);

#ifdef __cplusplus
}
#endif