/*
 * frank-micro — BBC Micro for RP2350
 * platform.c — BBC Pico platform hooks.
 *
 * This file:
 *  - Provides micro_frame_present() called from pico_vsync_handler()
 *    (injected into bbc.c's framebuffer ready callback)
 *  - Bridges PS/2 keyboard to beebjit's keyboard_system_key_pressed/released
 *  - Provides the audio ring buffer push functions for os_sound_pico.c
 */
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "board_config.h"
#include "HDMI.h"
#include "ps2kbd_wrapper.h"
#include "micro_settings.h"
#include "micro_ui.h"
#include "micro_loader.h"
#include "micro_serial_console.h"
#include "micro_keys.h"
#include "ui_draw.h"
#include "bbc.h"
#include "keyboard.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

extern uint8_t *SCREEN[2];
extern volatile uint32_t current_buffer;

/* BBC struct, set at init */
extern struct bbc_struct* g_p_bbc;

/* ── Palette ─────────────────────────────────────────────────────────────── */

/* BBC physical colours 0-7 → RGB888 */
static const uint32_t k_bbc_pal[8] = {
    0x000000, /* 0 BLACK   */
    0xFF0000, /* 1 RED     */
    0x00FF00, /* 2 GREEN   */
    0xFFFF00, /* 3 YELLOW  */
    0x0000FF, /* 4 BLUE    */
    0xFF00FF, /* 5 MAGENTA */
    0x00FFFF, /* 6 CYAN    */
    0xFFFFFF, /* 7 WHITE   */
};

void micro_init_palette(void) {
    for (int i = 0; i < 8; i++) {
        graphics_set_palette((uint8_t)i,        k_bbc_pal[i]);
        /* Flash variants (same colour, toggled by flash_on flag). */
        graphics_set_palette((uint8_t)(i + 8),  k_bbc_pal[i]);
    }

    /* Teletext anti-aliasing ramp (must match TT_AA_BASE/TT_AA_LEVELS in
     * pico_render.c): for each BBC colour 1..7 a fractional black→colour
     * blend in 8 levels, used to preserve SAA5050 glyph edge smoothing. */
    #define TT_AA_BASE   64
    #define TT_AA_LEVELS 8
    for (int c = 1; c <= 7; c++) {
        uint32_t col = k_bbc_pal[c];
        uint32_t cr = (col >> 16) & 0xff;
        uint32_t cg = (col >> 8)  & 0xff;
        uint32_t cb =  col        & 0xff;
        for (int l = 0; l < TT_AA_LEVELS; l++) {
            uint32_t r = (cr * l) / (TT_AA_LEVELS - 1);
            uint32_t g = (cg * l) / (TT_AA_LEVELS - 1);
            uint32_t b = (cb * l) / (TT_AA_LEVELS - 1);
            uint8_t idx = (uint8_t)(TT_AA_BASE + (c - 1) * TT_AA_LEVELS + l);
            graphics_set_palette(idx, (r << 16) | (g << 8) | b);
        }
    }
    #undef TT_AA_BASE
    #undef TT_AA_LEVELS

    /* UI colours (indices 240+) */
    ui_draw_install_palette();
    graphics_set_bgcolor(0x000000);
}

/* ── Frame present ─────────────────────────────────────────────────────────  */

static uint64_t g_next_frame_us = 0;
#define FRAME_PERIOD_US 20000u  /* 50 Hz */

void micro_frame_present(void) {
    uint8_t* fb = SCREEN[current_buffer ^ 1];
    /* Draw OSD if visible */
    if (micro_ui_is_visible())
        micro_ui_render(fb, MICRO_FB_WIDTH, MICRO_SCREEN_LINES);

    /* Flip */
    graphics_set_buffer(fb);
    current_buffer ^= 1u;

    /* Speed limiting is handled by bbc_do_sleep() inside the BBC timing loop
     * (bbc_cycles_timer_callback, 500Hz).  Do NOT sleep here — this function
     * runs inside the BBC callback chain and any sleep here would interfere
     * with bbc_do_sleep's pacing, causing double-sleeping and apparent
     * ~28% speed (20ms BBC sleep + 20ms micro_frame_present sleep per frame). */

    /* Serial console */
    micro_serial_poll();
}

/* ── PS/2 → beebjit keyboard bridge ─────────────────────────────────────── */

/* PS/2 scan code set 2 → beebjit key codes.
 * beebjit uses its own key constants for special keys (>127),
 * and ASCII for printable keys. */

/* Convert Duke3D scancode (from ps2kbd_get_key) to beebjit key code.
 * The Duke3D codes are US PC scancodes: sc_A=0x1E, sc_Q=0x10, etc.
 * beebjit expects uppercase letters and its own special key constants. */
static int duke3d_to_beebjit(uint8_t sc) {
    /* Alphabet keys — sc_A=0x1E..sc_Z (non-sequential — use switch) */
    switch (sc) {
    /* Letters */
    case 0x1e: return 'A';
    case 0x30: return 'B';
    case 0x2e: return 'C';
    case 0x20: return 'D';
    case 0x12: return 'E';
    case 0x21: return 'F';
    case 0x22: return 'G';
    case 0x23: return 'H';
    case 0x17: return 'I';
    case 0x24: return 'J';
    case 0x25: return 'K';
    case 0x26: return 'L';
    case 0x32: return 'M';
    case 0x31: return 'N';
    case 0x18: return 'O';
    case 0x19: return 'P';
    case 0x10: return 'Q';
    case 0x13: return 'R';
    case 0x1f: return 'S';
    case 0x14: return 'T';
    case 0x16: return 'U';
    case 0x2f: return 'V';
    case 0x11: return 'W';
    case 0x2d: return 'X';
    case 0x15: return 'Y';
    case 0x2c: return 'Z';
    /* Digits */
    case 0x02: return '1';
    case 0x03: return '2';
    case 0x04: return '3';
    case 0x05: return '4';
    case 0x06: return '5';
    case 0x07: return '6';
    case 0x08: return '7';
    case 0x09: return '8';
    case 0x0a: return '9';
    case 0x0b: return '0';
    /* Symbols */
    case 0x33: return ',';
    case 0x34: return '.';
    case 0x35: return '/';
    case 0x0c: return '-';
    case 0x0d: return '=';
    case 0x1a: return '[';
    case 0x1b: return ']';
    case 0x2b: return '\\';
    case 0x27: return ';';
    case 0x28: return '\'';
    case 0x29: return '`';   /* BBC @ */
    case 0x39: return ' ';
    /* Special keys */
    case 0x01: return k_keyboard_key_escape;
    case 0x0e: return k_keyboard_key_backspace;
    case 0x0f: return k_keyboard_key_tab;
    case 0x1c: return k_keyboard_key_enter;
    case 0x68: return k_keyboard_key_enter;  /* kpad enter */
    case 0x1d: return k_keyboard_key_ctrl;
    case 0x66: return k_keyboard_key_ctrl;   /* right ctrl */
    case 0x2a: return k_keyboard_key_shift_left;
    case 0x36: return k_keyboard_key_shift_right;
    case 0x3a: return k_keyboard_key_caps_lock;
    case 0x38: return k_keyboard_key_alt_left;
    case 0x65: return k_keyboard_key_alt_left;
    /* Function keys */
    case 0x3b: return k_keyboard_key_f1;
    case 0x3c: return k_keyboard_key_f2;
    case 0x3d: return k_keyboard_key_f3;
    case 0x3e: return k_keyboard_key_f4;
    case 0x3f: return k_keyboard_key_f5;
    case 0x40: return k_keyboard_key_f6;
    case 0x41: return k_keyboard_key_f7;
    case 0x42: return k_keyboard_key_f8;
    case 0x43: return k_keyboard_key_f9;
    case 0x44: return k_keyboard_key_f0;  /* F10 = f0 in BBC */
    case 0x57: return k_keyboard_key_f11;
    case 0x58: return k_keyboard_key_f12; /* BBC BREAK */
    /* Arrow keys */
    case 0x5a: return k_keyboard_key_arrow_up;
    case 0x6a: return k_keyboard_key_arrow_down;
    case 0x6b: return k_keyboard_key_arrow_left;
    case 0x6c: return k_keyboard_key_arrow_right;
    /* Other nav */
    case 0x61: return k_keyboard_key_home;
    case 0x62: return k_keyboard_key_end;
    case 0x63: return k_keyboard_key_page_up;
    case 0x64: return k_keyboard_key_page_down;
    case 0x5f: return k_keyboard_key_delete;
    default:   return -1;
    }
}

void micro_keyboard_poll(void) {
    if (!g_p_bbc) return;

    struct keyboard_struct* p_kbd = bbc_get_keyboard(g_p_bbc);
    if (!p_kbd) return;

    /* Clock the programmatic key-injection queue (serial-driven). */
    micro_key_tick(p_kbd);

    /* Process all pending PS/2 events */
    ps2kbd_tick();

    int pressed;
    unsigned char sc;
    while (ps2kbd_get_key(&pressed, &sc)) {
        /* F12 = OSD toggle */
        if (sc == 0x58) {
            if (pressed) micro_ui_toggle();
            continue;
        }

        /* OSD key handling */
        if (micro_ui_is_visible()) {
            if (pressed) micro_ui_handle_key(sc, 1);
            continue;
        }

        int bbc_key = duke3d_to_beebjit(sc);
        if (bbc_key < 0) continue;

        if (pressed)
            keyboard_system_key_pressed(p_kbd, (uint8_t)bbc_key);
        else
            keyboard_system_key_released(p_kbd, (uint8_t)bbc_key);
    }
}

/* ── I2S audio ring buffer ───────────────────────────────────────────────── */

#define AUDIO_RING_FRAMES  (1u << 11)   /* 2048 = 8KB — keep BBC heap headroom */
#define AUDIO_RING_MASK    (AUDIO_RING_FRAMES - 1)

static uint32_t __attribute__((aligned(4))) g_audio_ring[AUDIO_RING_FRAMES];
volatile uint32_t g_audio_prod = 0;
volatile uint32_t g_audio_cons = 0;

unsigned i2s_ring_push(const int16_t* samples, unsigned count) {
    uint32_t prod = g_audio_prod;
    uint32_t cons = g_audio_cons;
    uint32_t free_slots = AUDIO_RING_FRAMES - (prod - cons);
    if (count > free_slots) count = free_slots;
    for (unsigned i = 0; i < count; ++i) {
        int16_t s = samples[i];
        g_audio_ring[(prod + i) & AUDIO_RING_MASK] = ((uint32_t)(uint16_t)s << 16) | (uint16_t)s;
    }
    __dmb();
    g_audio_prod = prod + count;
    return count;
}

unsigned i2s_ring_free(void) {
    return AUDIO_RING_FRAMES - (g_audio_prod - g_audio_cons);
}

/* Core 1 audio render loop */
#define AUDIO_FRAMES_PER_CHUNK 882
#define AUDIO_SAMPLE_RATE      44100

#if defined(HDMI_PIO_AUDIO)
/* In HDMI_PIO_AUDIO mode: frank_hdmi_run_core1 already runs on Core 1.
 * Provide a separate I2S core function for VGA fallback. */
#include "audio.h"

static volatile bool s_core1_ready = false;

void __time_critical_func(micro_render_core_i2s)(void) {
    static i2s_config_t cfg;
    cfg = i2s_get_default_config();
    cfg.sample_freq     = AUDIO_SAMPLE_RATE;
    cfg.dma_trans_count = AUDIO_FRAMES_PER_CHUNK;
    i2s_volume(&cfg, 0);
    i2s_init(&cfg);

    static uint32_t __attribute__((aligned(32))) chunk[AUDIO_FRAMES_PER_CHUNK];
    __dmb();
    s_core1_ready = true;
    __dmb();

    while (true) {
        uint32_t prod = g_audio_prod;
        uint32_t cons = g_audio_cons;
        uint32_t avail = prod - cons;
        if (avail >= AUDIO_FRAMES_PER_CHUNK) {
            for (uint32_t i = 0; i < AUDIO_FRAMES_PER_CHUNK; ++i)
                chunk[i] = g_audio_ring[(cons + i) & AUDIO_RING_MASK];
            __dmb();
            g_audio_cons = cons + AUDIO_FRAMES_PER_CHUNK;
        } else {
            for (uint32_t i = 0; i < AUDIO_FRAMES_PER_CHUNK; ++i)
                chunk[i] = 0;
        }
        i2s_dma_write(&cfg, (const int16_t*)chunk);
    }
}

volatile bool* micro_core1_ready_ptr(void) { return &s_core1_ready; }
#endif
