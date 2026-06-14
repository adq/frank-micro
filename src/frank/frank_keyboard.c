/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_keyboard.c — PS/2 keyboard -> b-em key matrix.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The frank PS/2 driver (ps2kbd_get_key) yields IBM PC XT-style scancodes.
 * b-em's key_down()/key_up() expect ALLEGRO_KEY_* codes, which it maps to the
 * BBC key matrix via keylookup.  We translate XT scancode -> ALLEGRO_KEY here.
 */
#include <stdint.h>
#include "pico.h"

#include "allegro5/allegro.h"   /* ALLEGRO_KEY_* */
#include "keyboard.h"           /* key_down(), key_up() */
#include "ps2kbd_wrapper.h"

#include "frank_gui.h"

/* XT scancode (0x00..0x7f) -> ALLEGRO_KEY_*; 0 = unmapped. */
static const uint8_t xt_to_allegro[128] = {
    [0x01] = ALLEGRO_KEY_ESCAPE,
    [0x02] = ALLEGRO_KEY_1, [0x03] = ALLEGRO_KEY_2, [0x04] = ALLEGRO_KEY_3,
    [0x05] = ALLEGRO_KEY_4, [0x06] = ALLEGRO_KEY_5, [0x07] = ALLEGRO_KEY_6,
    [0x08] = ALLEGRO_KEY_7, [0x09] = ALLEGRO_KEY_8, [0x0a] = ALLEGRO_KEY_9,
    [0x0b] = ALLEGRO_KEY_0,
    [0x0c] = ALLEGRO_KEY_MINUS, [0x0d] = ALLEGRO_KEY_EQUALS,
    [0x0e] = ALLEGRO_KEY_BACKSPACE, [0x0f] = ALLEGRO_KEY_TAB,
    [0x10] = ALLEGRO_KEY_Q, [0x11] = ALLEGRO_KEY_W, [0x12] = ALLEGRO_KEY_E,
    [0x13] = ALLEGRO_KEY_R, [0x14] = ALLEGRO_KEY_T, [0x15] = ALLEGRO_KEY_Y,
    [0x16] = ALLEGRO_KEY_U, [0x17] = ALLEGRO_KEY_I, [0x18] = ALLEGRO_KEY_O,
    [0x19] = ALLEGRO_KEY_P,
    [0x1a] = ALLEGRO_KEY_OPENBRACE, [0x1b] = ALLEGRO_KEY_CLOSEBRACE,
    [0x1c] = ALLEGRO_KEY_ENTER, [0x1d] = ALLEGRO_KEY_LCTRL,
    [0x1e] = ALLEGRO_KEY_A, [0x1f] = ALLEGRO_KEY_S, [0x20] = ALLEGRO_KEY_D,
    [0x21] = ALLEGRO_KEY_F, [0x22] = ALLEGRO_KEY_G, [0x23] = ALLEGRO_KEY_H,
    [0x24] = ALLEGRO_KEY_J, [0x25] = ALLEGRO_KEY_K, [0x26] = ALLEGRO_KEY_L,
    [0x27] = ALLEGRO_KEY_SEMICOLON, [0x28] = ALLEGRO_KEY_QUOTE,
    [0x29] = ALLEGRO_KEY_TILDE, [0x2a] = ALLEGRO_KEY_LSHIFT,
    [0x2b] = ALLEGRO_KEY_BACKSLASH,
    [0x2c] = ALLEGRO_KEY_Z, [0x2d] = ALLEGRO_KEY_X, [0x2e] = ALLEGRO_KEY_C,
    [0x2f] = ALLEGRO_KEY_V, [0x30] = ALLEGRO_KEY_B, [0x31] = ALLEGRO_KEY_N,
    [0x32] = ALLEGRO_KEY_M,
    [0x33] = ALLEGRO_KEY_COMMA, [0x34] = ALLEGRO_KEY_FULLSTOP,
    [0x35] = ALLEGRO_KEY_SLASH, [0x36] = ALLEGRO_KEY_RSHIFT,
    [0x37] = ALLEGRO_KEY_PAD_ASTERISK, [0x38] = ALLEGRO_KEY_ALT,
    [0x39] = ALLEGRO_KEY_SPACE, [0x3a] = ALLEGRO_KEY_CAPSLOCK,
    [0x3b] = ALLEGRO_KEY_F1, [0x3c] = ALLEGRO_KEY_F2, [0x3d] = ALLEGRO_KEY_F3,
    [0x3e] = ALLEGRO_KEY_F4, [0x3f] = ALLEGRO_KEY_F5, [0x40] = ALLEGRO_KEY_F6,
    [0x41] = ALLEGRO_KEY_F7, [0x42] = ALLEGRO_KEY_F8, [0x43] = ALLEGRO_KEY_F9,
    [0x44] = ALLEGRO_KEY_F10,
    [0x45] = ALLEGRO_KEY_NUMLOCK, [0x46] = ALLEGRO_KEY_SCROLLLOCK,
    [0x47] = ALLEGRO_KEY_PAD_7, [0x48] = ALLEGRO_KEY_PAD_8,
    [0x49] = ALLEGRO_KEY_PAD_9, [0x4a] = ALLEGRO_KEY_PAD_MINUS,
    [0x4b] = ALLEGRO_KEY_PAD_4, [0x4c] = ALLEGRO_KEY_PAD_5,
    [0x4d] = ALLEGRO_KEY_PAD_6, [0x4e] = ALLEGRO_KEY_PAD_PLUS,
    [0x4f] = ALLEGRO_KEY_PAD_1, [0x50] = ALLEGRO_KEY_PAD_2,
    [0x51] = ALLEGRO_KEY_PAD_3, [0x52] = ALLEGRO_KEY_PAD_0,
    [0x53] = ALLEGRO_KEY_PAD_DELETE,
    [0x57] = ALLEGRO_KEY_F11, [0x58] = ALLEGRO_KEY_F12,
    [0x59] = ALLEGRO_KEY_PAUSE, [0x5a] = ALLEGRO_KEY_UP,
    [0x5e] = ALLEGRO_KEY_INSERT, [0x5f] = ALLEGRO_KEY_DELETE,
    [0x61] = ALLEGRO_KEY_HOME, [0x62] = ALLEGRO_KEY_END,
    [0x63] = ALLEGRO_KEY_PGUP, [0x64] = ALLEGRO_KEY_PGDN,
    [0x65] = ALLEGRO_KEY_ALTGR, [0x66] = ALLEGRO_KEY_RCTRL,
    [0x67] = ALLEGRO_KEY_PAD_SLASH, [0x68] = ALLEGRO_KEY_PAD_ENTER,
    [0x6a] = ALLEGRO_KEY_DOWN, [0x6b] = ALLEGRO_KEY_LEFT,
    [0x6c] = ALLEGRO_KEY_RIGHT,
};

void frank_keyboard_poll(void) {
    int pressed;
    unsigned char sc;
    while (ps2kbd_get_key(&pressed, &sc)) {
        if (sc >= 128) continue;
        int code = xt_to_allegro[sc];
        if (!code) continue;
        if (pressed)
            key_down(code);
        else
            key_up(code);
    }
}
