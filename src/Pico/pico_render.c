/*
 * frank-micro — BBC Micro for RP2350
 * pico_render.c — BBC video renderer targeting the HDMI 320×256 8-bit
 *                 indexed framebuffer.
 *
 * The BBC Micro produces one of several video modes:
 *  · 2MHz modes (80 cols): MODE 0 (2-colour), MODE 3 (text)
 *  · 1MHz modes (40/20 cols): MODE 1 (4-col), MODE 2 (16-col), MODE 4
 *    (2-col), MODE 5 (4-col)
 *  · Teletext: MODE 7
 *
 * We write into SCREEN[current_buffer] (320×256 8-bit indexed) which is
 * the buffer scanned out by the HDMI PIO.
 *
 * Rendering at 320 wide:
 *  - 2MHz path: each render_render() covers 8 2MHz pixels → output 4 pixels
 *  - 1MHz path: each render_render() covers 8 1MHz clocks = 16 pixel widths
 *    → output 8 pixels (or 4 for 20-col modes)
 *
 * Palette: BBC physical colours 0-7 → palette indices 0-7.
 * Flash colours add offset 8 (0-15 total).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "render.h"
#include "teletext.h"
#include "bbc_options.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#ifdef PICO_BUILD
#include "pico.h"
#endif
#ifndef FRANK_FAST_FUNC
#ifdef PICO_BUILD
#define FRANK_FAST_FUNC(decl) __not_in_flash_func(decl)
#else
#define FRANK_FAST_FUNC(decl) decl
#endif
#endif

/* Forbid FP/SIMD register use in the integer-only pixel render hot path; see
 * the matching note in video.c. Prevents GCC from spilling 64-bit pixel-LUT
 * values into FP register d8, whose access NOCP-faults when the FPU
 * coprocessor is not enabled (crash entering POP's cut scene). */
#ifdef PICO_BUILD
#define FRANK_NO_FPU __attribute__((target("general-regs-only")))
#else
#define FRANK_NO_FPU
#endif

/* Map a teletext RGBA host pixel (0xAARRGGBB) to a BBC physical colour
 * index 0-7.  BBC encoding: bit0=red, bit1=green, bit2=blue. */
static inline uint8_t rgb888_to_bbc(uint32_t px) {
    uint8_t r = (px >> 16) & 0xff;
    uint8_t g = (px >> 8)  & 0xff;
    uint8_t b =  px        & 0xff;
    return (uint8_t)((r >= 128 ? 1 : 0) |
                     (g >= 128 ? 2 : 0) |
                     (b >= 128 ? 4 : 0));
}

/* Teletext anti-aliasing palette layout.  The SAA5050 emits smoothed
 * (grayscale-blended) glyph edges; to preserve them on our 320-wide
 * indexed framebuffer we reserve a ramp of palette entries:
 *   index = TT_AA_BASE + (colour-1)*TT_AA_LEVELS + level
 * for colour 1..7 and level 0..(TT_AA_LEVELS-1), each a fractional
 * blend from black to the BBC colour.  micro_init_palette() installs
 * the matching RGB values (see platform.c). */
#define TT_AA_BASE   64
#define TT_AA_LEVELS 8

/* Map an averaged RGB888 teletext pixel to an anti-aliasing palette index. */
static inline uint8_t tt_aa_index(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t m = r; if (g > m) m = g; if (b > m) m = b;
    if (m < 24) return 0;                       /* background → black */
    /* Determine which colour channels are significant (≥ 1/3 of peak). */
    int c = (r * 3 >= m ? 1 : 0) |
            (g * 3 >= m ? 2 : 0) |
            (b * 3 >= m ? 4 : 0);
    if (c == 0) c = 7;
    int lvl = (int)((m * (TT_AA_LEVELS - 1)) / 255);
    if (lvl < 1) lvl = 1;
    if (lvl > TT_AA_LEVELS - 1) lvl = TT_AA_LEVELS - 1;
    return (uint8_t)(TT_AA_BASE + (c - 1) * TT_AA_LEVELS + lvl);
}

/* HDMI screen dimensions. */
#define SCR_W  320
#define SCR_H  256

/* BBC physical colour count. */
#define BBC_NCOLOURS 8

/* ── External SCREEN buffer (defined in main.c) ──────────────────────────── */
extern uint8_t *SCREEN[2];
extern volatile uint32_t current_buffer;


/* ── Render state ─────────────────────────────────────────────────────────── */
struct render_struct {
    struct teletext_struct* p_teletext;

    /* Output pixel position. */
    int x;          /* current x, 0..319 */
    int y;          /* current y, 0..255 */

    /* Interlace de-jitter: the BBC alternates odd/even fields with a
     * half-line offset, which makes the number of blanking scanlines
     * between vsync and the first active line alternate by 1.  We latch
     * the first active line of each frame to a fixed top so content sits
     * on a stable y-grid (no vertical shimmer). */
    int y_base;        /* subtracted from raw y when writing pixels */
    int top_latched;   /* set once per frame at first active scanline */

    /* Cursor: video.c calls render_cursor() just before the render_render()
     * for the cell under the cursor.  Since our renderer overwrites the cell,
     * we latch a pending flag and XOR the cell *after* drawing it.  The BBC
     * only calls render_cursor() on "cursor visible" frames, so this blinks
     * naturally. */
    int cursor_pending;

    /* Video mode flags (set by render_set_mode). */
    int is_clock_2MHz;    /* 1 = 2MHz (mode 0/3), 0 = 1MHz */
    int chars_per_line;   /* 80 or 40 or 20 (80 cols for 2MHz, 40 for 1MHz) */
    int is_teletext;

    /* Flash state. */
    int flash_on;

    /* Cursor. */
    int cursor_segments[4];   /* s0..s3 */
    uint32_t row_address;     /* RA from CRTC */

    /* Display enable. */
    int dispen;

    /* Physical colour lookup:
     * physical_lut[logical_colour] = physical colour index 0-7. */
    uint8_t physical_lut[16]; /* 4-bit logical -> 3-bit physical */

    /* Per-bit pixel colour lookup tables.
     * 2MHz mode: each byte = 8 pixels (bit7 = px0, bit6 = px1, ...)
     *            each bit selects physical_lut[0] or physical_lut[1].
     * pixel_lut_2mhz[byte] → 4 output bytes (4-pixel packed) */
    /* 1MHz mode (4-colour): bits interleaved */
    /* We build these per-mode tables on demand. */

    int  table_dirty;
    /* Pixel tables: precomputed output byte for each data byte.
     * For 2MHz (4 output pixels per byte): lut2[256] -> 4 bytes in uint32_t
     * For 1MHz (8 output pixels per byte): lut1[256] -> 8 bytes in uint64_t */
    uint32_t lut2[256];   /* 2MHz mode: 8 BBC pixels → 4 output pixels */
    uint64_t lut1[256];   /* 1MHz mode: 8 chars → 8 output pixels */
    uint64_t lut1_20[256];/* 1MHz 20-col mode: 8 chars → 4 output pixels (x2) */

    /* Teletext per-row pixel buffer, 40×12 characters. */
    uint8_t  tt_row[40 * 2 * 8];   /* 40 chars × 2 bytes/char × 8 rows */

    /* Flyback callback. */
    void (*p_flyback_callback)(void*);
    void* p_flyback_object;

    /* Buffer pointer (unused on Pico - we use SCREEN[] directly). */
    uint32_t* p_buf;
    int has_buf;

    /* Dimensions (returned to caller). */
    uint32_t width;
    uint32_t height;
};

/* ── Colour palette ──────────────────────────────────────────────────────── */
/* BBC physical colours (index 0-7 → RRGGBB) - also set by graphics_set_palette. */
static const uint32_t k_bbc_colours[8] = {
    0x000000, /* 0 BLACK   */
    0xFF0000, /* 1 RED     */
    0x00FF00, /* 2 GREEN   */
    0xFFFF00, /* 3 YELLOW  */
    0x0000FF, /* 4 BLUE    */
    0xFF00FF, /* 5 MAGENTA */
    0x00FFFF, /* 6 CYAN    */
    0xFFFFFF, /* 7 WHITE   */
};

/* HDMI palette is configured at init time by micro_init_palette(). */

/* ── Pixel table rebuild ─────────────────────────────────────────────────── */
/*
 * 2MHz mode (modes 0/3): bit7..bit0 = pixels 0..7.
 * Each bit maps: 1 → physical_lut[1], 0 → physical_lut[0].
 * Output: 4 bytes (pixels 0,2,4,6 — take every other for 2:1 downsample).
 */
static void rebuild_lut2(struct render_struct* p) {
    uint8_t c0 = (p->physical_lut[0] ^ 7) & 7;
    uint8_t c1 = (p->physical_lut[1] ^ 7) & 7;
    for (int byte = 0; byte < 256; byte++) {
        uint32_t out = 0;
        /* Take pixels 0,2,4,6 (bits 7,5,3,1). */
        out  = ((byte >> 7) & 1) ? c1 : c0;
        out |= (uint32_t)(((byte >> 5) & 1) ? c1 : c0) << 8;
        out |= (uint32_t)(((byte >> 3) & 1) ? c1 : c0) << 16;
        out |= (uint32_t)(((byte >> 1) & 1) ? c1 : c0) << 24;
        p->lut2[byte] = out;
    }
}

/*
 * 1MHz 40-col mode (4-colour): bit pairs map to 4 logical colours.
 * BBC interleaving: colour = {bit7,bit3} for pixel 0, {bit6,bit2} for pixel 1, etc.
 * Each logical colour → physical_lut[log_col].
 * Output: 8 bytes (8 pixels).
 */
static void rebuild_lut1_4col(struct render_struct* p) {
    for (int byte = 0; byte < 256; byte++) {
        uint64_t out = 0;
        for (int px = 0; px < 4; px++) {
            int bit_hi = 7 - px;
            int bit_lo = 3 - px;
            int log_col = (((byte >> bit_hi) & 1) << 1) | ((byte >> bit_lo) & 1);
            uint8_t phys = (p->physical_lut[log_col] ^ 7) & 7;
            /* Each logical pixel maps to 2 output pixels. */
            out |= (uint64_t)phys << (px * 16);
            out |= (uint64_t)phys << (px * 16 + 8);
        }
        p->lut1[byte] = out;
    }
}

/*
 * 1MHz 80-col 2-colour mode (MODE 4): same as 2MHz but outputs 8 pixels.
 * Each bit → 1 output pixel.
 */
static void rebuild_lut1_2col(struct render_struct* p) {
    uint8_t c0 = (p->physical_lut[0] ^ 7) & 7;
    uint8_t c1 = (p->physical_lut[1] ^ 7) & 7;
    for (int byte = 0; byte < 256; byte++) {
        uint64_t out = 0;
        for (int bit = 0; bit < 8; bit++) {
            uint8_t col = ((byte >> (7 - bit)) & 1) ? c1 : c0;
            out |= (uint64_t)col << (bit * 8);
        }
        p->lut1[byte] = out;
    }
}

/*
 * 1MHz 20-col 16-colour mode (MODE 2): nibble encoding.
 * 2 pixels per byte; each uses 4 physical bits.
 * BBC encoding: {bit7,bit5,bit3,bit1} = pixel 0 (4-bit logical colour)
 *               {bit6,bit4,bit2,bit0} = pixel 1.
 * Output: 4 bytes (2 pixels × 2 output pixels each).
 */
static void rebuild_lut1_16col(struct render_struct* p) {
    for (int byte = 0; byte < 256; byte++) {
        /* Logical colour bit order must match the authoritative beebjit
         * decoder (render.c render_generate_1MHz_table): logical bit0 = data
         * bit1, bit1 = data bit3, bit2 = data bit5, bit3 = data bit7 for the
         * left pixel; the right pixel uses the even data bits. */
        int log0 = ((byte >> 1) & 1) | (((byte >> 3) & 1) << 1) |
                   (((byte >> 5) & 1) << 2) | (((byte >> 7) & 1) << 3);
        int log1 = ((byte >> 0) & 1) | (((byte >> 2) & 1) << 1) |
                   (((byte >> 4) & 1) << 2) | (((byte >> 6) & 1) << 3);
        uint8_t p0 = (p->physical_lut[log0] ^ 7) & 7;
        uint8_t p1 = (p->physical_lut[log1] ^ 7) & 7;
        uint64_t out = (uint64_t)p0 | ((uint64_t)p0 << 8) |
                       ((uint64_t)p1 << 16) | ((uint64_t)p1 << 24);
        p->lut1_20[byte] = out;
    }
}

static void rebuild_pixel_tables(struct render_struct* p) {
    if (p->is_teletext) { p->table_dirty = 0; return; }
    if (p->is_clock_2MHz) {
        /* 2MHz modes — chars_per_line (raw ULA value 0-3):
         * 3=MODE0(80col,2clr), 2=MODE1(40col,4clr), 1=MODE2(20col,16clr), 0=unusual */
        switch (p->chars_per_line) {
        case 3: rebuild_lut2(p);         break;  /* MODE0: 2-color */
        case 2: rebuild_lut1_4col(p);    break;  /* MODE1: 4-color */
        case 1: rebuild_lut1_16col(p);   break;  /* MODE2: 16-color */
        default: rebuild_lut2(p);        break;
        }
    } else {
        /* 1MHz modes — chars_per_line (raw ULA value 0-3):
         * 3=MODE4_80, 2=MODE4(40col,2clr), 1=MODE5(20col,4clr), 0=unusual */
        switch (p->chars_per_line) {
        case 3: rebuild_lut1_2col(p);   break;
        case 2: rebuild_lut1_2col(p);   break;
        case 1: rebuild_lut1_4col(p);   break;
        case 0: rebuild_lut1_16col(p);  break;
        default: rebuild_lut1_2col(p);  break;
        }
    }
    p->table_dirty = 0;
}

/* ── render.h interface ─────────────────────────────────────────────────── */

struct render_struct* render_create(struct teletext_struct* p_teletext,
                                     struct bbc_options* p_options) {
    (void)p_options;
    struct render_struct* p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->p_teletext = p_teletext;
    p->width  = SCR_W;
    p->height = SCR_H;

    /* Physical colour LUT mirrors video.c's ula_palette, which resets to all
     * zero (video_ula_power_on_reset). video_ula_write_palette skips the
     * renderer update when the new value equals the cached ula_palette entry,
     * so physical_lut MUST start in the same all-zero state or the two copies
     * desync and palette writes get silently dropped. */
    for (int i = 0; i < 16; i++)
        p->physical_lut[i] = 0;

    p->is_clock_2MHz  = 0;
    p->chars_per_line = 40;
    p->table_dirty    = 1;
    p->dispen         = 1;

    return p;
}

void render_destroy(struct render_struct* p) { free(p); }

void render_power_on_reset(struct render_struct* p) {
    p->x = 0; p->y = 0;
    p->dispen = 0;
    p->flash_on = 0;
    /* Reset to all-zero to stay in lockstep with video.c's ula_palette reset;
     * see the matching note in render_create(). */
    memset(p->physical_lut, 0, sizeof(p->physical_lut));
    p->table_dirty = 1;
}

void render_set_flyback_callback(struct render_struct* p,
                                  void (*cb)(void*), void* obj) {
    p->p_flyback_callback = cb;
    p->p_flyback_object   = obj;
}

uint32_t render_get_width(struct render_struct* p)       { return p->width; }
uint32_t render_get_height(struct render_struct* p)      { return p->height; }
uint32_t render_get_buffer_size(struct render_struct* p) { return p->width * p->height * 4; }
uint32_t render_get_horiz_pos(struct render_struct* p)   { return (uint32_t)p->x; }
uint32_t render_get_vert_pos(struct render_struct* p)    { return (uint32_t)p->y; }
uint32_t render_get_buffer_crc32(struct render_struct* p){ (void)p; return 0; }

uint32_t* render_get_buffer(struct render_struct* p) { return p->p_buf; }

void render_set_buffer(struct render_struct* p, uint32_t* p_buf) {
    p->p_buf  = p_buf;
    p->has_buf = (p_buf != NULL) ? 1 : 0;
}

int render_has_buffer(struct render_struct* p) { return 1; /* always have SCREEN[] */ }

void render_create_internal_buffer(struct render_struct* p) {
    /* No internal buffer needed — we render directly to SCREEN[]. */
    (void)p;
}

void render_set_mode(struct render_struct* p,
                      int clock_speed,
                      int chars_per_line,
                      int is_teletext) {
    p->is_clock_2MHz  = clock_speed;
    p->chars_per_line = chars_per_line;
    p->is_teletext    = is_teletext;
    p->table_dirty    = 1;
}

void render_set_flash(struct render_struct* p, int is_flash) {
    if (p->flash_on != is_flash) {
        p->flash_on    = is_flash;
        p->table_dirty = 1;
    }
}

void render_set_physical_color(struct render_struct* p,
                                 uint8_t logical_color,
                                 uint8_t physical_color) {
    if (logical_color < 16) {
        p->physical_lut[logical_color] = physical_color & 15;
        p->table_dirty = 1;
    }
}

void render_set_palette(struct render_struct* p,
                          uint8_t index,
                          uint32_t rgba) {
    /* The HDMI palette is managed by the HDMI driver directly
     * via micro_init_palette().  Colour mapping is physical 0-7.
     * rgba is ARGB32 from beebjit — we can ignore it, the HDMI palette
     * is set up from the known BBC colour table.*/
    (void)p; (void)index; (void)rgba;
}

void render_set_cursor_segments(struct render_struct* p,
                                  int s0, int s1, int s2, int s3) {
    p->cursor_segments[0] = s0;
    p->cursor_segments[1] = s1;
    p->cursor_segments[2] = s2;
    p->cursor_segments[3] = s3;
}

void render_set_DISPEN(struct render_struct* p, int is_enabled) {
    /* On the rising edge of DISPEN (start of the active region on a line)
     * snap the output x back to the left edge so the 40/80-column active
     * area aligns to our 320-wide buffer with no left border.  In teletext
     * mode the SAA5050 pipeline delays glyph output by 3 character clocks,
     * so we pre-bias x by -3 chars (24 px) to compensate. */
    if (is_enabled && !p->dispen) {
        p->x = p->is_teletext ? -(2 * 8) : 0;
        /* Latch the top of the active region once per frame so interlace
         * half-line jitter doesn't shift content vertically. */
        if (!p->top_latched) {
            p->y_base = p->y;
            p->top_latched = 1;
        }
    }
    p->dispen = is_enabled;
}

void render_set_RA(struct render_struct* p, uint32_t row_address) {
    p->row_address = row_address;
}

void render_prepare(struct render_struct* p) {
    if (p->table_dirty) rebuild_pixel_tables(p);
}

void render_force_table_rebuild(struct render_struct* p) {
    p->table_dirty = 1;
}

void render_debug_dump_lut(struct render_struct* p) {
    printf("RENDER clk2M=%d cpl=%d tt=%d dirty=%d flash=%d\n",
           p->is_clock_2MHz, p->chars_per_line, p->is_teletext,
           p->table_dirty, p->flash_on);
    printf("PHYS_LUT:");
    for (int i = 0; i < 16; i++) printf(" %d", p->physical_lut[i]);
    printf("\n");
}

/* ── Core pixel rendering ─────────────────────────────────────────────────── */

/* Effective output row = raw scanline minus the per-frame latched top. */
static inline int eff_y(struct render_struct* p) {
    return p->y - p->y_base;
}

static inline uint8_t* screen_row(int y) {
    if ((unsigned)y >= SCR_H) return NULL;
    return SCREEN[current_buffer ^ 1] + y * SCR_W;
}

void FRANK_NO_FPU FRANK_FAST_FUNC(render_render)(struct render_struct* p,
                    uint8_t data,
                    uint16_t addr,
                    uint64_t ticks) {
    /* ── Teletext (MODE 7) ───────────────────────────────────────────────
     * The SAA5050 is driven by video.c directly (DISPEN/RA/VSYNC).  Here we
     * feed it data bytes and render the pipelined glyph output.  Feeding
     * happens on every even (1MHz) tick regardless of DISPEN so the chip's
     * internal pipeline and scanline-end detection stay consistent. */
    if (p->is_teletext) {
        if (ticks & 1) return;                  /* SAA5050 clocked at 1MHz */
        uint8_t d = (addr & 0x2000) ? data : 0; /* off-screen → space */
        teletext_data(p->p_teletext, d);

        int x = p->x;
        int yy = eff_y(p);
        if (x >= 0 && x + 8 <= SCR_W && (unsigned)yy < SCR_H) {
            struct render_character_1MHz tmp;
            teletext_render(p->p_teletext, &tmp, NULL);
            uint8_t* row = SCREEN[current_buffer ^ 1] + yy * SCR_W;
            /* 16 source pixels → 8 output pixels.  Average each adjacent
             * subpixel pair and map through the anti-aliasing ramp so the
             * SAA5050 edge smoothing is preserved (smooth, legible glyphs
             * rather than jagged 1-bit downsampling). */
            for (int k = 0; k < 8; k++) {
                uint32_t a = tmp.host_pixels[k * 2];
                uint32_t b = tmp.host_pixels[k * 2 + 1];
                uint32_t r = (((a >> 16) & 0xff) + ((b >> 16) & 0xff)) >> 1;
                uint32_t g = (((a >> 8)  & 0xff) + ((b >> 8)  & 0xff)) >> 1;
                uint32_t bl = ((a & 0xff)        + (b & 0xff))         >> 1;
                row[x + k] = tt_aa_index(r, g, bl);
            }
            /* Apply a pending cursor.  In teletext the cursor address leads
             * the pipelined glyph stream by one character cell, so the cursor
             * must land on the *next* cell (the typing position next to the
             * prompt).  We defer one cell: pending 1 → arm for next cell,
             * pending 2 → XOR this cell after it's been drawn (so the
             * character draw doesn't overwrite the cursor). */
            if (p->cursor_pending == 2) {
                for (int k = 0; k < 8; k++) row[x + k] ^= 7;
                p->cursor_pending = 0;
            } else if (p->cursor_pending == 1) {
                p->cursor_pending = 2;
            }
        }
        p->x += 8;
        return;
    }

    if (!p->dispen) {
        /* Off-screen / blanking — don't advance (x is reset on DISPEN rise). */
        return;
    }

    if (p->table_dirty) rebuild_pixel_tables(p);

    uint8_t* row = screen_row(eff_y(p));
    if (!row) return;

    int x_before = p->x;

    if (p->is_clock_2MHz) {
        /* 2MHz modes — dispatch on chars_per_line:
         * cpl=3: MODE0 (80col, 2-color): 8 px/byte → 4 output px (lut2)
         * cpl=2: MODE1 (40col, 4-color): 8 px/byte → 8 output px (lut1)
         * cpl=1: MODE2 (20col, 16-color): 2 px/byte → 4 output px (lut1_20) */
        switch (p->chars_per_line) {
        case 3:
        default: {
            /* MODE0: 2-color, 4 output pixels per byte */
            if (p->x + 4 <= SCR_W) {
                uint32_t pix = p->lut2[data];
                row[p->x + 0] = (uint8_t)(pix);
                row[p->x + 1] = (uint8_t)(pix >> 8);
                row[p->x + 2] = (uint8_t)(pix >> 16);
                row[p->x + 3] = (uint8_t)(pix >> 24);
            }
            p->x += 4;
            break;
        }
        case 2: {
            /* MODE1: 4-color, 8 output pixels per byte */
            if (p->x + 8 <= SCR_W) {
                uint64_t pix = p->lut1[data];
                row[p->x+0] = (uint8_t)(pix);
                row[p->x+1] = (uint8_t)(pix >> 8);
                row[p->x+2] = (uint8_t)(pix >> 16);
                row[p->x+3] = (uint8_t)(pix >> 24);
                row[p->x+4] = (uint8_t)(pix >> 32);
                row[p->x+5] = (uint8_t)(pix >> 40);
                row[p->x+6] = (uint8_t)(pix >> 48);
                row[p->x+7] = (uint8_t)(pix >> 56);
            }
            p->x += 8;
            break;
        }
        case 1: {
            /* MODE2: 16-color, 4 output pixels per byte */
            if (p->x + 4 <= SCR_W) {
                uint64_t pix = p->lut1_20[data];
                row[p->x+0] = (uint8_t)(pix);
                row[p->x+1] = (uint8_t)(pix >> 8);
                row[p->x+2] = (uint8_t)(pix >> 16);
                row[p->x+3] = (uint8_t)(pix >> 24);
            }
            p->x += 4;
            break;
        }
        }
    } else {
        /* 1MHz modes — chars_per_line is raw ULA value (0-3):
         * 3=MODE4_80(4bpp/80col), 2=MODE4(2bpp/40col),
         * 1=MODE5(4bpp/20col),    0=MODE2_10(unusual) */
        switch (p->chars_per_line) {
        case 3:   /* MODE4_80: 80 logical cols → 4 output pixels per byte */
        case 2: { /* MODE4:   40 logical cols → 8 output pixels per byte */
            /* 1MHz 2-colour: lut1 gives 8 output pixels */
            if (p->x + 8 <= SCR_W) {
                uint64_t pix = p->lut1[data];
                row[p->x+0] = (uint8_t)(pix);
                row[p->x+1] = (uint8_t)(pix >> 8);
                row[p->x+2] = (uint8_t)(pix >> 16);
                row[p->x+3] = (uint8_t)(pix >> 24);
                row[p->x+4] = (uint8_t)(pix >> 32);
                row[p->x+5] = (uint8_t)(pix >> 40);
                row[p->x+6] = (uint8_t)(pix >> 48);
                row[p->x+7] = (uint8_t)(pix >> 56);
            }
            p->x += 8;
            break;
        }
        case 1: { /* MODE5: 20-col 4-colour → 8 output pixels per byte */
            if (p->x + 8 <= SCR_W) {
                uint64_t pix = p->lut1[data];
                row[p->x+0] = (uint8_t)(pix);
                row[p->x+1] = (uint8_t)(pix >> 8);
                row[p->x+2] = (uint8_t)(pix >> 16);
                row[p->x+3] = (uint8_t)(pix >> 24);
                row[p->x+4] = (uint8_t)(pix >> 32);
                row[p->x+5] = (uint8_t)(pix >> 40);
                row[p->x+6] = (uint8_t)(pix >> 48);
                row[p->x+7] = (uint8_t)(pix >> 56);
            }
            p->x += 8;
            break;
        }
        case 0: { /* MODE2: 20-col 16-colour → 4 output pixels per byte */
            if (p->x + 4 <= SCR_W) {
                uint64_t pix = p->lut1_20[data];
                row[p->x+0] = (uint8_t)(pix);
                row[p->x+1] = (uint8_t)(pix >> 8);
                row[p->x+2] = (uint8_t)(pix >> 16);
                row[p->x+3] = (uint8_t)(pix >> 24);
            }
            p->x += 4;
            break;
        }
        default:
            p->x += 8;
            break;
        }
    }

    /* Apply a pending cursor over the cell we just drew. */
    if (p->cursor_pending) {
        int x0 = x_before;
        int x1 = p->x;
        if (x0 < 0) x0 = 0;
        if (x1 > SCR_W) x1 = SCR_W;
        for (int i = x0; i < x1; i++) row[i] ^= 7;
        p->cursor_pending = 0;
    }
}

/* Batched non-teletext row renderer.  Mirrors the per-byte render_render()
 * graphics paths but hoists the mode dispatch out of the per-byte loop and
 * uses aligned 32-bit stores (render_set_DISPEN snaps x to 0 at the start of a
 * non-teletext active region, and the per-byte step is 4 or 8, so row+x stays
 * 4-byte aligned — SCREEN[] is aligned(4) and SCR_W is a multiple of 4). */
void FRANK_NO_FPU FRANK_FAST_FUNC(render_render_run)(struct render_struct* p,
                       const uint8_t* data,
                       int count,
                       int cursor_col) {
    if (p->table_dirty) rebuild_pixel_tables(p);

    int yy = p->y - p->y_base;
    if ((unsigned)yy >= (unsigned)SCR_H) return;  /* whole row off-screen */
    uint8_t* row = SCREEN[current_buffer ^ 1] + (unsigned)yy * SCR_W;
    int x = p->x;

    /* Decode the mode once — it is constant across the whole row. */
    const uint64_t* l64 = NULL;
    const uint32_t* l32 = NULL;
    int step;       /* output pixels per byte (4 or 8) */
    if (p->is_clock_2MHz) {
        switch (p->chars_per_line) {
        case 2:  l64 = p->lut1;    step = 8; break;  /* MODE1 */
        case 1:  l64 = p->lut1_20; step = 4; break;  /* MODE2 */
        default: l32 = p->lut2;    step = 4; break;  /* MODE0 */
        }
    } else {
        switch (p->chars_per_line) {
        case 3:
        case 2:
        case 1:  l64 = p->lut1;    step = 8; break;
        case 0:  l64 = p->lut1_20; step = 4; break;
        default: p->x = x + count * 8; return;       /* unknown — advance only */
        }
    }

    for (int i = 0; i < count; i++) {
        int xb = x;
        if (l64) {
            uint64_t pix = l64[data[i]];
            if (step == 8) {
                if (x + 8 <= SCR_W) {
                    *(uint32_t*)(row + x)     = (uint32_t)pix;
                    *(uint32_t*)(row + x + 4) = (uint32_t)(pix >> 32);
                }
                x += 8;
            } else {
                if (x + 4 <= SCR_W) *(uint32_t*)(row + x) = (uint32_t)pix;
                x += 4;
            }
        } else {
            if (x + 4 <= SCR_W) *(uint32_t*)(row + x) = l32[data[i]];
            x += 4;
        }
        if (i == cursor_col) {
            int x0 = xb, x1 = x;
            if (x0 < 0) x0 = 0;
            if (x1 > SCR_W) x1 = SCR_W;
            for (int k = x0; k < x1; k++) row[k] ^= 7;
        }
    }
    p->x = x;
}

void render_clear_buffer(struct render_struct* p) {
    uint8_t* s = SCREEN[current_buffer ^ 1];
    if (s) memset(s, 0, SCR_W * SCR_H);
    p->x = 0; p->y = 0;
}

void render_process_full_buffer(struct render_struct* p) {
    (void)p;
    /* Nothing needed — SCREEN[] is already the display buffer. */
}

void render_hsync(struct render_struct* p, uint32_t hsync_pulse_ticks) {
    (void)hsync_pulse_ticks;
    p->x = 0;
    p->y++;
    /* Raw scanline can exceed SCR_H because of top blanking absorbed into
     * y_base; eff_y is what's bounds-checked at write time.  Guard against
     * runaway only. */
    if (p->y >= 360) p->y = 360;
}

void render_vsync(struct render_struct* p) {
    p->x = 0;
    p->y = 0;
    p->y_base = 0;
    p->top_latched = 0;
    if (p->p_flyback_callback) {
        p->p_flyback_callback(p->p_flyback_object);
    }
}

void render_horiz_line(struct render_struct* p, uint32_t argb) {
    /* Draw a solid horizontal line at current y with the given colour. */
    uint8_t col = (uint8_t)((argb >> 24) ? 7 : 0); /* crude: white or black */
    (void)argb;
    uint8_t* row = screen_row(eff_y(p));
    if (row) memset(row, col, SCR_W);
}

void render_cursor(struct render_struct* p) {
    /* video.c calls this just before the render_render() for the cell under
     * the cursor.  Latch a pending flag; render_render() XORs the cell after
     * drawing it (so the cursor isn't immediately overwritten).  The BBC only
     * calls this on cursor-visible frames, giving a natural blink. */
    p->cursor_pending = 1;
}

void render_set_horiz_beam_pos(struct render_struct* p, uint32_t pos) {
    /* pos is in pixel units (0..639 for 2MHz mode).
     * Convert to our 320-wide space. */
    p->x = (int)(pos / 2);
    if (p->x > SCR_W) p->x = SCR_W;
}
