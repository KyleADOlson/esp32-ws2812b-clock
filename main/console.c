/* Settings REPL on the USB serial port: `idf.py -p COMx monitor`, then type `help`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "clock.h"

static bool parse_int(const char *s, long lo, long hi, long *out)
{
    char *end;
    long v = strtol(s, &end, 0);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi) {
        printf("expected a number %ld..%ld, got '%s'\n", lo, hi, s);
        return false;
    }
    *out = v;
    return true;
}

static int parse_on_off(const char *s)
{
    if (!strcasecmp(s, "on") || !strcmp(s, "1") || !strcasecmp(s, "yes")) return 1;
    if (!strcasecmp(s, "off") || !strcmp(s, "0") || !strcasecmp(s, "no")) return 0;
    printf("expected on/off, got '%s'\n", s);
    return -1;
}

static void reboot_soon(void)
{
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        printf("usage: wifi <ssid> [password]   (quote names with spaces: wifi \"My Net\" \"pass\")\n");
        return 1;
    }
    const char *pass = argc == 3 ? argv[2] : "";
    if (strlen(argv[1]) > 32 || strlen(pass) > 64) {
        printf("SSID max 32 chars, password max 64 chars\n");
        return 1;
    }
    if (pass[0] && strlen(pass) < 8) printf("warning: WPA passwords are normally at least 8 chars\n");
    strlcpy(g_settings.ssid, argv[1], sizeof(g_settings.ssid));
    strlcpy(g_settings.pass, pass, sizeof(g_settings.pass));
    if (settings_save() != ESP_OK) return 1;
    printf("Saved Wi-Fi \"%s\". Rebooting to connect...\n", g_settings.ssid);
    reboot_soon();
    return 0;
}

static int cmd_wifi_reset(int argc, char **argv)
{
    settings_clear_wifi();
    printf("Wi-Fi forgotten. Rebooting into the setup portal...\n");
    reboot_soon();
    return 0;
}

static int cmd_tz(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: tz <name|POSIX TZ>\n  names:");
        for (size_t i = 0; i < TZ_PRESET_COUNT; i++) printf(" %s", TZ_PRESETS[i].name);
        printf("\n  e.g. tz eastern   or   tz \"EST5EDT,M3.2.0,M11.1.0\"\n  current: %s\n", g_settings.tz);
        return argc == 1 ? 0 : 1;
    }
    const char *tz = argv[1];
    for (size_t i = 0; i < TZ_PRESET_COUNT; i++) {
        if (!strcasecmp(argv[1], TZ_PRESETS[i].name)) tz = TZ_PRESETS[i].posix;
    }
    if (strlen(tz) >= sizeof(g_settings.tz)) {
        printf("TZ string too long\n");
        return 1;
    }
    strlcpy(g_settings.tz, tz, sizeof(g_settings.tz));
    g_tz_dirty = true;
    settings_save();
    printf("Time zone set to %s\n", g_settings.tz);
    return 0;
}

static int cmd_brightness(int argc, char **argv)
{
    long v;
    if (argc != 2) { printf("brightness: %u (1..255)\n", g_settings.brightness); return argc == 1 ? 0 : 1; }
    if (!parse_int(argv[1], 1, 255, &v)) return 1;
    g_settings.brightness = (uint8_t)v;
    settings_save();
    return 0;
}

static const struct { const char *name; uint8_t r, g, b; } NAMED_COLORS[] = {
    { "amber", 255, 96, 0 }, { "red", 255, 0, 0 }, { "green", 0, 255, 0 },
    { "blue", 0, 0, 255 }, { "white", 255, 255, 255 }, { "cyan", 0, 255, 255 },
    { "purple", 160, 0, 255 }, { "pink", 255, 40, 120 }, { "yellow", 255, 200, 0 },
};

static int cmd_color(int argc, char **argv)
{
    if (argc == 2) {
        for (size_t i = 0; i < sizeof(NAMED_COLORS) / sizeof(NAMED_COLORS[0]); i++) {
            if (!strcasecmp(argv[1], NAMED_COLORS[i].name)) {
                g_settings.r = NAMED_COLORS[i].r;
                g_settings.g = NAMED_COLORS[i].g;
                g_settings.b = NAMED_COLORS[i].b;
                settings_save();
                return 0;
            }
        }
    }
    if (argc != 4) {
        printf("usage: color <r> <g> <b>  or  color <name>\n  names:");
        for (size_t i = 0; i < sizeof(NAMED_COLORS) / sizeof(NAMED_COLORS[0]); i++) printf(" %s", NAMED_COLORS[i].name);
        printf("\n  current: %u %u %u\n", g_settings.r, g_settings.g, g_settings.b);
        return argc == 1 ? 0 : 1;
    }
    long r, g, b;
    if (!parse_int(argv[1], 0, 255, &r) || !parse_int(argv[2], 0, 255, &g) || !parse_int(argv[3], 0, 255, &b)) return 1;
    g_settings.r = r; g_settings.g = g; g_settings.b = b;
    settings_save();
    return 0;
}

static void print_layout(uint8_t l)
{
    printf("layout %u: %s, %s%s%s\n", l,
           (l & LAYOUT_ROW_MAJOR) ? "rows" : "columns",
           (l & LAYOUT_PROGRESSIVE) ? "progressive" : "zig-zag",
           (l & LAYOUT_FLIP_X) ? ", flip-x" : "",
           (l & LAYOUT_FLIP_Y) ? ", flip-y" : "");
}

static int cmd_layout(int argc, char **argv)
{
    long v;
    if (argc != 2) {
        print_layout(g_settings.layout);
        printf("usage: layout <0-15>  bits: 1=row-major 2=no zig-zag 4=flip-x 8=flip-y\n"
               "  run `test` and try values until the corners/text look right\n");
        return argc == 1 ? 0 : 1;
    }
    if (!parse_int(argv[1], 0, 15, &v)) return 1;
    g_settings.layout = (uint8_t)v;
    settings_save();
    print_layout(g_settings.layout);
    return 0;
}

static int set_flag(int argc, char **argv, uint8_t *flag)
{
    if (argc != 2) { printf("%s: %s\n", argv[0], *flag ? "on" : "off"); return argc == 1 ? 0 : 1; }
    int v = parse_on_off(argv[1]);
    if (v < 0) return 1;
    *flag = (uint8_t)v;
    settings_save();
    return 0;
}

static int cmd_ampm(int argc, char **argv)  { return set_flag(argc, argv, &g_settings.ampm); }

static int cmd_txpower(int argc, char **argv)
{
    long v;
    if (argc != 2) { printf("txpower: %d dBm (0 = default)\n", g_settings.txpower); return argc == 1 ? 0 : 1; }
    if (!parse_int(argv[1], 0, 20, &v)) return 1;
    g_settings.txpower = (int8_t)v;
    settings_save();
    net_apply_tx_power();
    if (v == 0) printf("Reboot to return to the default TX power.\n");
    return 0;
}

static int cmd_ntp(int argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) >= sizeof(g_settings.ntp)) {
        printf("usage: ntp <server>   current: %s\n", g_settings.ntp);
        return argc == 1 ? 0 : 1;
    }
    strlcpy(g_settings.ntp, argv[1], sizeof(g_settings.ntp));
    settings_save();
    printf("NTP server set to %s (takes effect after reboot)\n", g_settings.ntp);
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    char ip[20], when[48] = "(not synced)";
    net_get_ip(ip, sizeof(ip));
    if (g_time_synced) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(when, sizeof(when), "%Y-%m-%d %I:%M:%S %p %Z", &tm);
    }
    printf("Wi-Fi     : %s\n", g_settings.ssid[0] ? g_settings.ssid : "(not set)");
    printf("Mode      : %s\n", g_net_mode == NET_PORTAL ? "setup portal" :
                               g_wifi_connected ? "connected" : "connecting");
    printf("IP        : %s\n", ip);
    if (g_wifi_connected) printf("Web page  : http://led-clock.local/  or  http://%s/\n", ip);
    printf("Time      : %s\n", when);
    printf("Time zone : %s\n", g_settings.tz);
    printf("NTP       : %s\n", g_settings.ntp);
    printf("Color     : %u %u %u   brightness %u\n", g_settings.r, g_settings.g, g_settings.b, g_settings.brightness);
    printf("Display   : ampm %s, ", g_settings.ampm ? "on" : "off");
    print_layout(g_settings.layout);
    printf("TX power  : %d dBm (0 = default)\n", g_settings.txpower);
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    g_test_until_us = esp_timer_get_time() + 20LL * 1000 * 1000;
    printf("Test pattern for 20s: RED top-left, GREEN top-right, BLUE bottom-left,\n"
           "WHITE bottom-right, and \"TEST\" reading left to right.\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    reboot_soon();
    return 0;
}

void console_start(void)
{
    static const esp_console_cmd_t cmds[] = {
        { .command = "status",     .help = "Show Wi-Fi, time and display settings", .func = cmd_status },
        { .command = "wifi",       .help = "Set Wi-Fi network and reboot", .hint = "<ssid> [password]", .func = cmd_wifi },
        { .command = "wifi_reset", .help = "Forget Wi-Fi and reboot into the setup portal", .func = cmd_wifi_reset },
        { .command = "tz",         .help = "Set time zone (name or POSIX TZ string)", .hint = "<name|tz>", .func = cmd_tz },
        { .command = "brightness", .help = "Set brightness 1-255", .hint = "<1-255>", .func = cmd_brightness },
        { .command = "color",      .help = "Set digit color", .hint = "<r> <g> <b> | <name>", .func = cmd_color },
        { .command = "layout",     .help = "Set panel wiring (0-15)", .hint = "<0-15>", .func = cmd_layout },
        { .command = "ampm",       .help = "Show A/P indicator", .hint = "<on|off>", .func = cmd_ampm },
        { .command = "txpower",    .help = "Limit Wi-Fi TX power in dBm (try 8 if Wi-Fi is flaky)", .hint = "<0-20>", .func = cmd_txpower },
        { .command = "ntp",        .help = "Set NTP server", .hint = "<host>", .func = cmd_ntp },
        { .command = "test",       .help = "Show a wiring test pattern for 20 seconds", .func = cmd_test },
        { .command = "reboot",     .help = "Restart the clock", .func = cmd_reboot },
    };

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "clock> ";
    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl));

    esp_console_register_help_command();
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
