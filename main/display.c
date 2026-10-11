#include <ctype.h>
#include <string.h>
#include "led_strip.h"
#include "esp_log.h"
#include "clock.h"

static const char *TAG = "display";

static led_strip_handle_t s_strip;
static rgb_t   s_fb[PANEL_W][PANEL_H];
static rgb_t   s_shown[PANEL_W][PANEL_H];
static uint8_t s_shown_brightness;
static uint8_t s_shown_layout;
static bool    s_shown_valid;

/* 5x7 glyphs, one byte per column, bit 0 = top row. */
typedef struct { char ch; uint8_t col[5]; } glyph5_t;

static const glyph5_t FONT5[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '-', { 0x08, 0x08, 0x08, 0x08, 0x08 } },
    { ':', { 0x00, 0x36, 0x36, 0x00, 0x00 } },
    { '0', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { '1', { 0x00, 0x42, 0x7F, 0x40, 0x00 } },
    { '2', { 0x42, 0x61, 0x51, 0x49, 0x46 } },
    { '3', { 0x21, 0x41, 0x45, 0x4B, 0x31 } },
    { '4', { 0x18, 0x14, 0x12, 0x7F, 0x10 } },
    { '5', { 0x27, 0x45, 0x45, 0x45, 0x39 } },
    { '6', { 0x3C, 0x4A, 0x49, 0x49, 0x30 } },
    { '7', { 0x01, 0x71, 0x09, 0x05, 0x03 } },
    { '8', { 0x36, 0x49, 0x49, 0x49, 0x36 } },
    { '9', { 0x06, 0x49, 0x49, 0x29, 0x1E } },
    { 'A', { 0x7E, 0x11, 0x11, 0x11, 0x7E } },
    { 'C', { 0x3E, 0x41, 0x41, 0x41, 0x22 } },
    { 'D', { 0x7F, 0x41, 0x41, 0x22, 0x1C } },
    { 'E', { 0x7F, 0x49, 0x49, 0x49, 0x41 } },
    { 'F', { 0x7F, 0x09, 0x09, 0x09, 0x01 } },
    { 'G', { 0x3E, 0x41, 0x49, 0x49, 0x7A } },
    { 'H', { 0x7F, 0x08, 0x08, 0x08, 0x7F } },
    { 'I', { 0x00, 0x41, 0x7F, 0x41, 0x00 } },
    { 'K', { 0x7F, 0x08, 0x14, 0x22, 0x41 } },
    { 'L', { 0x7F, 0x40, 0x40, 0x40, 0x40 } },
    { 'M', { 0x7F, 0x02, 0x0C, 0x02, 0x7F } },
    { 'N', { 0x7F, 0x04, 0x08, 0x10, 0x7F } },
    { 'O', { 0x3E, 0x41, 0x41, 0x41, 0x3E } },
    { 'P', { 0x7F, 0x09, 0x09, 0x09, 0x06 } },
    { 'R', { 0x7F, 0x09, 0x19, 0x29, 0x46 } },
    { 'S', { 0x46, 0x49, 0x49, 0x49, 0x31 } },
    { 'T', { 0x01, 0x01, 0x7F, 0x01, 0x01 } },
    { 'U', { 0x3F, 0x40, 0x40, 0x40, 0x3F } },
    { 'W', { 0x3F, 0x40, 0x38, 0x40, 0x3F } },
    { 'Y', { 0x07, 0x08, 0x70, 0x08, 0x07 } },
};

/* 3x5 glyphs for the AM/PM indicator, bit 0 = top row. */
static const uint8_t SMALL_A[3] = { 0x1E, 0x05, 0x1E };
static const uint8_t SMALL_P[3] = { 0x1F, 0x05, 0x02 };

static int xy_to_index(int x, int y, uint8_t layout)
{
    if (layout & LAYOUT_FLIP_X) x = PANEL_W - 1 - x;
    if (layout & LAYOUT_FLIP_Y) y = PANEL_H - 1 - y;
    bool zigzag = !(layout & LAYOUT_PROGRESSIVE);
    if (layout & LAYOUT_ROW_MAJOR) {
        if (zigzag && (y & 1)) x = PANEL_W - 1 - x;
        return y * PANEL_W + x;
    }
    if (zigzag && (x & 1)) y = PANEL_H - 1 - y;
    return x * PANEL_H + y;
}

esp_err_t display_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = PANEL_W * PANEL_H,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 96,   /* two RMT blocks: fewer refill IRQs, less Wi-Fi induced glitching */
        .flags.with_dma = false,   /* the C3's RMT has no DMA */
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }
    display_clear();
    display_flush();
    return ESP_OK;
}

void display_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

void display_pixel(int x, int y, rgb_t c)
{
    if (x < 0 || x >= PANEL_W || y < 0 || y >= PANEL_H) return;
    s_fb[x][y] = c;
}

static void draw_columns(int x, int y, const uint8_t *cols, int ncols, int nrows, rgb_t c)
{
    for (int i = 0; i < ncols; i++) {
        for (int row = 0; row < nrows; row++) {
            if (cols[i] & (1 << row)) display_pixel(x + i, y + row, c);
        }
    }
}

void display_char(int x, int y, char ch, rgb_t c)
{
    ch = (char)toupper((unsigned char)ch);
    for (size_t i = 0; i < sizeof(FONT5) / sizeof(FONT5[0]); i++) {
        if (FONT5[i].ch == ch) {
            draw_columns(x, y, FONT5[i].col, 5, 7, c);
            return;
        }
    }
}

void display_small_char(int x, int y, char ch, rgb_t c)
{
    if (ch == 'A') draw_columns(x, y, SMALL_A, 3, 5, c);
    else if (ch == 'P') draw_columns(x, y, SMALL_P, 3, 5, c);
}

void display_text_centered(const char *s, int y, rgb_t c)
{
    int n = (int)strlen(s);
    int x = (PANEL_W - (n * 6 - 1)) / 2;
    for (int i = 0; i < n; i++, x += 6) display_char(x, y, s[i], c);
}

void display_flush(void)
{
    uint8_t br = g_settings.brightness;
    uint8_t layout = g_settings.layout;
    if (s_shown_valid && br == s_shown_brightness && layout == s_shown_layout &&
        memcmp(s_fb, s_shown, sizeof(s_fb)) == 0) {
        return;
    }
    for (int x = 0; x < PANEL_W; x++) {
        for (int y = 0; y < PANEL_H; y++) {
            rgb_t c = s_fb[x][y];
            led_strip_set_pixel(s_strip, xy_to_index(x, y, layout),
                                c.r * br / 255, c.g * br / 255, c.b * br / 255);
        }
    }
    led_strip_refresh(s_strip);
    memcpy(s_shown, s_fb, sizeof(s_fb));
    s_shown_brightness = br;
    s_shown_layout = layout;
    s_shown_valid = true;
}
