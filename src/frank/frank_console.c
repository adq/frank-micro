/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_console.c — inject keystrokes into the emulator over the USB-CDC
 *                   serial console (for autonomous testing / headless control).
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Protocol (one byte per command, read from stdin / USB CDC):
 *   - printable ASCII letters/digits/space: tapped (down for a few frames,
 *     then auto-released) so they register as a normal key press.
 *   - arrow keys via a tiny scheme (we can't send raw arrows over a TTY):
 *       'h' = LEFT   'l' = RIGHT   'k' = UP   'j' = DOWN   (vi-style)
 *       uppercase H/L/K/J = same but with SHIFT held (PoP "careful" move)
 *   - 's' = SHIFT tap, 'S' = hold SHIFT, 'u' = release SHIFT
 *   - '\r'/'\n' = RETURN, ' ' = SPACE, 27 = ESCAPE
 *   - '1'..'0' map to the BBC digit row
 *   - '.' = report status line to stdout
 *
 * Each tapped key is held for FRANK_TAP_FRAMES frames then released, so a
 * single byte produces a clean press/release the BBC keyboard scan sees.
 */
#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"

#include "frank_console.h"

/* ALLEGRO_KEY_* codes (from stub allegro5/allegro.h). */
enum {
    AK_A = 1, AK_0 = 27,
    AK_F1 = 47, AK_ESCAPE = 59, AK_ENTER = 67, AK_SPACE = 75,
    AK_LEFT = 82, AK_RIGHT = 83, AK_UP = 84, AK_DOWN = 85,
    AK_LSHIFT = 215, AK_LCTRL = 217,
};

extern void key_down(int code);
extern void key_up(int code);

#define FRANK_TAP_FRAMES   4
#define MAX_TAPS           8

struct tap { int code; int frames_left; };
static struct tap s_taps[MAX_TAPS];
static bool s_shift_held = false;

static void start_tap(int code) {
    for (int i = 0; i < MAX_TAPS; i++) {
        if (s_taps[i].frames_left == 0) {
            s_taps[i].code = code;
            s_taps[i].frames_left = FRANK_TAP_FRAMES;
            key_down(code);
            return;
        }
    }
}

static int ascii_to_allegro(int c, bool *shift) {
    *shift = false;
    if (c >= 'a' && c <= 'z') return AK_A + (c - 'a');
    if (c >= 'A' && c <= 'Z') { *shift = true; return AK_A + (c - 'A'); }
    if (c >= '1' && c <= '9') return AK_0 + (c - '0');
    if (c == '0') return AK_0;
    if (c == ' ') return AK_SPACE;
    if (c == '\r' || c == '\n') return AK_ENTER;
    if (c == 27) return AK_ESCAPE;
    return 0;
}

void frank_console_poll(void) {
    /* Advance/expire active taps. */
    for (int i = 0; i < MAX_TAPS; i++) {
        if (s_taps[i].frames_left > 0) {
            if (--s_taps[i].frames_left == 0)
                key_up(s_taps[i].code);
        }
    }

    /* Drain all pending serial input this frame. */
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        switch (c) {
            /* vi-style movement (lower = plain, upper = with SHIFT). */
            case 'h': start_tap(AK_LEFT);  break;
            case 'l': start_tap(AK_RIGHT); break;
            case 'k': start_tap(AK_UP);    break;
            case 'j': start_tap(AK_DOWN);  break;
            case 'H': key_down(AK_LSHIFT); start_tap(AK_LEFT);  key_up(AK_LSHIFT); break;
            case 'L': key_down(AK_LSHIFT); start_tap(AK_RIGHT); key_up(AK_LSHIFT); break;
            case 'K': key_down(AK_LSHIFT); start_tap(AK_UP);    key_up(AK_LSHIFT); break;
            case 'J': key_down(AK_LSHIFT); start_tap(AK_DOWN);  key_up(AK_LSHIFT); break;
            /* SHIFT control. */
            case 'S': if (!s_shift_held) { key_down(AK_LSHIFT); s_shift_held = true; } break;
            case 'u': if (s_shift_held)  { key_up(AK_LSHIFT);  s_shift_held = false; } break;
            case '`': printf("CONSOLE: alive\n"); break;
            default: {
                bool shift;
                int code = ascii_to_allegro(c, &shift);
                if (code) {
                    if (shift) key_down(AK_LSHIFT);
                    start_tap(code);
                    if (shift) key_up(AK_LSHIFT);
                }
                break;
            }
        }
    }
}
