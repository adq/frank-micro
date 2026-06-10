/*
 * frank-micro — BBC Micro for RP2350
 * micro_boot.c — Boot screen drawn before the emulator starts.
 */
#include "micro_boot.h"
#include "HDMI.h"
#include "ui_draw.h"
#include "ps2kbd_wrapper.h"
#include "usbhid_wrapper.h"
#include "crash_handler.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#ifndef FRANK_MICRO_VERSION
#define FRANK_MICRO_VERSION "dev"
#endif

extern uint8_t *SCREEN[2];
extern volatile uint32_t current_buffer;

#define FB_W  MICRO_FB_WIDTH
#define FB_H  MICRO_SCREEN_LINES

static void draw_line(uint8_t* fb, int x, int y, const char* text, uint8_t color) {
    int w = (int)strlen(text) * UI_CHAR_W;
    ui_fill_rect(fb, FB_W, x - 2, y - 1, w + 4, UI_CHAR_H + 2, UI_COLOR_BLACK);
    ui_draw_string(fb, FB_W, x, y, text, color);
}

/* ── Animated starfield welcome (same design as frank-cpc) ───────────────── */

#define BOOT_COLOR_BG        230
#define BOOT_COLOR_STAR_FAR  231
#define BOOT_COLOR_STAR_MID  232
#define BOOT_COLOR_STAR_NEAR 233
#define BOOT_COLOR_LOGO      234
#define BOOT_COLOR_LOGO_SH   235
#define BOOT_COLOR_SUBTLE    236
#define BOOT_COLOR_BLINK     237

static void install_boot_palette(void) {
    graphics_set_palette(BOOT_COLOR_BG,        0x0a0e1c);
    graphics_set_palette(BOOT_COLOR_STAR_FAR,  0x3a4056);
    graphics_set_palette(BOOT_COLOR_STAR_MID,  0x8088a8);
    graphics_set_palette(BOOT_COLOR_STAR_NEAR, 0xf0f0f0);
    graphics_set_palette(BOOT_COLOR_LOGO,      0xffffff);
    graphics_set_palette(BOOT_COLOR_LOGO_SH,   0x000000);
    graphics_set_palette(BOOT_COLOR_SUBTLE,    0x9fb2d0);
    graphics_set_palette(BOOT_COLOR_BLINK,     0xffd060);
}

static inline void fb_pixel(uint8_t *fb, int x, int y, uint8_t color) {
    if ((unsigned)x >= (unsigned)FB_W) return;
    if ((unsigned)y >= (unsigned)FB_H) return;
    fb[y * FB_W + x] = color;
}

static void fb_fill(uint8_t *fb, uint8_t color) {
    memset(fb, color, (size_t)FB_W * FB_H);
}

static void fb_text(uint8_t *fb, int x, int y, const char *s, uint8_t color) {
    ui_draw_string(fb, FB_W, x, y, s, color);
}

static void fb_text_center_shadow(uint8_t *fb, int y, const char *s,
                                  uint8_t fg, uint8_t sh) {
    int w = (int)strlen(s) * UI_CHAR_W;
    int x = (FB_W - w) / 2;
    fb_text(fb, x + 1, y + 1, s, sh);
    fb_text(fb, x, y, s, fg);
}

static void fb_char_scaled(uint8_t *fb, int x, int y, char c, int scale, uint8_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = ui_font_6x8[(int)c - 32];
    for (int row = 0; row < UI_CHAR_H; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < UI_CHAR_W; ++col) {
            if (bits & (0x80 >> col)) {
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        fb_pixel(fb, x + col * scale + dx, y + row * scale + dy, color);
            }
        }
    }
}

static int fb_text_scaled_width(const char *s, int scale) {
    return (int)strlen(s) * UI_CHAR_W * scale;
}

static void fb_text_scaled_center(uint8_t *fb, int y, const char *s, int scale,
                                  uint8_t fg, uint8_t sh) {
    int w = fb_text_scaled_width(s, scale);
    int x = (FB_W - w) / 2;
    for (size_t i = 0; s[i]; ++i) {
        int cx = x + (int)i * UI_CHAR_W * scale;
        fb_char_scaled(fb, cx + scale, y + scale, s[i], scale, sh);
        fb_char_scaled(fb, cx, y, s[i], scale, fg);
    }
}

static bool any_input_pressed(void) {
    bool pressed = false;
    int down;
    unsigned char sc;

    ps2kbd_tick();
    while (ps2kbd_get_key(&down, &sc)) {
        if (down) pressed = true;
    }

    usbhid_wrapper_tick();
    while (usbhid_wrapper_get_key(&down, &sc)) {
        if (down) pressed = true;
    }
    if (usbhid_wrapper_get_joystick() != 0) pressed = true;

    return pressed;
}

static void boot_flip(void) {
    graphics_set_buffer(SCREEN[current_buffer]);
    current_buffer ^= 1u;
}

static void sleep_vsync(void) {
    sleep_us(16666);
}

#define STAR_COUNT 48

typedef struct {
    int32_t x;
    int32_t y;
    int16_t vx;
    uint8_t color;
} star_t;

static uint32_t star_rng(uint32_t *state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static void init_starfield(star_t stars[STAR_COUNT], uint32_t *rng_state) {
    const uint8_t tier_color[3] = {
        BOOT_COLOR_STAR_FAR,
        BOOT_COLOR_STAR_MID,
        BOOT_COLOR_STAR_NEAR,
    };
    const int16_t tier_vx[3] = { -24, -64, -128 };
    for (int i = 0; i < STAR_COUNT; ++i) {
        int tier = (int)(star_rng(rng_state) % 3);
        stars[i].x = (int32_t)(star_rng(rng_state) % (FB_W << 8));
        stars[i].y = (int32_t)(star_rng(rng_state) % (FB_H << 8));
        stars[i].vx = tier_vx[tier];
        stars[i].color = tier_color[tier];
    }
}

static void tick_starfield(uint8_t *fb, star_t stars[STAR_COUNT]) {
    for (int i = 0; i < STAR_COUNT; ++i) {
        stars[i].x += stars[i].vx;
        if (stars[i].x < 0) stars[i].x += (FB_W << 8);
        fb_pixel(fb, stars[i].x >> 8, stars[i].y >> 8, stars[i].color);
    }
}

void micro_boot_welcome(uint32_t timeout_ms) {
    star_t stars[STAR_COUNT];
    uint32_t rng_state = 0xC0FFEE17u;

    install_boot_palette();
    init_starfield(stars, &rng_state);

    uint64_t t0 = time_us_64();
    uint32_t frame = 0;

    (void)any_input_pressed();

    /* The BBC text area is 256 lines tall; centre the layout vertically.
     * Logo at ~1/4, captions below, blink prompt near the bottom. */
    while (true) {
        uint64_t now = time_us_64();
        if ((now - t0) / 1000 >= timeout_ms) break;

        uint8_t *fb = SCREEN[current_buffer];
        fb_fill(fb, BOOT_COLOR_BG);
        tick_starfield(fb, stars);

        fb_text_scaled_center(fb, 60, "FRANK MICRO", 3, BOOT_COLOR_LOGO, BOOT_COLOR_LOGO_SH);

        char vline[24];
        snprintf(vline, sizeof(vline), "v%s", FRANK_MICRO_VERSION);
        fb_text_center_shadow(fb, 104, vline, BOOT_COLOR_SUBTLE, BOOT_COLOR_LOGO_SH);

        fb_text_center_shadow(fb, 124, "BBC Micro for RP2350", BOOT_COLOR_SUBTLE, BOOT_COLOR_LOGO_SH);
        fb_text_center_shadow(fb, 138, "by Mikhail Matveev", BOOT_COLOR_SUBTLE, BOOT_COLOR_LOGO_SH);
        fb_text_center_shadow(fb, 152, "github.com/rh1tech/frank-micro", BOOT_COLOR_SUBTLE, BOOT_COLOR_LOGO_SH);

        if (frame >= 60 && ((frame / 30) & 1u) == 0u) {
            fb_text_center_shadow(fb, 192, "PRESS ANY KEY", BOOT_COLOR_BLINK, BOOT_COLOR_LOGO_SH);
        }

        boot_flip();
        ++frame;

        crash_handler_feed();  /* keep watchdog alive during welcome screen */

        if (frame >= 30 && any_input_pressed()) break;
        sleep_vsync();
    }
}

void micro_boot_error(bool sd_ok, FRESULT sd_result) {
    /* Animated red-plasma error screen — same look as frank-cpc.
     * Shown when the SD card can't be mounted.  Never returns. */

    /* Sine LUT for plasma — in flash (static const) to avoid using RAM. */
    static const int8_t plasma_sin[256] = {
           0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
          49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
          90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
         117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
         127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
         117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
          90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
          49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
           0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
         -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
         -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
        -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
        -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
        -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
         -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
         -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
    };

    /* 64-color red plasma palette — black → deep red → bright red. */
    static const uint32_t plasma_pal[64] = {
        0x080000, 0x1a0000, 0x280000, 0x350000, 0x400100, 0x4b0100, 0x560200, 0x5f0300,
        0x690300, 0x720400, 0x7b0500, 0x830600, 0x8b0700, 0x920700, 0x990800, 0xa00900,
        0xa70a00, 0xad0b00, 0xb20b00, 0xb80c00, 0xbd0d00, 0xc10e00, 0xc50e00, 0xc90f00,
        0xcd0f00, 0xd01000, 0xd21000, 0xd41100, 0xd61100, 0xd81100, 0xd91100, 0xd91100,
        0xda1200, 0xd91100, 0xd91100, 0xd81100, 0xd61100, 0xd41100, 0xd21000, 0xd01000,
        0xcd0f00, 0xc90f00, 0xc50e00, 0xc10e00, 0xbd0d00, 0xb80c00, 0xb20b00, 0xad0b00,
        0xa70a00, 0xa00900, 0x990800, 0x920700, 0x8b0700, 0x830600, 0x7b0500, 0x720400,
        0x690300, 0x5f0300, 0x560200, 0x4b0100, 0x400100, 0x350000, 0x280000, 0x1a0000,
    };

    ui_draw_install_palette();

    /* Load plasma palette into slots 16-79 (emulator not running, free). */
    for (int i = 0; i < 64; i++)
        graphics_set_palette((uint8_t)(16 + i), plasma_pal[i]);

    uint8_t* fb = SCREEN[0];

    /* Draw plasma into the framebuffer background (one-time). */
    for (int py = 0; py < FB_H; py++) {
        for (int px = 0; px < FB_W; px++) {
            int v = (int)plasma_sin[(uint8_t)(px * 2)]
                  + (int)plasma_sin[(uint8_t)(py * 2)]
                  + (int)plasma_sin[(uint8_t)(px + py)]
                  + (int)plasma_sin[(uint8_t)(px - py)];
            fb[py * FB_W + px] = (uint8_t)(16 + (uint8_t)((v + 508) >> 4));
        }
    }
    graphics_set_buffer(fb);

    /* Draw error text on top — UI_COLOR_* indices unaffected by cycling. */
    const int x = 18;
    int y = 24;
    draw_line(fb, x, y, "frank-micro failed to start", UI_COLOR_FG);
    y += 18;

    if (!sd_ok || sd_result != FR_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), "SD card: could not be mounted (error %d)",
                 (int)sd_result);
        draw_line(fb, x, y, buf, UI_COLOR_ERROR);
        y += 10;
    } else {
        draw_line(fb, x, y, "SD card: OK", UI_COLOR_OK);
        y += 10;
    }

    y += 8;
    draw_line(fb, x, y, "The emulator could not be started.", UI_COLOR_FG);
    y += 10;
    draw_line(fb, x, y, "Please insert an SD card with /micro/,", UI_COLOR_FG);
    y += 10;
    draw_line(fb, x, y, "then reboot.", UI_COLOR_FG);

    y += 20;
    draw_line(fb, x, y, "github.com/rh1tech/frank-micro", UI_COLOR_DIM);
    y += 10;
    {
        char info[50];
        snprintf(info, sizeof(info), "frank-micro v%s | (c) 2026 Mikhail Matveev",
                 FRANK_MICRO_VERSION);
        draw_line(fb, x, y, info, UI_COLOR_DIM);
    }

    /* Animate: cycle palette phase each frame — zero extra RAM, ~30 fps. */
    uint8_t phase = 0;
    while (true) {
        for (int i = 0; i < 64; i++)
            graphics_set_palette((uint8_t)(16 + i), plasma_pal[(uint8_t)(i + phase) & 63]);
        phase++;
        sleep_ms(33);
    }
}
