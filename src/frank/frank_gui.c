/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_gui.c — bridges b-em's X_GUI raw-row video path and audio output
 *               onto the frank HDMI 8-bit palette framebuffer.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
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
#include "frank_ui.h"
#include "frank_screenshot.h"

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

/*
 * Frame/scanline bookkeeping, faithfully matching the upstream x_gui:
 *   - scanline_number counts begin_scanline() calls 1..256 within a frame.
 *   - frame_number_part increments by 0x10000 at the start of each frame.
 *   - scanline_id = frame_number_part | scanline_number.
 * display.c's per-line catch-up logic uses scanvideo_frame_number(scanline_id)
 * to detect a new frame and scanvideo_scanline_number() for the line position,
 * padding each frame to exactly 256 emitted scanlines.  Encoding the frame
 * number is essential: without it, when a program reprograms the CRTC (e.g.
 * the Prince of Persia cut scenes) the scanline accounting desyncs and the
 * frame never completes, freezing the display while the CPU keeps running.
 */
static int      s_scanline_number;     /* 0..256 within the current frame */
static uint32_t s_frame_number_part;   /* high 16 bits of scanline_id      */

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

/* Luminance proxy: brightest channel of an RGB555 pixel (0..31). */
static inline uint8_t lum5(uint16_t p) {
    uint8_t r = PICO_SCANVIDEO_R5_FROM_PIXEL(p);
    uint8_t g = PICO_SCANVIDEO_G5_FROM_PIXEL(p);
    uint8_t b = PICO_SCANVIDEO_B5_FROM_PIXEL(p);
    uint8_t m = r > g ? r : g;
    return m > b ? m : b;
}

/*
 * Render one MODE 7 teletext scanline pixel-perfectly.
 *
 * The engine emits each 40-column teletext line into a 640-wide row: a fixed
 * 16 sub-pixels per character (char c → src[c*16 .. c*16+15]).  Those 16
 * sub-pixels are the engine's interpolation of the 6-column SAA5050 glyph cell
 * (the original 6 columns doubled to 12, then linearly resampled 12→16).
 *
 * Tracing that 12→16 resample shows the six original font columns reappear at
 * full intensity exactly at sub-pixel offsets {0,2,5,8,11,14}; every other
 * offset is an interpolated blend (or character-rounding bleed in the other
 * field).  Sampling only those six points therefore recovers the original
 * SAA5050 glyph with no blur, giving uniform 1-px stems for every character.
 * We place the 6 columns 1:1 into the 8-px output cell with a 1-px margin
 * either side.  Colours come from the sampled pixels, so coloured teletext and
 * coloured backgrounds are preserved.
 */
#define TELETEXT_INK_LUM 16   /* original column is full-on (≈31) or off (0) */

static void blit_row_teletext(const uint16_t *src, uint8_t *dst) {
    /* Sub-pixel offset of each original glyph column within the 16-wide cell. */
    static const uint8_t col_off[6] = { 0, 2, 5, 8, 11, 14 };

    for (int c = 0; c < FB_W / 8; c++) {       /* 40 character cells */
        const uint16_t *cell = src + c * 16;
        uint8_t *o = dst + c * 8;

        /* Cell background = darkest sampled column (covers coloured bg). */
        uint16_t bgpx = cell[col_off[0]];
        uint8_t  bglum = lum5(cell[col_off[0]]);
        for (int i = 1; i < 6; i++) {
            uint8_t l = lum5(cell[col_off[i]]);
            if (l < bglum) { bglum = l; bgpx = cell[col_off[i]]; }
        }
        uint8_t bg = rgb555_to_bbc(bgpx);

        o[0] = bg;                              /* left inter-char margin */
        for (int i = 0; i < 6; i++) {
            uint16_t px = cell[col_off[i]];
            o[i + 1] = (lum5(px) >= TELETEXT_INK_LUM && lum5(px) > bglum)
                       ? rgb555_to_bbc(px) : bg;
        }
        o[7] = bg;                              /* right inter-char margin */
    }
}

/*
 * Convert a 640-wide RGB555 row into an 8bpp framebuffer row (2:1 downscale).
 *
 * Graphics modes already emit ≤320 distinct, full-on colours into the row, so
 * point-sampling every other pixel is lossless there (games look sharp).
 * MODE 7 teletext gets the dedicated crisp renderer above.
 */
static void blit_row(const uint16_t *src, int y, bool teletext) {
    if (y < 0 || y >= FB_H) return;
    uint8_t *dst = SCREEN[current_buffer] + y * FB_W;
    if (teletext) {
        blit_row_teletext(src, dst);
    } else {
        for (int x = 0; x < FB_W; x++)
            dst[x] = rgb555_to_bbc(src[x * 2]);
    }
}

/* ── x_gui video hooks called from b-em display.c ────────────────────────── */

int x_gui_init(void) {
    return 0;
}

/* display.c sets buffer->double_height before each scanline in teletext. */
struct scanvideo_scanline_buffer *x_gui_begin_scanline(void) {
    if (s_scanline_number++ == 0) {
        /* Start of a new frame: draw into the back buffer, bump frame number. */
        current_buffer ^= 1u;
        s_frame_number_part += 0x10000u;
    }
    s_buffer.row0 = row_work0;
    s_buffer.row1 = s_buffer.double_height ? row_work1 : NULL;
    s_buffer.scanline_id = s_frame_number_part | (uint32_t)s_scanline_number;
    /* Clear the work row so undriven pixels are black. */
    memset(row_work0, 0, sizeof(row_work0));
    if (s_buffer.row1) memset(row_work1, 0, sizeof(row_work1));
    return &s_buffer;
}

void x_gui_end_scanline(struct scanvideo_scanline_buffer *buffer) {
    /* scanline_number is 1-based here (post-increment in begin); the row index
     * on screen is scanline_number-1.  double_height is set by display.c only
     * for MODE 7 teletext scanlines, which need the box-filtered downscale. */
    blit_row(buffer->row0, s_scanline_number - 1, buffer->double_height);
    if (s_scanline_number >= FB_H) {
        s_scanline_number = 0;
        /* Deferred screenshot: capture the clean BBC frame before any overlay
         * is drawn (raised by the PrintScreen key or the F12 menu). */
        if (g_frank_screenshot_pending) {
            g_frank_screenshot_pending = false;
            int rc = frank_screenshot_save(SCREEN[current_buffer]);
            frank_ui_toast(rc == 0 ? "Screenshot saved" : "Screenshot failed");
        }
        /* Overlay the settings/browser UI onto the freshly-rendered frame. */
        if (frank_ui_is_visible())
            frank_ui_render(SCREEN[current_buffer], FB_W, FB_H);
        /* Frame complete — present it to the HDMI encoder. */
        graphics_set_buffer(SCREEN[current_buffer]);
    }
}

void x_gui_refresh_menu_display(void) {
}
