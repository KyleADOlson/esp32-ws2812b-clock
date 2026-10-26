#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "clock.h"

#define SETTINGS_MAGIC 0xC10C0001u

static const char *TAG = "settings";

clock_settings_t g_settings;

/* Each live change bumps s_gen; a save records the generation it wrote, so a change that
 * lands while a save is in progress still counts as unsaved. */
static volatile uint32_t s_gen, s_saved_gen;
static volatile int64_t  s_changed_us;

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
    s_saved_gen = s_gen;

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
    uint32_t gen = s_gen;
    nvs_handle_t h;
    esp_err_t err = nvs_open("clock", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "cfg", &g_settings, sizeof(g_settings));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) {
        s_saved_gen = gen;
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

void settings_changed(void)
{
    s_changed_us = esp_timer_get_time();
    s_gen++;
}

bool settings_dirty(void)
{
    return s_gen != s_saved_gen;
}

void settings_autosave(void)
{
    if (!settings_dirty()) return;
    int64_t now = esp_timer_get_time();
    if (now - s_changed_us < SETTINGS_AUTOSAVE_MS * 1000LL) return;
    if (settings_save() != ESP_OK) s_changed_us = now;   /* back off before retrying */
}

void settings_clear_wifi(void)
{
    g_settings.ssid[0] = '\0';
    g_settings.pass[0] = '\0';
    settings_save();
}
