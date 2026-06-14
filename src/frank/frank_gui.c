/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_gui.c — bridges b-em's X_GUI raw-row video path and audio output
 *               onto the frank HDMI 8-bit palette framebuffer.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * b-em (built with -DX_GUI -DSINGLE_CORE) rasterises each displayed scanline
 * synchronously on core0 into a flat 640-pixel RGB555 row (scanline_buffer->
 * row0).  We convert each row to an 8-bit BBC palette index and write it,
 * 2:1 horizontally downscaled, into the 320x256 framebuffer that the frank
 * HDMI encoder scans out from core1.
 */
#include <stdint.h>
#include <string.h>
#include "pico.h"

#include "x_gui.h"        /* struct scanvideo_scanline_buffer, RGB555 macros   */
#include "HDMI.h"         /* graphics_set_buffer                               */

#include "frank_gui.h"

/* The 320x256 8-bit framebuffers scanned out by the HDMI encoder. */
#define FB_W 320
#define FB_H 256
static uint8_t __attribute__((aligned(4))) screen_mem[2][FB_W * FB_H];
uint8_t *SCREEN[2] = { screen_mem[0], screen_mem[1] };
volatile uint32_t current_buffer = 0;

/* One reusable 640-wide RGB555 working row (+ a second for double-height). */
static uint16_t row_work0[640];
static uint16_t row_work1[640];
static struct scanvideo_scanline_buffer s_buffer;
static int s_scanline_number;

/*
 * Map an RGB555 pixel to a BBC physical-colour index 0..7.
 * BBC colours are full-on/off per channel; the ULA palette also produces
 * teletext anti-alias blends which we snap to the nearest primary.
 */
static inline uint8_t rgb555_to_bbc(uint16_t p) {
    uint8_t r = PICO_SCANVIDEO_R5_FROM_PIXEL(p);
    uint8_t g = PICO_SCANVIDEO_G5_FROM_PIXEL(p);
    uint8_t b = PICO_SCANVIDEO_B5_FROM_PIXEL(p);
    return (uint8_t)((r >= 16 ? 1 : 0) | (g >= 16 ? 2 : 0) | (b >= 16 ? 4 : 0));
}

/* Convert a 640-wide RGB555 row into an 8bpp framebuffer row (2:1 downscale). */
static void blit_row(const uint16_t *src, int y) {
    if (y < 0 || y >= FB_H) return;
    uint8_t *dst = SCREEN[current_buffer] + y * FB_W;
    for (int x = 0; x < FB_W; x++)
        dst[x] = rgb555_to_bbc(src[x * 2]);
}

/* ── x_gui video hooks called from b-em display.c ────────────────────────── */

int x_gui_init(void) {
    return 0;
}

/* display.c sets buffer->double_height before each scanline in teletext. */
struct scanvideo_scanline_buffer *x_gui_begin_scanline(void) {
    if (s_scanline_number == 0) {
        /* Start of a new frame: draw into the back buffer. */
        current_buffer ^= 1u;
    }
    s_buffer.row0 = row_work0;
    s_buffer.row1 = s_buffer.double_height ? row_work1 : NULL;
    s_buffer.scanline_id = s_scanline_number;
    /* Clear the work row so undriven pixels are black. */
    memset(row_work0, 0, sizeof(row_work0));
    if (s_buffer.row1) memset(row_work1, 0, sizeof(row_work1));
    return &s_buffer;
}

void x_gui_end_scanline(struct scanvideo_scanline_buffer *buffer) {
    int y = s_scanline_number;
    blit_row(buffer->row0, y);
    s_scanline_number++;
    if (s_scanline_number >= FB_H) {
        s_scanline_number = 0;
        /* Frame complete — present it to the HDMI encoder. */
        graphics_set_buffer(SCREEN[current_buffer]);
    }
}

void x_gui_refresh_menu_display(void) {
}
