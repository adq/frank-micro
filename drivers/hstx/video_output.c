#include "video_output.h"

#include "hstx_data_island_queue.h"
#include "hstx_packet.h"
#include "hstx_pins.h"
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/pll.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include "stdio.h"

#ifndef HSTX_DEBUG
#define HSTX_DEBUG 0
#endif

// ============================================================================
// DVI/HSTX Constants
// ============================================================================

#define TMDS_CTRL_00 0x354u // vsync=0 hsync=0
#define TMDS_CTRL_01 0x0abu // vsync=0 hsync=1
#define TMDS_CTRL_10 0x154u // vsync=1 hsync=0
#define TMDS_CTRL_11 0x2abu // vsync=1 hsync=1

// Sync symbols: Lane 0 carries sync, Lanes 1&2 are always CTRL_00
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

// Data Island preamble: Lane 0 = sync, Lanes 1&2 = CTRL_01 pattern
// Per HDMI 1.3a Table 5-2: CTL0=1, CTL1=0, CTL2=1, CTL3=0
#define PREAMBLE_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))
#define PREAMBLE_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_01 << 20))

// Video preamble: Lane 0 = sync, Lane 1 = CTRL_01, Lane 2 = CTRL_00
// Per HDMI 1.3a Table 5-2: CTL0=1, CTL1=0, CTL2=0, CTL3=0
#define VIDEO_PREAMBLE_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))
#define VIDEO_PREAMBLE_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_01 << 10) | (TMDS_CTRL_00 << 20))

// Video guard band: Per HDMI 1.3a Table 5-5
// CH0 = 0b1011001100 (0x2CC), CH1 = 0b0100110011 (0x133), CH2 = 0b1011001100 (0x2CC)
#define VIDEO_GUARD_BAND (0x2CCu | (0x133u << 10) | (0x2CCu << 20))

#define HSTX_CMD_RAW (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT (0x1u << 12)
#define HSTX_CMD_TMDS (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP (0xfu << 12)

#define SYNC_AFTER_DI (MODE_H_SYNC_WIDTH - W_PREAMBLE - W_DATA_ISLAND)

// Video preamble and guard band widths (HDMI 1.3a Section 5.2.2)
#define W_VIDEO_PREAMBLE 8
#define W_VIDEO_GUARD_BAND 2

// A mode change is the easiest way to break this file silently: the horizontal
// budget is split between the sync region, which has to hold the data island,
// and the back porch, which has to hold the video preamble and guard band.
// Get either wrong and the line still emits, just with the wrong number of
// pixel clocks in it, which shows up as a picture that will not lock rather
// than as an error. So check the arithmetic at compile time.
static_assert(SYNC_AFTER_DI > 0,
              "hsync is too narrow to hold the data island and its preamble");
static_assert(MODE_H_BACK_PORCH > W_VIDEO_PREAMBLE + W_VIDEO_GUARD_BAND,
              "back porch is too short for the video preamble and guard band");
static_assert(MODE_H_FRONT_PORCH + W_PREAMBLE + W_DATA_ISLAND + SYNC_AFTER_DI +
                      (MODE_H_BACK_PORCH - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND) +
                      W_VIDEO_PREAMBLE + W_VIDEO_GUARD_BAND + MODE_H_ACTIVE_PIXELS ==
                  MODE_H_TOTAL_PIXELS,
              "active-line command list does not add up to one whole line");
static_assert(MODE_H_FRONT_PORCH + W_PREAMBLE + W_DATA_ISLAND + SYNC_AFTER_DI +
                      MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS ==
                  MODE_H_TOTAL_PIXELS,
              "blanking-line command list does not add up to one whole line");
// Four RGB332 pixels per 32-bit FIFO word, so a row has to divide by four.
static_assert(MODE_H_ACTIVE_PIXELS % 4 == 0,
              "active width must be a whole number of 4-pixel FIFO words");

// ============================================================================
// Audio/Video State
// ============================================================================

uint16_t frame_width = 0;
uint16_t frame_height = 0;
volatile uint32_t video_frame_count = 0;

#if HSTX_DEBUG
static volatile uint32_t irq_count = 0;
#endif

// DVI mode: when true, disables all HDMI Data Islands (pure DVI output, no audio)
// Some monitors have trouble syncing with HDMI Data Islands
static bool dvi_mode = false; // Default to HDMI mode (full features with audio)

// Framebuffer the pixel DMA reads from, and how the active region maps onto
// it. Upstream kept two line buffers here and filled one per line from a
// callback; RGB332 pixels let the DMA read framebuffer rows directly, so the
// CPU is out of the video path entirely.
//
// fb_row_addr[] holds a source address per active raster line, resolved once
// by video_output_set_framebuffer(). The IRQ then costs one load rather than
// a divide, and a border line needs no branch: it points at a black row like
// any other line.
static const uint8_t *fb_row_addr[MODE_V_ACTIVE_LINES];
static uint32_t fb_stride_words = MODE_H_ACTIVE_PIXELS / 4;

// Emitted before any framebuffer is registered, and whenever the caller
// supplies no border row of its own. Zero is RGB332 black.
static uint8_t blank_row[MODE_H_ACTIVE_PIXELS] __attribute__((aligned(4)));
// Active line whose pixels the *next* IRQ has to supply. Set when that line's
// command list is posted, consumed one IRQ later.
static uint32_t fb_pending_line = 0;

static uint32_t v_scanline = 2;
static bool vactive_cmdlist_posted = false;
static bool dma_pong = false;

static video_output_task_fn background_task = NULL;
static video_output_vsync_cb_t vsync_callback = NULL;

// Auto-resync watchdog: set by core 0 (or by the core-1 main-loop watchdog)
// to request a full HSTX+DMA reset. Consumed in video_output_core1_run().
static volatile bool resync_requested = false;
static volatile int resync_count = 0;
#define DMACH_PING 0
#define DMACH_PONG 1

// ============================================================================
// Command Lists
// ============================================================================

// Pure DVI command lists (no Data Islands)
static uint32_t vblank_line_vsync_off[] = {HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
                                           SYNC_V1_H1,
                                           HSTX_CMD_NOP,
                                           HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
                                           SYNC_V1_H0,
                                           HSTX_CMD_NOP,
                                           HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
                                           SYNC_V1_H1,
                                           HSTX_CMD_NOP};

static uint32_t vblank_line_vsync_on[] = {HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
                                          SYNC_V0_H1,
                                          HSTX_CMD_NOP,
                                          HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
                                          SYNC_V0_H0,
                                          HSTX_CMD_NOP,
                                          HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
                                          SYNC_V0_H1,
                                          HSTX_CMD_NOP};

// The bare 9-word DVI active-line command list that would go here is gone:
// DVI mode uses vactive_di_null instead, for the reason in
// video_output_handle_vsync(). Upstream kept it as dead code.

static uint32_t vactive_di_ping[128], vactive_di_pong[128], vactive_di_null[128];
static uint32_t vactive_di_len, vactive_di_null_len;

static uint32_t vblank_di_ping[128], vblank_di_pong[128], vblank_di_null[128];
static uint32_t vblank_di_len, vblank_di_null_len;

// DVI-mode null-DI cmdlist for VSYNC-active blanking lines. The other
// null-DI buffers above are pre-built with vsync=false; we need a vsync=true
// variant so DVI mode can emit the same HDMI-structured cmdlist (data-island
// period + video preamble + guard band) on every line of the frame, including
// the 2-line vertical sync pulse. See video_output_init().
static uint32_t vblank_di_null_vsync_on[128];
static uint32_t vblank_di_null_vsync_on_len;

static uint32_t vblank_acr_vsync_on[64], vblank_acr_vsync_on_len;
static uint32_t vblank_acr_vsync_off[64], vblank_acr_vsync_off_len;
static uint32_t vblank_infoframe_vsync_on[64], vblank_infoframe_vsync_on_len;
static uint32_t vblank_infoframe_vsync_off[64], vblank_infoframe_vsync_off_len;
static uint32_t vblank_avi_infoframe[64], vblank_avi_infoframe_len;

// ============================================================================
// HSTX Resync - Reset output to sync with input VSYNC
// ============================================================================

static void __not_in_flash_func(hstx_resync)(void)
{
    // RP2350-E5: clear EN on the aborted channel AND any channel it chains
    // from, otherwise a chained partner can re-trigger the aborted channel
    // while abort is in flight and leave it latched live with stale config.
    hw_clear_bits(&dma_hw->ch[DMACH_PING].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[DMACH_PONG].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);

    // 1. Abort DMA chains
    dma_channel_abort(DMACH_PING);
    dma_channel_abort(DMACH_PONG);

    // 2. Disable HSTX (resets shift register, clock generator, and flushes FIFO)
    hstx_ctrl_hw->csr &= ~HSTX_CTRL_CSR_EN_BITS;

    // Small delay to ensure HSTX fully stops
    __asm volatile("nop\nnop\nnop\nnop");

    // 3. Reset state to start of frame
    v_scanline = 0;
    vactive_cmdlist_posted = false;
    dma_pong = false;
    fb_pending_line = 0;

    // 4. Clear any pending DMA interrupts (including spurious completion
    //    interrupts that abort can latch per RP2350-E5 / SDK docs).
    dma_hw->ints1 = (1U << DMACH_PING) | (1U << DMACH_PONG);

    // 5. Restore the EN bits cleared above so the channels are live again.
    hw_set_bits(&dma_hw->ch[DMACH_PING].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_set_bits(&dma_hw->ch[DMACH_PONG].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);

    // 6. Configure DMA PING to start from beginning of frame (Line 0)
    dma_channel_hw_t *ch_ping = &dma_hw->ch[DMACH_PING];
    ch_ping->read_addr = (uintptr_t)vblank_line_vsync_off;
    ch_ping->transfer_count = count_of(vblank_line_vsync_off);

    // 7. Configure DMA PONG for the NEXT line (Line 1)
    // This ensures that when PING finishes and chains to PONG, PONG is ready.
    dma_channel_hw_t *ch_pong = &dma_hw->ch[DMACH_PONG];
    ch_pong->read_addr = (uintptr_t)vblank_line_vsync_off; // Line 1 is also blank
    ch_pong->transfer_count = count_of(vblank_line_vsync_off);

    // 8. Re-enable HSTX then start DMA
    hstx_ctrl_hw->csr |= HSTX_CTRL_CSR_EN_BITS;
    dma_channel_start(DMACH_PING);
}

// ============================================================================
// Internal Helpers
// ============================================================================

// __not_in_flash_func: called from the scanline DMA IRQ; must not fetch from
// flash while the QMI may be saturated (CHD decompress, PSRAM traffic).
static uint32_t __not_in_flash_func(build_line_with_di)(uint32_t *buf, const uint32_t *di_words, bool vsync, bool active)
{
    uint32_t *p = buf;
    uint32_t sync_h0 = vsync ? SYNC_V0_H0 : SYNC_V1_H0;
    uint32_t sync_h1 = vsync ? SYNC_V0_H1 : SYNC_V1_H1;
    uint32_t preamble = vsync ? PREAMBLE_V0_H0 : PREAMBLE_V1_H0;

    *p++ = HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH;
    *p++ = sync_h1;
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW_REPEAT | W_PREAMBLE;
    *p++ = preamble;
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW | W_DATA_ISLAND;
    for (int i = 0; i < W_DATA_ISLAND; i++)
        *p++ = di_words[i];
    *p++ = HSTX_CMD_NOP;

    *p++ = HSTX_CMD_RAW_REPEAT | SYNC_AFTER_DI;
    *p++ = sync_h0;
    *p++ = HSTX_CMD_NOP;

    if (active) {
        // HDMI 1.3a Section 5.2.2: Video Data Period requires preamble and guard band
        uint32_t video_preamble = vsync ? VIDEO_PREAMBLE_V0_H1 : VIDEO_PREAMBLE_V1_H1;

        // Control period (back porch minus preamble and guard band)
        *p++ = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH - W_VIDEO_PREAMBLE - W_VIDEO_GUARD_BAND);
        *p++ = sync_h1;
        *p++ = HSTX_CMD_NOP;

        // Video Preamble (8 pixels)
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_PREAMBLE;
        *p++ = video_preamble;
        *p++ = HSTX_CMD_NOP;

        // Video Guard Band (2 pixels)
        *p++ = HSTX_CMD_RAW_REPEAT | W_VIDEO_GUARD_BAND;
        *p++ = VIDEO_GUARD_BAND;

        // Active video pixels
        *p++ = HSTX_CMD_TMDS | MODE_H_ACTIVE_PIXELS;
    } else {
        *p++ = HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS);
        *p++ = sync_h1;
        *p++ = HSTX_CMD_NOP;
    }
    return (uint32_t)(p - buf);
}

typedef struct {
    bool vsync_active;
    bool front_porch;
    bool back_porch;
    bool active_video;
    bool send_acr;
    uint32_t active_line;
} scanline_state_t;

static inline void __not_in_flash_func(get_scanline_state)(uint32_t v_scanline, scanline_state_t *state)
{
    state->vsync_active = (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH));
    state->front_porch = (v_scanline < MODE_V_FRONT_PORCH);
    state->back_porch = (v_scanline >= MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH &&
                         v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH);
    state->active_video = (!state->vsync_active && !state->front_porch && !state->back_porch);

    state->send_acr = (v_scanline >= (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH) &&
                       v_scanline < (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES) && (v_scanline % 4 == 0));

    if (state->active_video) {
        state->active_line = v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES);
    } else {
        state->active_line = 0;
    }
}

static inline void __not_in_flash_func(video_output_handle_vsync)(dma_channel_hw_t *ch, uint32_t v_scanline)
{
    if (dvi_mode) {
        // DVI mode: use the same HDMI-structured cmdlist (data-island period
        // + video preamble + guard band) as HDMI mode, but with a null DI
        // packet so no audio / AVI / ACR data is actually transmitted. The
        // longer cmdlist produces a much richer control-period before each
        // active region, which several sinks (notably the Murmulator M2's
        // monitor) need to maintain TMDS lock — the bare 9-word DVI cmdlist
        // caused frequent lock loss and watchdog resyncs.
        ch->read_addr = (uintptr_t)vblank_di_null_vsync_on;
        ch->transfer_count = vblank_di_null_vsync_on_len;
        if (v_scanline == MODE_V_FRONT_PORCH) {
            video_frame_count++;
            if (vsync_callback)
                vsync_callback();
        }
    } else {
        if (v_scanline == MODE_V_FRONT_PORCH) {
            ch->read_addr = (uintptr_t)vblank_acr_vsync_on;
            ch->transfer_count = vblank_acr_vsync_on_len;
            video_frame_count++;
            if (vsync_callback)
                vsync_callback();
        } else {
            ch->read_addr = (uintptr_t)vblank_infoframe_vsync_on;
            ch->transfer_count = vblank_infoframe_vsync_on_len;
        }
    }
}

static inline void __not_in_flash_func(video_output_handle_active_start)(dma_channel_hw_t *ch, uint32_t v_scanline, uint32_t active_line, bool dma_pong)
{
    // 1. Arm the cmdlist DMA FIRST, before any slow work. This channel will be
    //    chain-triggered when the currently-running pixel-data DMA completes,
    //    so it must be armed well before then. Doing this first means that
    //    even if the scanline callback below overruns, the cmdlist chain is
    //    safe and HSTX keeps emitting valid sync.
    if (dvi_mode) {
        // DVI mode: share HDMI's active-line cmdlist (with null DI) so the
        // sink sees the same control-period structure on every line. See
        // video_output_handle_vsync for rationale.
        ch->read_addr = (uintptr_t)vactive_di_null;
        ch->transfer_count = vactive_di_null_len;
    } else {
        uint32_t *buf = dma_pong ? vactive_di_ping : vactive_di_pong;
        const uint32_t *di_words = hstx_di_queue_get_audio_packet();
        if (di_words) {
            vactive_di_len = build_line_with_di(buf, di_words, false, true);
            ch->read_addr = (uintptr_t)buf;
            ch->transfer_count = vactive_di_len;
        } else {
            ch->read_addr = (uintptr_t)vactive_di_null;
            ch->transfer_count = vactive_di_null_len;
        }
    }

    // 2. Record which active line the next IRQ has to supply pixels for.
    //    Nothing else happens here: the pixel data comes straight out of the
    //    framebuffer, so there is no line to fill and no callback to run.
    //    IRQs are serialised by the NVIC, so the next one cannot start until
    //    this one returns.
    (void)v_scanline;
    fb_pending_line = active_line;
}

static inline void __not_in_flash_func(video_output_handle_blanking)(dma_channel_hw_t *ch, uint32_t v_scanline, bool send_acr, bool dma_pong)
{
    if (dvi_mode) {
        // DVI mode: HDMI-structured blanking cmdlist with a null DI packet
        // (no audio / no ACR / no AVI InfoFrame). See video_output_handle_vsync
        // for why the cmdlist shape is shared with HDMI mode.
        (void)send_acr;
        (void)dma_pong;
        (void)v_scanline;
        ch->read_addr = (uintptr_t)vblank_di_null;
        ch->transfer_count = vblank_di_null_len;
    } else {
        if (send_acr) {
            ch->read_addr = (uintptr_t)vblank_acr_vsync_off;
            ch->transfer_count = vblank_acr_vsync_off_len;
        } else if (v_scanline == 0) {
            ch->read_addr = (uintptr_t)vblank_avi_infoframe;
            ch->transfer_count = vblank_avi_infoframe_len;
        } else {
            const uint32_t *di_words = hstx_di_queue_get_audio_packet();
            if (di_words) {
                uint32_t *buf = dma_pong ? vblank_di_ping : vblank_di_pong;
                vblank_di_len = build_line_with_di(buf, di_words, false, false);
                ch->read_addr = (uintptr_t)buf;
                ch->transfer_count = vblank_di_len;
            } else {
                ch->read_addr = (uintptr_t)vblank_di_null;
                ch->transfer_count = vblank_di_null_len;
            }
        }
    }
}

static inline void __not_in_flash_func(video_output_handle_active_data)(dma_channel_hw_t *ch)
{
    // One RGB332 byte per pixel, four pixels per 32-bit FIFO word. The row
    // address was resolved once when the mapping was set, so a border line
    // and a picture line cost the same here.
    ch->read_addr = (uintptr_t)fb_row_addr[fb_pending_line];
    ch->transfer_count = fb_stride_words;
}

// ============================================================================
// DMA IRQ Handler
// ============================================================================

void __not_in_flash_func(dma_irq_handler)(void)
{
    #if HSTX_DEBUG
    irq_count++;
    #endif
    uint32_t ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1U << ch_num;
    dma_pong = !dma_pong;

    // Advance audio/data-island scheduler exactly once per scanline (HDMI mode only)
    if (!dvi_mode && !vactive_cmdlist_posted) {
        hstx_di_queue_tick();
    }

    scanline_state_t state;
    get_scanline_state(v_scanline, &state);

    if (state.vsync_active) {
        video_output_handle_vsync(ch, v_scanline);
    } else if (state.active_video && !vactive_cmdlist_posted) {
        video_output_handle_active_start(ch, v_scanline, state.active_line, dma_pong);
        vactive_cmdlist_posted = true;
    } else if (state.active_video && vactive_cmdlist_posted) {
        video_output_handle_active_data(ch);
        vactive_cmdlist_posted = false;
    } else {
        video_output_handle_blanking(ch, v_scanline, state.send_acr, dma_pong);
    }
    if (!vactive_cmdlist_posted)
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
}

// ============================================================================
// Public Interface
// ============================================================================

// ACR N/CTS lookup for the 27.0 MHz pixel clock of 720x576p50 (HDMI spec
// Table 7-1/7-2). N is the spec's recommended value per rate; CTS follows
// from CTS = pixel_clock * N / (128 * sample_rate) and comes out an exact
// integer at 27 MHz for every rate below, which is what lets a sink lock its
// audio clock without drift.
static void get_acr_params(uint32_t sample_rate, uint32_t *n, uint32_t *cts)
{
    switch (sample_rate) {
        case 32000:
            *n = 4096;
            *cts = 27000;
            break;
        case 44100:
            *n = 6272;
            *cts = 30000;
            break;
        case 48000:
            *n = 6144;
            *cts = 27000;
            break;
        case 88200:
            *n = 12544;
            *cts = 30000;
            break;
        case 96000:
            *n = 12288;
            *cts = 27000;
            break;
        case 176400:
            *n = 25088;
            *cts = 30000;
            break;
        case 192000:
            *n = 24576;
            *cts = 27000;
            break;
        default:
            *n = 6144;
            *cts = 27000;
            break; // fallback to 48kHz
    }
}

// Last rate handed to configure_audio_packets. Lets hstx_restart_core1
// restore the caller-configured rate instead of resetting to 44.1 kHz
// (pico_snesPlus runs 32 kHz).
static uint32_t configured_audio_sample_rate = 48000;

uint32_t pico_hdmi_get_audio_sample_rate(void)
{
    return configured_audio_sample_rate;
}

static void configure_audio_packets(uint32_t sample_rate)
{
    configured_audio_sample_rate = sample_rate;
    hstx_di_queue_set_sample_rate(sample_rate);
    // Keep the IEC 60958 channel-status sample-frequency code in lockstep
    // with ACR and the Audio InfoFrame — strict sinks require all three to
    // agree with the actual stream rate.
    hstx_packet_set_cs_sample_rate(sample_rate);

    hstx_packet_t packet;
    hstx_data_island_t island;

    uint32_t acr_n;
    uint32_t acr_cts;
    get_acr_params(sample_rate, &acr_n, &acr_cts);
    hstx_packet_set_acr(&packet, acr_n, acr_cts);
    hstx_encode_data_island(&island, &packet, true, true);
    vblank_acr_vsync_on_len = build_line_with_di(vblank_acr_vsync_on, island.words, true, false);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_acr_vsync_off_len = build_line_with_di(vblank_acr_vsync_off, island.words, false, false);

    hstx_packet_set_audio_infoframe(&packet, sample_rate, 2, 16);
    hstx_encode_data_island(&island, &packet, true, true);
    vblank_infoframe_vsync_on_len = build_line_with_di(vblank_infoframe_vsync_on, island.words, true, false);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_infoframe_vsync_off_len = build_line_with_di(vblank_infoframe_vsync_off, island.words, false, false);
}

void video_output_init(uint16_t width, uint16_t height)
{
    frame_width = width;
    frame_height = height;

    // clk_hstx is the caller's job, not this driver's. Upstream derived it
    // from clk_sys with a divider, which cannot produce 135 MHz from the
    // 252 MHz this emulator runs at; hstx_clock_init() in HDMI_hstx.c takes
    // it from a retasked pll_usb instead. Fail loudly rather than paint a
    // picture at the wrong line rate.
    hard_assert(clock_get_hz(clk_hstx) == MODE_HSTX_CLK_HZ);

    // Park the active region on black until a framebuffer is registered.
    video_output_set_framebuffer(NULL, MODE_H_ACTIVE_PIXELS, 0, 0, 1, NULL);

    // Claim DMA channels for HSTX (channels 0 and 1)
    dma_channel_claim(DMACH_PING);
    dma_channel_claim(DMACH_PONG);

    // Initialize HDMI audio packets (default 48kHz)
    configure_audio_packets(48000);

    hstx_packet_t packet;
    hstx_data_island_t island;

    // VIC 17 is 720x576p50 4:3. No pixel repetition: the DMA reads one
    // framebuffer byte per output pixel.
    hstx_packet_set_avi_infoframe(&packet, 17, 0);
    hstx_encode_data_island(&island, &packet, false, true);
    vblank_avi_infoframe_len = build_line_with_di(vblank_avi_infoframe, island.words, false, false);

    vblank_di_null_len = build_line_with_di(vblank_di_null, hstx_get_null_data_island(false, true), false, false);
    vactive_di_null_len = build_line_with_di(vactive_di_null, hstx_get_null_data_island(false, true), false, true);

    // Vsync-active variant for DVI mode's two vsync lines (HDMI mode uses
    // the ACR/InfoFrame cmdlists instead). Built with vsync=true so the sync
    // symbols carry VSYNC asserted, and the embedded null DI also has its
    // vsync-active TERC4 encoding.
    vblank_di_null_vsync_on_len = build_line_with_di(vblank_di_null_vsync_on, hstx_get_null_data_island(true, true), true, false);

    vblank_di_len = build_line_with_di(vblank_di_ping, hstx_get_null_data_island(false, true), false, false);
    memcpy(vblank_di_pong, vblank_di_ping, sizeof(vblank_di_ping));
}

// Tear-down counterpart to video_output_init / video_output_core1_run.
// Disables and releases everything those two install so the same hardware
// can be brought back up by a fresh init pair. Required when the caller
// wants to reset core1 with a different stack at runtime — the SDK's
// dma_channel_claim and irq_set_exclusive_handler both panic on the second
// install if the previous claim / handler wasn't released.
//
// Call ordering: caller must already have stopped feeding the audio path
// (background_task = NULL) and be on core0 — core1 will be reset by the
// caller after this returns.
void video_output_stop(void)
{
    // Quiet the IRQ first so the DMA abort that follows can't trigger a
    // half-state in dma_irq_handler.
    irq_set_enabled(DMA_IRQ_1, false);

    // Disable the DMA channels' EN bit before aborting (matches the safe
    // pattern used by hstx_resync above).
    hw_clear_bits(&dma_hw->ch[DMACH_PING].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[DMACH_PONG].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(DMACH_PING);
    dma_channel_abort(DMACH_PONG);
    dma_hw->ints1 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    dma_hw->inte1 &= ~((1U << DMACH_PING) | (1U << DMACH_PONG));

    // Park HSTX. video_output_core1_run will re-program CSR on the re-launch.
    hstx_ctrl_hw->csr &= ~HSTX_CTRL_CSR_EN_BITS;

    // Hand the channels and IRQ back to the SDK so the next init can
    // re-claim them without panicking.
    irq_remove_handler(DMA_IRQ_1, dma_irq_handler);
    dma_channel_unclaim(DMACH_PING);
    dma_channel_unclaim(DMACH_PONG);

    // Reset latched state in our own driver layer that the next init may
    // consult on the way back up.
    resync_requested = false;
}

void video_output_set_background_task(video_output_task_fn task)
{
    background_task = task;
}

void video_output_request_resync(void)
{
    resync_requested = true;
}

bool video_output_get_dvi_mode(void)
{
    return dvi_mode;
}

void video_output_set_dvi_mode(bool enabled)
{
    dvi_mode = enabled;
}

void video_output_set_framebuffer(const uint8_t *fb, uint32_t stride, uint32_t rows, uint32_t v_offset,
                                  uint32_t v_scale, const uint8_t *border_row)
{
    // The video data period is exactly MODE_H_ACTIVE_PIXELS wide, so a row
    // has to be that wide too: a pillarbox border is black pixels inside the
    // row, not a shorter DMA transfer.
    if (stride != MODE_H_ACTIVE_PIXELS || (stride & 3u) != 0)
        return;
    if (v_scale == 0)
        v_scale = 1;

    const uint8_t *border = border_row ? border_row : blank_row;

    // Resolve every raster line to a source address once, here, rather than
    // dividing in the IRQ. A line outside the picture window gets the border
    // row, which is also what happens to every line when fb is NULL.
    for (uint32_t line = 0; line < MODE_V_ACTIVE_LINES; line++) {
        const uint8_t *addr = border;
        if (fb && line >= v_offset) {
            uint32_t row = (line - v_offset) / v_scale;
            if (row < rows)
                addr = fb + (size_t)row * stride;
        }
        fb_row_addr[line] = addr;
    }

    fb_stride_words = stride / 4;
}

void video_output_set_vsync_callback(video_output_vsync_cb_t cb)
{
    vsync_callback = cb;
}

// __not_in_flash_func: this function's while(1) loop runs on core1 forever.
// When core0 calls flash_range_erase / flash_range_program, XIP is briefly
// disabled and any core1 instruction fetch from flash stalls or returns
// garbage. Keeping the whole function (including the one-time hardware-init
// prologue) in SRAM costs ~700 bytes but lets the bootloader's progress bar
// keep updating on screen while flashing a new emulator. All callees from
// the steady-state loop (time_us_32 inline, hstx_resync, dma_irq_handler,
// scanline_callback, etc.) are themselves __not_in_flash_func; the only
// flash-resident calls left here (printf, irq_set_enabled) sit inside the
// rare resync path, which is not expected to fire during a normal flash
// write -- and would be the same risk as before this attribute was added.
void __not_in_flash_func(video_output_core1_run)(void)
{
    // TMDS encoder set up for RGB332, one byte per pixel, laid out RRRGGGBB.
    //
    // Each lane right-rotates the shifter by its own ROT and then takes
    // NBITS+1 bits down from bit 7. Lane 2 is red, lane 1 green, lane 0 blue.
    // So red needs no rotate, green rotates its bit 4 up to bit 7 (a left
    // rotate of 3, written as a right rotate of 29), and blue rotates its
    // bit 1 up to bit 7 (left 6, written as right 26).
    //
    // Full-scale white is 0xFF and a primary is 224 of 255. That is what
    // makes 8bpp worth the memory over a 4bpp mode, where one valid bit per
    // channel yields 0x80 and the whole picture comes out at half intensity.
    hstx_ctrl_hw->expand_tmds =
        26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB |
        1 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
        2 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
        2 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB;
    // Four 8-bit pixels per FIFO word. Control symbols (RAW) are an entire
    // 32-bit word, as before.
    hstx_ctrl_hw->expand_shift =
        4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    // The output shifter is unchanged by the pixel format: a TMDS symbol is
    // 10 bits, two bits leave per cycle because the pins are DDR, so a pixel
    // is 5 shifts of 2 bits whatever went in.
    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS | (uint32_t)MODE_HSTX_CSR_CLKDIV << HSTX_CTRL_CSR_CLKDIV_LSB |
                        5U << HSTX_CTRL_CSR_N_SHIFTS_LSB | 2U << HSTX_CTRL_CSR_SHIFT_LSB | HSTX_CTRL_CSR_EN_BITS;

    int clk_bit_p = HSTX_BIT_FROM_GPIO(GPIOHSTXCK);
    int clk_bit_n = GPIOHSTXINVERTED ? (clk_bit_p - 1) : (clk_bit_p + 1);
    hstx_ctrl_hw->bit[clk_bit_p] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[clk_bit_n] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    // hstx_ctrl_hw->bit[0] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    // hstx_ctrl_hw->bit[1] = HSTX_CTRL_BIT0_CLK_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
          // For each TMDS lane, assign it to the correct GPIO pair based on the desired pinout:

        // lane_to_output_bit Array:
        // The lane_to_output_bit array specifies which HSTX output bits are used for each TMDS lane, based on the GPIOHSTXDx defines:

        // Index 0: TMDS lane D0 (data lane 0) → GPIOHSTXD0 (D0+), (GPIOHSTXD0 + GPIOHSTXINVERTED ? -1 : +1) (D0-)
        // Index 1: TMDS lane D1 (data lane 1) → GPIOHSTXD1 (D1+), (GPIOHSTXD1 + GPIOHSTXINVERTED ? -1 : +1) (D1-)
        // Index 2: TMDS lane D2 (data lane 2) → GPIOHSTXD2 (D2+), (GPIOHSTXD2 + GPIOHSTXINVERTED ? -1 : +1) (D2-)

        // Example default mapping for Adafruit Metro RP2350:
        // D0+ = GPIOHSTXD0 (default: GPIO18), D0- = GPIOHSTXD0 + 1 (default: GPIO19)
        // D1+ = GPIOHSTXD1 (default: GPIO16), D1- = GPIOHSTXD1 + 1 (default: GPIO17)
        // D2+ = GPIOHSTXD2 (default: GPIO12), D2- = GPIOHSTXD2 + 1 (default: GPIO13)
        // If GPIOHSTXINVERTED is set, D- is D+ - 1 instead of D+ + 1
        // See https://learn.adafruit.com/adafruit-metro-rp2350/pinouts#hstx-connector-3193107
        static const int lane_to_output_bit[3] = {
            HSTX_BIT_FROM_GPIO(GPIOHSTXD0),
            HSTX_BIT_FROM_GPIO(GPIOHSTXD1),
            HSTX_BIT_FROM_GPIO(GPIOHSTXD2)};
        int bit = lane_to_output_bit[lane];
        int bit_dn = GPIOHSTXINVERTED ? (bit - 1) : (bit + 1);
        // Output even bits during first half of each HSTX cycle, and odd bits
        // during second half. The shifter advances by two bits each cycle.
        uint32_t lane_data_sel_bits =
            (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        // The two halves of each pair get identical data, but one pin is inverted.
        hstx_ctrl_hw->bit[bit] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit_dn] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    // Set the eight HSTX GPIOs to the HSTX function (function 0 on RP2350)
    for (int i = HSTX_GPIO_BASE; i <= HSTX_GPIO_BASE + 7; ++i) {
        gpio_set_function(i, 0);
        gpio_set_slew_rate(i, GPIO_SLEW_RATE_FAST);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_12MA);
    }

    // DMA Setup
    dma_channel_config c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PING, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off),
                          false);

    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(DMACH_PONG, &c, &hstx_fifo_hw->fifo, vblank_line_vsync_off, count_of(vblank_line_vsync_off),
                          false);

    dma_hw->ints1 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    dma_hw->inte1 = (1U << DMACH_PING) | (1U << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_priority(DMA_IRQ_1, 0);
    irq_set_enabled(DMA_IRQ_1, true);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    dma_channel_start(DMACH_PING);

    // Watchdog: if video_frame_count stops advancing (e.g. DMA chain wedged
    // after an IRQ overrun, HSTX FIFO underflow), hstx_resync() brings the
    // pipeline back up without a reboot. 500 ms is ~30 frames at 60 Hz, long
    // enough not to false-trigger at boot but short enough to recover quickly.
    // time_us_32 wraps every ~71 min, irrelevant for the 500 ms window and
    // ~10x cheaper than time_us_64 in a hot loop.
    #define VIDEO_OUTPUT_WATCHDOG_US 500000u

    // Rate-limit watchdog: no real display exceeds ~65 Hz vertical. If
    // video_frame_count advances above 75 fps, DMA chain state has drifted
    // (observed ~158 Hz in DVI mode); trigger a full hstx_resync() to
    // recover. A soft HSTX CSR toggle was tested and does not clear it.
    #define VIDEO_OUTPUT_OVERRATE_FPS 75u
    uint32_t last_frame_us = time_us_32();
    uint32_t last_frame_count_seen = video_frame_count;
    uint32_t overrate_last_us = last_frame_us;
    uint32_t overrate_last_frames = last_frame_count_seen;
#if HSTX_DEBUG
    // 1 Hz rate-diagnostic baseline
    uint32_t rate_last_us = last_frame_us;
    uint32_t rate_last_frames = last_frame_count_seen;
    uint32_t rate_last_irqs = irq_count;
#endif
    while (1) {
        // ----------------------------------------------------------------
        // HSTX recovery loop
        //
        // The HSTX/DMA pipeline can wedge in two distinct failure modes that
        // both manifest as the monitor losing signal. Two cooperating
        // watchdogs detect them; both feed into a single recovery path that
        // calls hstx_resync() to rebuild the DMA chain from scratch.
        //
        //   1. Stuck-frame watchdog (per-iteration check)
        //      video_frame_count stops advancing — typical after a DMA IRQ
        //      overrun or HSTX FIFO underflow leaves the ping/pong chain in
        //      an inconsistent state. Detected when no new frame has been
        //      observed for VIDEO_OUTPUT_WATCHDOG_US (~500 ms).
        //
        //   2. Over-rate watchdog (1 Hz sampling window)
        //      video_frame_count advances faster than any real display can
        //      sync to (>75 fps; ~158 Hz observed in DVI mode after the chain
        //      drifts). Latches resync_requested for the next iteration.
        //
        // Originally these checks were gated on dvi_mode (signal loss was
        // only ever observed in DVI), but they are now run unconditionally
        // for HDMI as a safety net.
        //
        // hstx_resync() is the only recovery that has been shown to work;
        // a soft HSTX CSR toggle was tested and fails reliably.
        //
        // All elapsed-time comparisons use uint32_t subtraction, which is
        // safe across the ~71 min wrap of time_us_32().
        // ----------------------------------------------------------------

        uint32_t now = time_us_32();
        uint32_t current_count = video_frame_count;

        // Stuck-frame watchdog: refresh the "last progress" timestamp
        // whenever the IRQ-driven frame counter has moved forward.
        if (current_count != last_frame_count_seen)
        {
            last_frame_count_seen = current_count;
            last_frame_us = now;
        }

#if HSTX_DEBUG
        // 1 Hz diagnostic dump: frame/IRQ rates plus a snapshot of every
        // clock and HSTX/DMA register relevant to debugging a wedge.
        if ((now - rate_last_us) >= 1000000u)
        {
            uint32_t d_us = now - rate_last_us;
            uint32_t d_frames = current_count - rate_last_frames;
            uint32_t d_irqs = irq_count - rate_last_irqs;
            printf("video rate: %lu frames/s, %lu irqs/s (dvi=%d) clk_sys=%lu clk_hstx=%lu csr=%08lx resync=%d di_lvl=%lu underrun=%lu\n",
                   (unsigned long)((uint64_t)d_frames * 1000000u / d_us),
                   (unsigned long)((uint64_t)d_irqs * 1000000u / d_us),
                   dvi_mode ? 1 : 0,
                   (unsigned long)clock_get_hz(clk_sys),
                   (unsigned long)clock_get_hz(clk_hstx),
                   (unsigned long)hstx_ctrl_hw->csr,
                   resync_count,
                   (unsigned long)hstx_di_queue_get_level(),
                   (unsigned long)hstx_di_queue_get_underrun_count());
            printf("  hstx_div=%08lx exp_sh=%08lx exp_tmds=%08lx ping_ctrl=%08lx pong_ctrl=%08lx\n",
                   (unsigned long)clocks_hw->clk[clk_hstx].div,
                   (unsigned long)hstx_ctrl_hw->expand_shift,
                   (unsigned long)hstx_ctrl_hw->expand_tmds,
                   (unsigned long)dma_hw->ch[DMACH_PING].al1_ctrl,
                   (unsigned long)dma_hw->ch[DMACH_PONG].al1_ctrl);
            uint32_t fc_sys_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
            uint32_t fc_hstx_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_HSTX);
            printf("  sys_ctrl=%08lx sys_div=%08lx pll_fbdiv=%lu pll_prim=%08lx fc_sys=%lu kHz fc_hstx=%lu kHz\n",
                   (unsigned long)clocks_hw->clk[clk_sys].ctrl,
                   (unsigned long)clocks_hw->clk[clk_sys].div,
                   (unsigned long)pll_sys_hw->fbdiv_int,
                   (unsigned long)pll_sys_hw->prim,
                   (unsigned long)fc_sys_khz,
                   (unsigned long)fc_hstx_khz);
            rate_last_us = now;
            rate_last_frames = current_count;
            rate_last_irqs = irq_count;
        }
#endif

        // Over-rate watchdog: once per second, compute fps over the elapsed
        // window. Above VIDEO_OUTPUT_OVERRATE_FPS the DMA chain has drifted
        // and the recovery is queued for this iteration's resync block.
        if ((now - overrate_last_us) >= 1000000u)
        {
            uint32_t d_us = now - overrate_last_us;
            uint32_t d_frames = current_count - overrate_last_frames;
            uint32_t fps = (uint32_t)((uint64_t)d_frames * 1000000u / d_us);
            if (fps > VIDEO_OUTPUT_OVERRATE_FPS)
            {
                resync_requested = true;
            }
            overrate_last_us = now;
            overrate_last_frames = current_count;
        }

        // Trigger recovery if either watchdog has fired. The DMA IRQ is
        // masked across hstx_resync() so it cannot observe the chain in a
        // half-rebuilt state, then all watchdog baselines are reset so the
        // next iteration measures fresh post-recovery progress.
        bool do_resync = resync_requested ||
                         (now - last_frame_us) > VIDEO_OUTPUT_WATCHDOG_US;

        if (do_resync)
        {
            resync_count++;
            irq_set_enabled(DMA_IRQ_1, false);
            hstx_resync();
            resync_requested = false;
            irq_set_enabled(DMA_IRQ_1, true);
            last_frame_us = time_us_32();
            last_frame_count_seen = video_frame_count;
            overrate_last_us = last_frame_us;
            overrate_last_frames = video_frame_count;
            printf("HSTX resync performed! Total resyncs since boot: %d\n", resync_count);
        }

        if (background_task) {
            background_task();
        }
        tight_loop_contents();
    }
}

void pico_hdmi_set_audio_sample_rate(uint32_t sample_rate)
{
    configure_audio_packets(sample_rate);
}

/// @brief Returns the number of times the video output has auto-resynced
/// (via hstx_resync()) since boot. This can be used to detect if the output is having trouble
/// keeping up and is frequently resyncing.
/// @param  
/// @return /
int get_video_output_resync_count(void)
{
    return resync_count;
}


