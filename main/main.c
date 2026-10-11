#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "clock.h"

static const char *TAG = "clock";

volatile bool    g_tz_dirty = true;
volatile int64_t g_test_until_us = 0;

/*
 * 12-hour layout, fixed-width 5x7 digits with 1px gaps, steady colon:
 *
 *   x: 1    7     13 15   21        28
 *      [H1] [H2]  :  [M1] [M2]      [A/P 3x5]
 *
 * Without the A/P indicator the time is centered instead.
 */
static void draw_time(const char d[4], char ap, rgb_t c)
{
    int x = g_settings.ampm ? 1 : 3;
    display_char(x, 0, d[0], c);
    display_char(x + 6, 0, d[1], c);
    display_pixel(x + 12, 2, c);
    display_pixel(x + 12, 4, c);
    display_char(x + 14, 0, d[2], c);
    display_char(x + 20, 0, d[3], c);
    if (g_settings.ampm && ap) display_small_char(x + 27, 2, ap, c);
}

static void draw_test_pattern(void)
{
    display_text_centered("TEST", 0, (rgb_t){ 255, 255, 255 });
    display_pixel(0, 0, (rgb_t){ 255, 0, 0 });
    display_pixel(PANEL_W - 1, 0, (rgb_t){ 0, 255, 0 });
    display_pixel(0, PANEL_H - 1, (rgb_t){ 0, 0, 255 });
    display_pixel(PANEL_W - 1, PANEL_H - 1, (rgb_t){ 255, 255, 255 });
}

static void render(void)
{
    rgb_t c = { g_settings.r, g_settings.g, g_settings.b };
    display_clear();

    if (esp_timer_get_time() < g_test_until_us) {
        draw_test_pattern();
    } else if (g_net_mode == NET_PORTAL) {
        display_text_centered("SETUP", 0, c);
    } else if (!g_time_synced) {
        draw_time("----", 0, c);
    } else {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        int h = tm.tm_hour % 12;
        if (h == 0) h = 12;
        const char d[4] = {
            h >= 10 ? '1' : ' ',
            (char)('0' + h % 10),
            (char)('0' + tm.tm_min / 10),
            (char)('0' + tm.tm_min % 10),
        };
        draw_time(d, tm.tm_hour < 12 ? 'A' : 'P', c);
    }
    display_flush();
}

static void check_reset_button(void)
{
    static int64_t pressed_since;
    if (gpio_get_level(RESET_BUTTON_GPIO) != 0) {
        pressed_since = 0;
        return;
    }
    int64_t now = esp_timer_get_time();
    if (!pressed_since) {
        pressed_since = now;
    } else if (now - pressed_since >= RESET_HOLD_MS * 1000LL) {
        ESP_LOGW(TAG, "reset button held: forgetting Wi-Fi");
        display_clear();
        display_text_centered("RESET", 0, (rgb_t){ 255, 0, 0 });
        display_flush();
        settings_clear_wifi();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

static void display_task(void *arg)
{
    for (;;) {
        if (g_tz_dirty) {
            g_tz_dirty = false;
            setenv("TZ", g_settings.tz, 1);
            tzset();
        }
        check_reset_button();
        render();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    settings_load();

    ESP_ERROR_CHECK(display_init());

    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << RESET_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);

    xTaskCreate(display_task, "display", 4096, NULL, 5, NULL);
    net_start();
    console_start();
}
