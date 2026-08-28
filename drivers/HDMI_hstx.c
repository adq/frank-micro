/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Andrew de Quincey <adq@lidskialf.net>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * HDMI_hstx.c — graphics_* backend over the RP2350's HSTX peripheral.
 *
 * Compiled only when the build selects HDMI_DRIVER=HSTX.  Output is
 * 720x576p50 with HDMI-embedded audio in data islands.  The driver underneath
 * is drivers/hstx/, vendored from fhoedemakers/pico_shared.
 *
 * Two things about this backend differ from every other one in the tree, and
 * both follow from the same fact: HSTX has no palette lookup.
 *
 *   1. graphics_set_palette() changes no pixel that is already on screen.
 *      The framebuffer holds RGB332 colour bytes, not indices, so the palette
 *      is applied when a pixel is written, through FRAMEBUFFER_PIXEL() in
 *      HDMI.h.  Everything that sets a palette entry still works and still
 *      decides what colour an index paints; a palette-cycling animation does
 *      not.  See the boot error screen in frank_platform.c.
 *
 *   2. No index is reserved.  drivers/HDMI.c has to keep indices 251-254 for
 *      TMDS sync symbols and substitutes content pixels that land on them;
 *      here all 256 byte values are colours.
 *
 * The pixel DMA reads framebuffer rows directly, so neither core touches
 * pixel data.  Core 1 runs the HSTX line IRQ and the audio background task
 * and is otherwise idle.
 *
 * Video data periods are exactly 720 pixels wide, so the framebuffer is
 * 720 bytes per row and the pillarbox border is black pixels inside the row
 * rather than a shorter DMA transfer.  Rows outside the picture read a
 * separate border row.
 */

#include "board_config.h"
#include "HDMI.h"

#include "hstx/video_output.h"
#include "hstx/hstx_packet.h"
#include "hstx/hstx_data_island_queue.h"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdalign.h>

/* ------------------------------------------------------------------ */
/* Globals the shared code expects                                    */
/* ------------------------------------------------------------------ */

int graphics_buffer_width = MODE_H_ACTIVE_PIXELS;
int graphics_buffer_height = 256;
int graphics_buffer_shift_x = 0;
int graphics_buffer_shift_y = 0;
enum graphics_mode_t hdmi_graphics_mode = GRAPHICSMODE_DEFAULT;

/* There is no VGA ribbon on an HSTX board and nothing to detect, so the
 * runtime dispatcher in HDMI_vga.c is not built.  HDMI_tv.c stubs these the
 * same way for the same reason. */
bool SELECT_VGA = false;
int testPins(uint32_t pin0, uint32_t pin1) { (void)pin0; (void)pin1; return 0xFF; }

/* Index-to-RGB332 table, read through FRAMEBUFFER_PIXEL() by everything that
 * writes a pixel.  Not static: HDMI.h declares it. */
uint8_t hdmi_hstx_pal332[256];

/* Shadow of what graphics_set_palette() was given, in RGB888.  Kept so
 * graphics_get_palette() and the screenshot writer can report real colours
 * rather than the 3-3-2 approximation. */
static uint32_t palette_shadow[256];

static uint8_t *graphics_buffer = NULL;
static bool crt_active = false;
static bool greyscale_active = false;

/* The border, one full-width row of background colour.  Every raster line
 * above and below the picture reads this. */
static alignas(4) uint8_t border_row[MODE_H_ACTIVE_PIXELS];
static uint8_t border_pixel = 0;

/* ------------------------------------------------------------------ */
/* Colour conversion                                                  */
/* ------------------------------------------------------------------ */

/*
 * RGB888 to RGB332, byte layout RRRGGGBB.  That layout is fixed by the lane
 * rotates in video_output_core1_run(): lane 2 reads bits 7:5, lane 1 bits
 * 4:2, lane 0 bits 1:0.
 *
 * Rounding rather than truncation, so 0xFF stays full scale in all three
 * channels and mid greys do not drift dark.
 */
static inline uint8_t rgb888_to_rgb332(uint32_t c) {
    uint32_t r = (c >> 16) & 0xff;
    uint32_t g = (c >> 8) & 0xff;
    uint32_t b = c & 0xff;
    uint32_t r3 = (r * 7u + 127u) / 255u;
    uint32_t g3 = (g * 7u + 127u) / 255u;
    uint32_t b2 = (b * 3u + 127u) / 255u;
    return (uint8_t)((r3 << 5) | (g3 << 2) | b2);
}

/* Expand an RGB332 byte back to RGB888 by bit replication, which is what the
 * TMDS encoder does: 3 valid bits become 8 by repeating the pattern, so 0b111
 * is 0xFF and 0b000 is 0x00. */
uint32_t hdmi_hstx_rgb332_to_rgb888(uint8_t p) {
    uint32_t r3 = (p >> 5) & 7u;
    uint32_t g3 = (p >> 2) & 7u;
    uint32_t b2 = p & 3u;
    uint32_t r = (r3 * 255u + 3u) / 7u;
    uint32_t g = (g3 * 255u + 3u) / 7u;
    uint32_t b = (b2 * 255u + 1u) / 3u;
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t rgb_to_grey(uint32_t color888) {
    uint32_t R = (color888 >> 16) & 0xff;
    uint32_t G = (color888 >> 8) & 0xff;
    uint32_t B = color888 & 0xff;
    uint32_t Y = (R * 77u + G * 150u + B * 29u) >> 8;
    return (Y << 16) | (Y << 8) | Y;
}

/* ------------------------------------------------------------------ */
/* Clocks                                                             */
/* ------------------------------------------------------------------ */

/*
 * clk_hstx has to be 135 MHz: five times the 27.0 MHz pixel clock, because a
 * TMDS symbol is 10 bits and the pins are DDR.
 *
 * clk_sys stays where it is, at 252 MHz on pll_sys.  That value is proven on
 * this board with PIO-USB and with the emulator, and 135 is not an integer
 * divisor of it.  A fractional divider would work arithmetically but puts
 * jitter on the TMDS bit clock, which strict sinks show as sparkle.
 *
 * So pll_usb is retasked: VCO 1080 MHz (12 MHz reference, fbdiv 90, inside
 * the SDK's 750-1600 MHz range) with post-dividers 4 and 2 gives exactly
 * 135 MHz, and clk_hstx takes it with no divider at all.
 *
 * That spends pll_usb, so the RP2350's native USB controller cannot run
 * afterwards.  On the Fruit Jam that costs nothing: the USB host is PIO-USB
 * on GPIO 1 and 2, and the console is on UART1.
 *
 * ---- clk_peri has to be moved first, and this is not optional ----
 *
 * pll_usb does not only feed USB.  `set_sys_clock_khz()` leaves clk_peri
 * attached to it, undivided, at 48 MHz: see `set_sys_clock_pll()` in the
 * SDK's `hardware_clocks/clocks.c`, which takes that branch unless
 * PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK is set, and it is not.
 *
 * So retasking pll_usb drags clk_peri from 48 MHz to 135 MHz, and every
 * peripheral that derives a divisor from it comes out 2.8 times too fast
 * while the SDK still believes the old number. That is UART1, the console,
 * and SPI0, the SD card. It presents as a card that will not mount with a
 * card in the slot, because the 400 kHz initialisation clock the card
 * requires is really running at 1.125 MHz.
 *
 * Re-pointing clk_peri at clk_sys divided by 3 gives 84 MHz, near enough to
 * the 48 MHz the rest of the tree was written against, well under the
 * peripheral clock's ceiling, and no longer coupled to the video clock at
 * all. clk_peri's divider is 2 bits and integer-only, so 3 is the largest
 * it offers.
 *
 * This whole function therefore has to run before stdio_init_all() and
 * before anything touches SPI, so that every divisor downstream is computed
 * from the final clk_peri. main() calls it immediately after
 * set_sys_clock_khz().
 */
void hdmi_hstx_clock_init(void) {
    uint32_t sys_hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri,
                    0, /* no glitchless mux on clk_peri */
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    sys_hz,
                    sys_hz / 3);

    /* Neither is used on this board, and leaving them attached to a PLL that
     * is about to change frequency underneath them helps nobody. */
    clock_stop(clk_usb);
    clock_stop(clk_adc);

    pll_init(pll_usb, 1, 1080 * MHZ, 4, 2);

    clock_configure(clk_hstx,
                    0, /* no glitchless mux on clk_hstx */
                    CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    MODE_HSTX_CLK_HZ,
                    MODE_HSTX_CLK_HZ);
}

/* ------------------------------------------------------------------ */
/* Framebuffer mapping                                                */
/* ------------------------------------------------------------------ */

/*
 * Hand the current framebuffer and geometry to the video driver.
 *
 * Vertical scaling is whole-number and centred: 256 rows at scale 2 fill 512
 * of the 576 active lines, leaving a 32-line border top and bottom.  Nothing
 * is clipped, which is the point of moving to HSTX — the PIO path could only
 * show 240 of the BBC's 256 lines.
 */
static void apply_geometry(void) {
    uint32_t rows = (uint32_t)graphics_buffer_height;
    uint32_t scale = rows ? (MODE_V_ACTIVE_LINES / rows) : 1;
    if (scale == 0) scale = 1;

    uint32_t used = rows * scale;
    if (used > MODE_V_ACTIVE_LINES) used = MODE_V_ACTIVE_LINES;
    uint32_t offset = (MODE_V_ACTIVE_LINES - used) / 2;

    video_output_set_framebuffer(graphics_buffer, MODE_H_ACTIVE_PIXELS, rows,
                                 offset, scale, border_row);
}

/* ------------------------------------------------------------------ */
/* graphics_* API                                                     */
/* ------------------------------------------------------------------ */

void graphics_set_buffer(uint8_t *buffer) {
    /* Called once per frame by frank_gui.c's present step. The framebuffer is
     * single-buffered here, so the pointer almost never changes and rebuilding
     * the 576-entry row table each frame would be pure waste. */
    if (buffer == graphics_buffer) return;
    graphics_buffer = buffer;
    apply_geometry();
}

/* ---- vsync timestamp, for the frame pacer ---- */

static volatile uint32_t vsync_us = 0;

static void __not_in_flash_func(on_vsync)(void) {
    vsync_us = time_us_32();
}

uint32_t hdmi_hstx_vsync_us(void) { return vsync_us; }

uint8_t *graphics_get_buffer(void) { return graphics_buffer; }

uint32_t graphics_get_width(void)  { return (uint32_t)graphics_buffer_width;  }
uint32_t graphics_get_height(void) { return (uint32_t)graphics_buffer_height; }

void graphics_set_res(int w, int h) {
    /* The width is not negotiable: a video data period is exactly
     * MODE_H_ACTIVE_PIXELS wide and the DMA reads one framebuffer byte per
     * pixel, so a narrower framebuffer would shear the picture rather than
     * shrink it.  Borders are black pixels inside the row. */
    if (w != MODE_H_ACTIVE_PIXELS) {
        printf("HSTX: ignoring graphics_set_res width %d, mode is %d wide\n",
               w, MODE_H_ACTIVE_PIXELS);
    } else {
        graphics_buffer_width = w;
    }
    graphics_buffer_height = h;
    apply_geometry();
}

void graphics_set_shift(int x, int y) {
    graphics_buffer_shift_x = x;
    graphics_buffer_shift_y = y;
}

void graphics_set_mode(enum graphics_mode_t mode) {
    hdmi_graphics_mode = mode;
}

uint8_t *get_line_buffer(int line) {
    if (!graphics_buffer) return NULL;
    if (line < 0 || line >= graphics_buffer_height) return NULL;
    return graphics_buffer + (size_t)line * MODE_H_ACTIVE_PIXELS;
}

struct video_mode_t graphics_get_video_mode(int mode) {
    (void)mode;
    struct video_mode_t vm = {
        .h_total  = MODE_H_TOTAL_PIXELS,
        .h_width  = MODE_H_ACTIVE_PIXELS,
        .freq     = 50,
        .vgaPxClk = (int)MODE_PIXEL_CLOCK_HZ,
    };
    return vm;
}

void graphics_set_palette(uint8_t i, uint32_t color888) {
    color888 &= 0x00ffffffu;
    palette_shadow[i] = color888;
    uint32_t display = greyscale_active ? rgb_to_grey(color888) : color888;
    hdmi_hstx_pal332[i] = rgb888_to_rgb332(display);
}

uint32_t graphics_get_palette(uint8_t i) { return palette_shadow[i]; }

void graphics_set_bgcolor(uint32_t color888) {
    /* Index 0 is the background everywhere else in the tree, and it is also
     * what the border rows are filled with. */
    graphics_set_palette(0, color888);
    border_pixel = hdmi_hstx_pal332[0];
    memset(border_row, border_pixel, sizeof(border_row));
}

void graphics_restore_sync_colors(void) {
    /* Nothing to restore.  HSTX generates sync in hardware from the command
     * list, so no palette index is reserved for control symbols. */
}

/* HDMI-specific entry points.  There is no VGA dispatcher in this build, so
 * these are the same functions under their other names. */
void graphics_init_hdmi(void) { }
void graphics_set_palette_hdmi(uint8_t i, uint32_t color888) { graphics_set_palette(i, color888); }
void graphics_set_bgcolor_hdmi(uint32_t color888) { graphics_set_bgcolor(color888); }

/* ---- CRT / greyscale ---- */

void graphics_set_crt_active(bool active) { crt_active = active; }
bool graphics_get_crt_active(void)        { return crt_active; }

void graphics_set_greyscale(bool active) {
    if (greyscale_active == active) return;
    greyscale_active = active;
    for (int i = 0; i < 256; i++) {
        uint32_t c = palette_shadow[i];
        hdmi_hstx_pal332[i] = rgb888_to_rgb332(greyscale_active ? rgb_to_grey(c) : c);
    }
    /* Pixels already in the framebuffer keep the colour they were written
     * with; the change takes effect as the emulator redraws. */
}

bool graphics_get_greyscale(void) { return greyscale_active; }

/* ---- Stubs kept for API compatibility ---- */

void startVIDEO(uint8_t vol) { (void)vol; }
void set_palette(uint8_t n)  { (void)n;   }

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */

void graphics_init(g_out out) {
    (void)out;

    /* hdmi_hstx_clock_init() has already run, from main(), because it moves
     * clk_peri and so has to happen before the console and the SD card are
     * brought up. */
    printf("HSTX: clk_sys=%lu Hz clk_peri=%lu Hz clk_hstx=%lu Hz\n",
           (unsigned long)clock_get_hz(clk_sys),
           (unsigned long)clock_get_hz(clk_peri),
           (unsigned long)clock_get_hz(clk_hstx));

    /* HDMI rather than DVI, so audio data islands are emitted. */
    video_output_set_dvi_mode(false);
    video_output_set_vsync_callback(on_vsync);
    hstx_di_queue_init();
    video_output_init(MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES);

    /* The SN76489 path resamples 31250 to 32000 before it gets here.  32000
     * is a standard rate, so the sink has a tabulated N and CTS for it; at a
     * 27 MHz pixel clock CTS is 27000 exactly. */
    pico_hdmi_set_audio_sample_rate(32000);

    memset(border_row, 0, sizeof(border_row));
    apply_geometry();

    /* Core 1 programs HSTX and then owns the line IRQ.  It has to be launched
     * after video_output_init(), which builds the command lists the IRQ
     * reads and claims the DMA channels. */
    multicore_launch_core1(video_output_core1_run);
    sleep_ms(50);
    printf("HSTX video started: %dx%d @ 50 Hz, RGB332\n",
           MODE_H_ACTIVE_PIXELS, MODE_V_ACTIVE_LINES);
}

/* ------------------------------------------------------------------ */
/* Audio: mono/stereo frames into the data-island ring                */
/* ------------------------------------------------------------------ */

/* I2S ring buffer, for when the F12 menu selects the codec instead. */
extern unsigned i2s_ring_push(const int16_t *samples, unsigned count);
extern unsigned i2s_ring_push_stereo(const int16_t *samples, unsigned count);
extern unsigned i2s_ring_free(void);

/*
 * Accumulate four stereo frames per data island, encode, and push.  Four is
 * what one HDMI audio sample packet carries.
 *
 * Dropping rather than blocking when the ring is full is deliberate: this
 * runs on the emulator's core, and stalling it to wait for a video-rate
 * consumer would cost frames.  The reader substitutes a pre-encoded silence
 * packet when it finds the ring empty, so a drop costs a fraction of a
 * millisecond of audio rather than a loss of lock.
 */
unsigned __not_in_flash_func(hdmi_hstx_push_stereo)(const int16_t *buf, unsigned frames) {
    static audio_sample_t acc[4];
    static int acc_count = 0;
    static int frame_counter = 0;

    for (unsigned i = 0; i < frames; i++) {
        acc[acc_count].left  = buf[i * 2 + 0];
        acc[acc_count].right = buf[i * 2 + 1];
        acc_count++;
        if (acc_count < 4) continue;
        acc_count = 0;

        if (hstx_di_queue_get_level() >= HSTX_AUDIO_DI_HIGH_WATERMARK)
            continue;

        hstx_packet_t packet;
        /* The _cs variant writes IEC 60958 channel status with a valid
         * sample-frequency code.  Strict receivers, which is most computer
         * monitors, will not unmute without it even though the Audio
         * InfoFrame already carries the format. */
        frame_counter = hstx_packet_set_audio_samples_cs(&packet, acc, 4, frame_counter);

        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, true);
        (void)hstx_di_queue_push(&island);
    }
    return frames;
}

/*
 * audio_ring_push_* — the entry points b-em's audio layer calls.  frank-micro
 * drives the HDMI backend through frank_audio.c instead, so these exist for
 * the same reason they do in HDMI_audio.c: to keep the shared audio code
 * linking.  They route to I2S, which is the other live backend here.
 */
unsigned audio_ring_push_mono(const int16_t *samples, unsigned count) {
    return i2s_ring_push(samples, count);
}

unsigned audio_ring_push_stereo(const int16_t *samples, unsigned count) {
    return i2s_ring_push_stereo(samples, count);
}

unsigned audio_ring_free(void) { return i2s_ring_free(); }

unsigned audio_ring_avail(void) {
    extern volatile uint32_t g_audio_prod, g_audio_cons;
    return g_audio_prod - g_audio_cons;
}
