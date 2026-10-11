#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "clock.h"

#define SETTINGS_MAGIC 0xC10C0001u

static const char *TAG = "settings";

clock_settings_t g_settings;
volatile bool g_settings_dirty;

const tz_preset_t TZ_PRESETS[] = {
    { "pacific",  "PST8PDT,M3.2.0,M11.1.0" },
    { "mountain", "MST7MDT,M3.2.0,M11.1.0" },
    { "arizona",  "MST7" },
    { "central",  "CST6CDT,M3.2.0,M11.1.0" },
    { "eastern",  "EST5EDT,M3.2.0,M11.1.0" },
    { "alaska",   "AKST9AKDT,M3.2.0,M11.1.0" },
    { "hawaii",   "HST10" },
    { "utc",      "UTC0" },
    { "london",   "GMT0BST,M3.5.0/1,M10.5.0" },
    { "europe",   "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "india",    "IST-5:30" },
    { "japan",    "JST-9" },
    { "sydney",   "AEST-10AEDT,M10.1.0,M4.1.0/3" },
};
const size_t TZ_PRESET_COUNT = sizeof(TZ_PRESETS) / sizeof(TZ_PRESETS[0]);

static void set_defaults(clock_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = SETTINGS_MAGIC;
    strlcpy(s->tz, TZ_PRESETS[0].posix, sizeof(s->tz));
    strlcpy(s->ntp, "pool.ntp.org", sizeof(s->ntp));
    s->r = 255; s->g = 96; s->b = 0;   /* amber */
    s->brightness = 32;
    s->layout = 0;
    s->ampm = 1;
    s->txpower = 0;
}

void settings_load(void)
{
    set_defaults(&g_settings);
    g_settings_dirty = false;

    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        return;
    }
    clock_settings_t tmp;
    size_t len = sizeof(tmp);
    if (nvs_get_blob(h, "cfg", &tmp, &len) == ESP_OK && len == sizeof(tmp) &&
        tmp.magic == SETTINGS_MAGIC) {
        g_settings = tmp;
        g_settings.ssid[sizeof(g_settings.ssid) - 1] = '\0';
        g_settings.pass[sizeof(g_settings.pass) - 1] = '\0';
        g_settings.tz[sizeof(g_settings.tz) - 1] = '\0';
        g_settings.ntp[sizeof(g_settings.ntp) - 1] = '\0';
        if (g_settings.brightness == 0) g_settings.brightness = 1;
    } else {
        ESP_LOGW(TAG, "saved settings missing or outdated, using defaults");
    }
    nvs_close(h);
}

esp_err_t settings_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("clock", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "cfg", &g_settings, sizeof(g_settings));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) g_settings_dirty = false;
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}

void settings_clear_wifi(void)
{
    g_settings.ssid[0] = '\0';
    g_settings.pass[0] = '\0';
    settings_save();
}
