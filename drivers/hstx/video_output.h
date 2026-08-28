/*
 * HSTX DVI/HDMI video output for the RP2350.
 *
 * Vendored into frank-micro from pico_shared, drivers/pico_hdmi.
 *
 *   https://github.com/PicoPlus-devel/pico_shared
 *   (formerly fhoedemakers/pico_shared, maintained by github.com/fhoedemakers)
 *
 * Taken by way of the vendored copy inside fhoedemakers/fruitjam-doom, which
 * is where it was found running on Fruit Jam hardware.
 *
 * Upstream is licensed GPL-3.0. Its LICENSE is the plain GPLv3 text with no
 * copyright holder filled in, and no upstream source file carries a per-file
 * notice, so none is reproduced here and nothing below claims or reassigns
 * copyright in the vendored code. The comment block and the declarations for
 * video_output_set_framebuffer() are new; the rest of this header is
 * upstream's, edited.
 *
 * Changed for frank-micro, and these apply to every file in this directory:
 *
 *   - 720x576p50 rather than 640x480p60, so 50 Hz matches the emulator and
 *     the BBC's 256 lines double onto the raster with room to spare. Static
 *     asserts in video_output.c check the horizontal budget adds up.
 *   - RGB332 pixels rather than RGB555.
 *   - The pixel DMA reads framebuffer rows directly. Upstream called a
 *     scanline callback into a line buffer, which cost about 21 us of core 1
 *     per line; that callback and both line buffers are gone, replaced by
 *     fb_row_addr[], which resolves each raster line to a source address once.
 *   - DMA_IRQ_1 rather than DMA_IRQ_0, which I2S owns in this tree.
 *   - clk_hstx is the caller's job; see hdmi_hstx_clock_init() in
 *     drivers/HDMI_hstx.c.
 *   - The data-island ring is statically allocated rather than malloc'd.
 *   - The audio packet line rate follows the mode's pixel clock rather than a
 *     hardcoded 25.2 MHz.
 *   - hstx.c is not vendored: it owns a 154 KB framebuffer nothing here uses.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef VIDEO_OUTPUT_H
#define VIDEO_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Video Output Configuration
// ============================================================================

// 720x576p50, CEA-861 VIC 17. Pixel clock 27.000 MHz, both syncs negative.
// 864 x 625 x 50 = 27,000,000 exactly, so nothing here is approximate.
#define MODE_H_FRONT_PORCH 12
#define MODE_H_SYNC_WIDTH 64
#define MODE_H_BACK_PORCH 68
#define MODE_H_ACTIVE_PIXELS 720

#define MODE_V_FRONT_PORCH 5
#define MODE_V_SYNC_WIDTH 5
#define MODE_V_BACK_PORCH 39
#define MODE_V_ACTIVE_LINES 576

#define MODE_PIXEL_CLOCK_HZ 27000000u

// clk_hstx must be 5x the pixel clock: the output shifter emits 2 bits per
// cycle (DDR) and a TMDS symbol is 10 bits, so a pixel takes 5 cycles.
// MODE_HSTX_CLK_DIV is unused here because the caller configures clk_hstx
// itself; see hstx_clock_init() in HDMI_hstx.c.
#define MODE_HSTX_CLK_HZ (MODE_PIXEL_CLOCK_HZ * 5u)
#define MODE_HSTX_CLK_DIV 1
#define MODE_HSTX_CSR_CLKDIV 5

#define MODE_H_TOTAL_PIXELS (MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS)
#define MODE_V_TOTAL_LINES (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH + MODE_V_ACTIVE_LINES)

// Depth of the audio data-island ring, in packets, before pushes are dropped.
// 4 samples per packet, so 224 packets is about 28 ms at 32 kHz.
#ifndef HSTX_AUDIO_DI_HIGH_WATERMARK
#define HSTX_AUDIO_DI_HIGH_WATERMARK 224
#endif

// ============================================================================
// Global State
// ============================================================================

extern volatile uint32_t video_frame_count;

// ============================================================================
// Public Interface
// ============================================================================

typedef void (*video_output_task_fn)(void);
typedef void (*video_output_vsync_cb_t)(void);

/**
 * Point the pixel DMA at a framebuffer.
 *
 * The framebuffer holds one RGB332 byte per pixel, `stride` bytes per row and
 * `rows` rows. `stride` must be a multiple of 4 and equal to
 * MODE_H_ACTIVE_PIXELS: the video data period is exactly that many pixels
 * wide, so any pillarbox border has to be black pixels inside the row rather
 * than a shorter transfer. Rows are displayed starting at raster line
 * `v_offset` of the active region, each repeated `v_scale` times.
 *
 * Raster lines outside that window emit `border_row`, which must be a
 * `stride`-byte row of RGB332 background. Passing NULL for `fb` parks the
 * whole active region on the border row.
 *
 * Safe to call at any time; the DMA picks the new pointer up on the next line.
 */
void video_output_set_framebuffer(const uint8_t *fb, uint32_t stride, uint32_t rows, uint32_t v_offset,
                                  uint32_t v_scale, const uint8_t *border_row);

/**
 * Initialize HSTX and DMA for video output.
 * @param width Framebuffer width in pixels
 * @param height Framebuffer height in pixels
 */
void video_output_init(uint16_t width, uint16_t height);

/**
 * Tear down the HSTX video output. Disables and unclaims the DMA channels
 * + IRQ that video_output_init / video_output_core1_run installed so a
 * subsequent re-init can reclaim them. Must be called from core0 only,
 * after core1's loop has been stopped or is about to be reset.
 */
void video_output_stop(void);

/**
 * Register a VSYNC callback, called once per frame at the start of vertical
 * sync. Runs in the DMA IRQ on core 1, so it must be short and must not
 * fetch from flash.
 */
void video_output_set_vsync_callback(video_output_vsync_cb_t cb);

/**
 * Register a background task to run in the Core 1 loop.
 * This is typically used for audio processing.
 */
void video_output_set_background_task(video_output_task_fn task);

/**
 * Get DVI mode status.
 * @return true if DVI mode (no HDMI audio), false if HDMI mode
 */
bool video_output_get_dvi_mode(void);

/**
 * Set DVI mode.
 * When enabled, disables all HDMI Data Islands (no audio output).
 * Some monitors have trouble syncing with HDMI Data Islands.
 * Default: false (HDMI mode with audio).
 * @param enabled true for DVI mode, false for HDMI mode with audio
 */
void video_output_set_dvi_mode(bool enabled);

/**
 * Core 1 entry point for video output.
 * This function does not return.
 */
void video_output_core1_run(void);

/**
 * Reconfigure HDMI audio for a different sample rate.
 * Updates ACR, Audio InfoFrame, and packet timing.
 * Can be called after video_output_init() to override the default 48kHz.
 * @param sample_rate Audio sample rate in Hz (e.g. 32000, 44100, 48000)
 */
void pico_hdmi_set_audio_sample_rate(uint32_t sample_rate);

/// Last rate passed to pico_hdmi_set_audio_sample_rate (default 48000).
uint32_t pico_hdmi_get_audio_sample_rate(void);

/**
 * Request a full HSTX + DMA resync. The request is latched and serviced by
 * the core-1 main loop, so this is safe to call from core 0 at any time.
 * A watchdog in the core-1 loop also auto-triggers a resync if the frame
 * counter stops advancing for ~500 ms.
 */
void video_output_request_resync(void);

int get_video_output_resync_count(void);

#endif // VIDEO_OUTPUT_H
