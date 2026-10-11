#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* ---- Hardware ---------------------------------------------------------- */
#define PANEL_W            32
#define PANEL_H            8
#define LED_GPIO           1      /* panel DIN */
#define RESET_BUTTON_GPIO  9      /* BOOT button on the C3 SuperMini */
#define RESET_HOLD_MS      3000   /* hold this long to forget Wi-Fi */

/* ---- Panel wiring (settings.layout bit flags) ---------------------------
 * Default 0 = pixels run in columns, zig-zagging, starting top-left.     */
#define LAYOUT_ROW_MAJOR   0x01   /* pixels run along rows instead of columns */
#define LAYOUT_PROGRESSIVE 0x02   /* every line runs the same way (no zig-zag) */
#define LAYOUT_FLIP_X      0x04   /* mirror left/right */
#define LAYOUT_FLIP_Y      0x08   /* mirror top/bottom */

/* ---- Settings (persisted in NVS) --------------------------------------- */
typedef struct {
    uint32_t magic;
    char     ssid[33];
    char     pass[65];
    char     tz[64];          /* POSIX TZ string */
    char     ntp[64];
    uint8_t  r, g, b;
    uint8_t  brightness;      /* 1..255, scales the color */
    uint8_t  layout;          /* LAYOUT_* flags */
    uint8_t  ampm;            /* show A/P indicator */
    uint8_t  reserved;        /* was: blink (kept so saved settings stay valid) */
    int8_t   txpower;         /* max Wi-Fi TX power in dBm, 0 = driver default */
} clock_settings_t;

extern clock_settings_t g_settings;
extern volatile bool    g_settings_dirty;   /* live changes not yet saved to flash */

void      settings_load(void);
esp_err_t settings_save(void);
void      settings_clear_wifi(void);

typedef struct { const char *name; const char *posix; } tz_preset_t;
extern const tz_preset_t TZ_PRESETS[];
extern const size_t TZ_PRESET_COUNT;

/* ---- Shared runtime state ---------------------------------------------- */
typedef enum { NET_STA, NET_PORTAL } net_mode_t;

extern volatile net_mode_t g_net_mode;
extern volatile bool       g_wifi_connected;
extern volatile bool       g_time_synced;
extern volatile bool       g_tz_dirty;        /* display task re-applies TZ */
extern volatile int64_t    g_test_until_us;   /* show test pattern until */

/* ---- Display ----------------------------------------------------------- */
typedef struct { uint8_t r, g, b; } rgb_t;

esp_err_t display_init(void);
void      display_clear(void);
void      display_pixel(int x, int y, rgb_t c);
void      display_char(int x, int y, char ch, rgb_t c);        /* 5x7 */
void      display_small_char(int x, int y, char ch, rgb_t c);  /* 3x5 */
void      display_text_centered(const char *s, int y, rgb_t c);
void      display_flush(void);

/* ---- Network ----------------------------------------------------------- */
void net_start(void);
void net_apply_tx_power(void);
void net_get_ip(char *buf, size_t len);
void portal_start(void);
void web_start(bool captive);
void web_scan(void);

/* ---- Console ----------------------------------------------------------- */
void console_start(void);
