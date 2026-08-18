/*
 * platform_sdl.c — desktop implementation of the platform HAL.
 *
 * Storage is a JSON file in the working directory (flightradar_sim_config.json)
 * holding namespaced keys. HTTP/WiFi are stubbed because the simulator runs in
 * replay mode (a recorded OpenSky JSON is fed in directly); live HTTP via
 * libcurl is a later addition and isn't needed to iterate the UI.
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "cJSON.h"

#define CONFIG_FILE "flightradar_sim_config.json"

static cJSON *config_root = NULL;

static void config_save(void)
{
    if (!config_root) return;
    char *s = cJSON_PrintUnformatted(config_root);
    if (!s) return;
    FILE *f = fopen(CONFIG_FILE, "w");
    if (f) { fputs(s, f); fclose(f); }
    free(s);
}

static void config_load(void)
{
    if (config_root) return;
    FILE *f = fopen(CONFIG_FILE, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc(n + 1);
        if (buf) {
            size_t rd = fread(buf, 1, n, f);
            buf[rd] = '\0';
            config_root = cJSON_Parse(buf);
            free(buf);
        }
        fclose(f);
    }
    if (!config_root) config_root = cJSON_CreateObject();
}

static void make_key(char *out, size_t out_size, const char *ns, const char *key)
{
    snprintf(out, out_size, "%s:%s", ns, key);
}

/* ---------------- time ---------------- */

uint32_t platform_now_ms(void)
{
    return SDL_GetTicks();
}

bool platform_localtime_hm(char *out, size_t out_size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    snprintf(out, out_size, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return true;
}

/* ---------------- logging ---------------- */

void platform_log(int level, const char *tag, const char *fmt, ...)
{
    const char *pfx = (level == PLAT_LOG_ERROR) ? "E" :
                      (level == PLAT_LOG_WARN)  ? "W" : "I";
    fprintf(stderr, "%s (%s) ", pfx, tag);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---------------- LVGL lock (single-threaded sim) ---------------- */

bool platform_lvgl_lock(int timeout_ms) { (void)timeout_ms; return true; }
void platform_lvgl_unlock(void) {}

/* ---------------- storage ---------------- */

bool platform_storage_get_str(const char *ns, const char *key, char *out, size_t out_size)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON *item = cJSON_GetObjectItem(config_root, k);
    if (!cJSON_IsString(item)) return false;
    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

bool platform_storage_set_str(const char *ns, const char *key, const char *val)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON_DeleteItemFromObject(config_root, k);
    cJSON_AddStringToObject(config_root, k, val);
    config_save();
    return true;
}

static void hex_encode(char *out, size_t out_size, const void *data, size_t n)
{
    const uint8_t *b = data;
    size_t i;
    for (i = 0; i < n && (i * 2 + 2) < out_size; i++)
        snprintf(out + i * 2, out_size - i * 2, "%02x", b[i]);
    out[n * 2] = '\0';
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t hex_decode(void *out, size_t out_size, const char *hex)
{
    uint8_t *o = out;
    size_t i = 0;
    while (hex[0] && hex[1] && i < out_size) {
        int hi = hex_val(hex[0]);
        int lo = hex_val(hex[1]);
        if (hi < 0 || lo < 0) break;
        o[i++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return i;
}

bool platform_storage_get_blob(const char *ns, const char *key, void *out, size_t *inout_size)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON *item = cJSON_GetObjectItem(config_root, k);
    if (!cJSON_IsString(item)) return false;
    size_t got = hex_decode(out, *inout_size, item->valuestring);
    if (got != *inout_size) return false;   /* size mismatch */
    return true;
}

bool platform_storage_set_blob(const char *ns, const char *key, const void *val, size_t size)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    char hex[256];
    if (size * 2 >= sizeof(hex)) return false;   /* blobs here are small (floats) */
    hex_encode(hex, sizeof(hex), val, size);
    cJSON_DeleteItemFromObject(config_root, k);
    cJSON_AddStringToObject(config_root, k, hex);
    config_save();
    return true;
}

bool platform_storage_get_u8(const char *ns, const char *key, uint8_t *out)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON *item = cJSON_GetObjectItem(config_root, k);
    if (!cJSON_IsNumber(item)) return false;
    *out = (uint8_t)item->valueint;
    return true;
}

bool platform_storage_set_u8(const char *ns, const char *key, uint8_t val)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON_DeleteItemFromObject(config_root, k);
    cJSON_AddNumberToObject(config_root, k, val);
    config_save();
    return true;
}

bool platform_storage_get_i32(const char *ns, const char *key, int32_t *out)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON *item = cJSON_GetObjectItem(config_root, k);
    if (!cJSON_IsNumber(item)) return false;
    *out = (int32_t)item->valueint;
    return true;
}

bool platform_storage_set_i32(const char *ns, const char *key, int32_t val)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON_DeleteItemFromObject(config_root, k);
    cJSON_AddNumberToObject(config_root, k, val);
    config_save();
    return true;
}

bool platform_storage_erase_key(const char *ns, const char *key)
{
    config_load();
    char k[96]; make_key(k, sizeof(k), ns, key);
    cJSON_DeleteItemFromObject(config_root, k);
    config_save();
    return true;
}

/* ---------------- HTTP (stub: replay mode bypasses it) ---------------- */

int platform_http_get(const char *url, const char *const *headers, int n_headers,
                      char *buf, size_t buf_size, size_t *out_len)
{
    (void)url; (void)headers; (void)n_headers; (void)buf; (void)buf_size;
    if (out_len) *out_len = 0;
    platform_log(PLAT_LOG_WARN, "SIM", "platform_http_get stubbed (replay mode)");
    return -1;
}

int platform_http_post(const char *url, const char *const *headers, int n_headers,
                       const char *body, size_t body_len,
                       char *buf, size_t buf_size, size_t *out_len)
{
    (void)url; (void)headers; (void)n_headers; (void)body; (void)body_len; (void)buf; (void)buf_size;
    if (out_len) *out_len = 0;
    platform_log(PLAT_LOG_WARN, "SIM", "platform_http_post stubbed (replay mode)");
    return -1;
}

/* ---------------- WiFi (stubbed on desktop) ---------------- */

void platform_wifi_connect(const char *ssid, const char *password)
{ (void)ssid; (void)password; platform_log(PLAT_LOG_INFO, "SIM", "wifi connect (stubbed)"); }

void platform_wifi_disconnect(void) {}

void platform_forget_wifi_creds(void)
{
    platform_storage_erase_key("wifi", "ssid");
    platform_storage_erase_key("wifi", "pass");
}

void platform_forget_opensky_creds(void)
{
    platform_storage_erase_key("opensky", "client_id");
    platform_storage_erase_key("opensky", "client_secret");
}

void platform_restart(void)
{
    platform_log(PLAT_LOG_WARN, "SIM", "platform_restart (no-op in simulator)");
}