/*
 * platform_esp32.c — ESP32-S3 implementation of the platform HAL.
 *
 * Thin wrappers over NVS, esp_http_client, esp_wifi, FreeRTOS tick and SNTP
 * time so the shared core can stay platform-agnostic.
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "lvgl_port.h"

/* ---------------- time ---------------- */

uint32_t platform_now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

bool platform_localtime_hm(char *out, size_t out_size)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 70) {                 /* not synced yet */
        snprintf(out, out_size, "--:--");
        return false;
    }
    snprintf(out, out_size, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return true;
}

/* ---------------- logging ---------------- */

void platform_log(int level, const char *tag, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    switch (level) {
    case PLAT_LOG_INFO:  ESP_LOGI(tag, "%s", buf); break;
    case PLAT_LOG_WARN:  ESP_LOGW(tag, "%s", buf); break;
    default:             ESP_LOGE(tag, "%s", buf); break;
    }
}

/* ---------------- LVGL lock ---------------- */

bool platform_lvgl_lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void platform_lvgl_unlock(void)
{
    lvgl_port_unlock();
}

/* ---------------- storage ---------------- */

bool platform_storage_get_str(const char *ns, const char *key, char *out, size_t out_size)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t need = out_size;
    esp_err_t e = nvs_get_str(h, key, out, &need);
    nvs_close(h);
    if (e == ESP_OK && out_size) out[out_size - 1] = '\0';
    return e == ESP_OK;
}

bool platform_storage_set_str(const char *ns, const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_set_str(h, key, val);
    esp_err_t e2 = nvs_commit(h);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

bool platform_storage_get_blob(const char *ns, const char *key, void *out, size_t *inout_size)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_blob(h, key, out, inout_size);
    nvs_close(h);
    return e == ESP_OK;
}

bool platform_storage_set_blob(const char *ns, const char *key, const void *val, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_set_blob(h, key, val, size);
    esp_err_t e2 = nvs_commit(h);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

bool platform_storage_get_u8(const char *ns, const char *key, uint8_t *out)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_u8(h, key, out);
    nvs_close(h);
    return e == ESP_OK;
}

bool platform_storage_set_u8(const char *ns, const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_set_u8(h, key, val);
    esp_err_t e2 = nvs_commit(h);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

bool platform_storage_get_i32(const char *ns, const char *key, int32_t *out)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e = nvs_get_i32(h, key, out);
    nvs_close(h);
    return e == ESP_OK;
}

bool platform_storage_set_i32(const char *ns, const char *key, int32_t val)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_set_i32(h, key, val);
    esp_err_t e2 = nvs_commit(h);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

bool platform_storage_erase_key(const char *ns, const char *key)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_erase_key(h, key);
    esp_err_t e2 = nvs_commit(h);
    nvs_close(h);
    return e1 == ESP_OK || e1 == ESP_ERR_NVS_NOT_FOUND;
}

/* ---------------- HTTP ---------------- */

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} http_ctx_t;

static esp_err_t http_evt_handler(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        if (ctx->len + evt->data_len + 1 > ctx->cap) return ESP_FAIL;
        memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
        ctx->len += evt->data_len;
        ctx->buf[ctx->len] = '\0';
    }
    return ESP_OK;
}

static int platform_http_perform(const char *url, const char *const *headers, int n_headers,
                                 const char *body, size_t body_len,
                                 char *buf, size_t buf_size, size_t *out_len,
                                 esp_http_client_method_t method)
{
    http_ctx_t ctx = { .buf = buf, .cap = buf_size, .len = 0 };
    buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_evt_handler,
        .user_data = &ctx,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 8192,
        .buffer_size_tx = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, method);

    /* headers[i] is "Name: value"; split at the first ':' and trim the value. */
    for (int i = 0; i < n_headers; i++) {
        const char *colon = strchr(headers[i], ':');
        if (!colon) continue;
        char name[64];
        size_t nlen = (size_t)(colon - headers[i]);
        if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, headers[i], nlen);
        name[nlen] = '\0';
        const char *val = colon + 1;
        while (*val == ' ') val++;
        esp_http_client_set_header(client, name, val);
    }

    if (method == HTTP_METHOD_POST && body)
        esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;
    esp_http_client_cleanup(client);

    if (out_len) *out_len = ctx.len;
    return status;
}

int platform_http_get(const char *url, const char *const *headers, int n_headers,
                      char *buf, size_t buf_size, size_t *out_len)
{
    return platform_http_perform(url, headers, n_headers, NULL, 0,
                                 buf, buf_size, out_len, HTTP_METHOD_GET);
}

int platform_http_post(const char *url, const char *const *headers, int n_headers,
                       const char *body, size_t body_len,
                       char *buf, size_t buf_size, size_t *out_len)
{
    return platform_http_perform(url, headers, n_headers, body, body_len,
                                 buf, buf_size, out_len, HTTP_METHOD_POST);
}

/* ---------------- WiFi ---------------- */

void platform_wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password));
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
}

void platform_wifi_disconnect(void)
{
    esp_wifi_disconnect();
}

void platform_forget_wifi_creds(void)
{
    platform_storage_erase_key("wifi", "ssid");
    platform_storage_erase_key("wifi", "pass");
    esp_wifi_disconnect();
}

void platform_forget_opensky_creds(void)
{
    platform_storage_erase_key("opensky", "client_id");
    platform_storage_erase_key("opensky", "client_secret");
}

void platform_restart(void)
{
    esp_restart();
}