/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_KEYCODE_TO_ALLEGRO_H
#define B_EM_PICO_KEYCODE_TO_ALLEGRO_H

#include <cstdint>
#include <allegro5/keyboard.h>
#include <map>

void sdl_to_allegro_init();
bool get_gui_key_event(ALLEGRO_EVENT *event_out);

#if !NO_USE_KEYCODE_MAP
#if X_GUI

#include "linux/input-event-codes.h"
#include "keycodes.h"

static std::map<const uint16_t, const uint16_t> linux_to_sdl_keycodes = {
        {KEY_A,          SDL_SCANCODE_A},
        {KEY_B,          SDL_SCANCODE_B},
        {KEY_C,          SDL_SCANCODE_C},
        {KEY_D,          SDL_SCANCODE_D},
        {KEY_E,          SDL_SCANCODE_E},
        {KEY_F,          SDL_SCANCODE_F},
        {KEY_G,          SDL_SCANCODE_G},
        {KEY_H,          SDL_SCANCODE_H},
        {KEY_I,          SDL_SCANCODE_I},
        {KEY_J,          SDL_SCANCODE_J},
        {KEY_K,          SDL_SCANCODE_K},
        {KEY_L,          SDL_SCANCODE_L},
        {KEY_M,          SDL_SCANCODE_M},
        {KEY_N,          SDL_SCANCODE_N},
        {KEY_O,          SDL_SCANCODE_O},
        {KEY_P,          SDL_SCANCODE_P},
        {KEY_Q,          SDL_SCANCODE_Q},
        {KEY_R,          SDL_SCANCODE_R},
        {KEY_S,          SDL_SCANCODE_S},
        {KEY_T,          SDL_SCANCODE_T},
        {KEY_U,          SDL_SCANCODE_U},
        {KEY_V,          SDL_SCANCODE_V},
        {KEY_W,          SDL_SCANCODE_W},
        {KEY_X,          SDL_SCANCODE_X},
        {KEY_Y,          SDL_SCANCODE_Y},
        {KEY_Z,          SDL_SCANCODE_Z},

        {KEY_1,          SDL_SCANCODE_1},
        {KEY_2,          SDL_SCANCODE_2},
        {KEY_3,          SDL_SCANCODE_3},
        {KEY_4,          SDL_SCANCODE_4},
        {KEY_5,          SDL_SCANCODE_5},
        {KEY_6,          SDL_SCANCODE_6},
        {KEY_7,          SDL_SCANCODE_7},
        {KEY_8,          SDL_SCANCODE_8},
        {KEY_9,          SDL_SCANCODE_9},
        {KEY_0,          SDL_SCANCODE_0},

        {KEY_ENTER,      SDL_SCANCODE_RETURN},
        {KEY_ESC,        SDL_SCANCODE_ESCAPE},
        {KEY_BACKSPACE,  SDL_SCANCODE_BACKSPACE},
        {KEY_TAB,        SDL_SCANCODE_TAB},
        {KEY_SPACE,      SDL_SCANCODE_SPACE},

        {KEY_MINUS,      SDL_SCANCODE_MINUS},
        {KEY_EQUAL,      SDL_SCANCODE_EQUALS},
        {KEY_LEFTBRACE,  SDL_SCANCODE_LEFTBRACKET},
        {KEY_RIGHTBRACE, SDL_SCANCODE_RIGHTBRACKET},
        {KEY_BACKSLASH,  SDL_SCANCODE_BACKSLASH},
        {KEY_GRAVE,      SDL_SCANCODE_NONUSHASH}, // not sure about this

        {KEY_SEMICOLON,  SDL_SCANCODE_SEMICOLON},
        {KEY_APOSTROPHE, SDL_SCANCODE_APOSTROPHE},
        {KEY_GRAVE,      SDL_SCANCODE_GRAVE},

        {KEY_COMMA,      SDL_SCANCODE_COMMA},
        {KEY_DOT,        SDL_SCANCODE_PERIOD},
        {KEY_SLASH,      SDL_SCANCODE_SLASH},

        {KEY_CAPSLOCK,   SDL_SCANCODE_CAPSLOCK},

        {KEY_F1,         SDL_SCANCODE_F1},
        {KEY_F2,         SDL_SCANCODE_F2},
        {KEY_F3,         SDL_SCANCODE_F3},
        {KEY_F4,         SDL_SCANCODE_F4},
        {KEY_F5,         SDL_SCANCODE_F5},
        {KEY_F6,         SDL_SCANCODE_F6},
        {KEY_F7,         SDL_SCANCODE_F7},
        {KEY_F8,         SDL_SCANCODE_F8},
        {KEY_F9,         SDL_SCANCODE_F9},
        {KEY_F10,        SDL_SCANCODE_F10},
        {KEY_F11,        SDL_SCANCODE_F11},
        {KEY_F12,        SDL_SCANCODE_F12},

        {KEY_PRINT,      SDL_SCANCODE_PRINTSCREEN},
        {KEY_SCROLLLOCK, SDL_SCANCODE_SCROLLLOCK},
        {KEY_PAUSE,      SDL_SCANCODE_PAUSE},
        {KEY_INSERT,     SDL_SCANCODE_INSERT},

        {KEY_HOME,       SDL_SCANCODE_HOME},
        {KEY_PAGEUP,     SDL_SCANCODE_PAGEUP},
        {KEY_DELETE,     SDL_SCANCODE_DELETE},
        {KEY_END,        SDL_SCANCODE_END},
        {KEY_PAGEDOWN,   SDL_SCANCODE_PAGEDOWN},
        {KEY_RIGHT,      SDL_SCANCODE_RIGHT},
        {KEY_LEFT,       SDL_SCANCODE_LEFT},
        {KEY_DOWN,       SDL_SCANCODE_DOWN},
        {KEY_UP,         SDL_SCANCODE_UP},

        {KEY_NUMLOCK,    SDL_SCANCODE_NUMLOCKCLEAR},
        {KEY_KPSLASH,    SDL_SCANCODE_KP_DIVIDE},
        {KEY_KPASTERISK, SDL_SCANCODE_KP_MULTIPLY},
        {KEY_KPMINUS,    SDL_SCANCODE_KP_MINUS},
        {KEY_KPPLUS,     SDL_SCANCODE_KP_PLUS},
        {KEY_KPENTER,    SDL_SCANCODE_KP_ENTER},
        {KEY_KP1,        SDL_SCANCODE_KP_1},
        {KEY_KP2,        SDL_SCANCODE_KP_2},
        {KEY_KP3,        SDL_SCANCODE_KP_3},
        {KEY_KP4,        SDL_SCANCODE_KP_4},
        {KEY_KP5,        SDL_SCANCODE_KP_5},
        {KEY_KP6,        SDL_SCANCODE_KP_6},
        {KEY_KP7,        SDL_SCANCODE_KP_7},
        {KEY_KP8,        SDL_SCANCODE_KP_8},
        {KEY_KP9,        SDL_SCANCODE_KP_9},
        {KEY_KP0,        SDL_SCANCODE_KP_0},

        {KEY_APOSTROPHE, SDL_SCANCODE_NONUSBACKSLASH}, // not sure about thise one
        {KEY_LEFTCTRL,   SDL_SCANCODE_LCTRL},
        {KEY_LEFTSHIFT,  SDL_SCANCODE_LSHIFT},
        {KEY_LEFTALT,    SDL_SCANCODE_LALT},
        {KEY_LEFTMETA,   SDL_SCANCODE_LGUI},
        {KEY_RIGHTCTRL,  SDL_SCANCODE_RCTRL},
        {KEY_RIGHTSHIFT, SDL_SCANCODE_RSHIFT},
        {KEY_RIGHTALT,   SDL_SCANCODE_RALT},
        {KEY_RIGHTMETA,  SDL_SCANCODE_RGUI},
};

#endif
static std::map<const uint16_t, const uint16_t> sdl_to_allegro_keycodes = {
        {SDL_SCANCODE_A,              ALLEGRO_KEY_A},
        {SDL_SCANCODE_B,              ALLEGRO_KEY_B},
        {SDL_SCANCODE_C,              ALLEGRO_KEY_C},
        {SDL_SCANCODE_D,              ALLEGRO_KEY_D},
        {SDL_SCANCODE_E,              ALLEGRO_KEY_E},
        {SDL_SCANCODE_F,              ALLEGRO_KEY_F},
        {SDL_SCANCODE_G,              ALLEGRO_KEY_G},
        {SDL_SCANCODE_H,              ALLEGRO_KEY_H},
        {SDL_SCANCODE_I,              ALLEGRO_KEY_I},
        {SDL_SCANCODE_J,              ALLEGRO_KEY_J},
        {SDL_SCANCODE_K,              ALLEGRO_KEY_K},
        {SDL_SCANCODE_L,              ALLEGRO_KEY_L},
        {SDL_SCANCODE_M,              ALLEGRO_KEY_M},
        {SDL_SCANCODE_N,              ALLEGRO_KEY_N},
        {SDL_SCANCODE_O,              ALLEGRO_KEY_O},
        {SDL_SCANCODE_P,              ALLEGRO_KEY_P},
        {SDL_SCANCODE_Q,              ALLEGRO_KEY_Q},
        {SDL_SCANCODE_R,              ALLEGRO_KEY_R},
        {SDL_SCANCODE_S,              ALLEGRO_KEY_S},
        {SDL_SCANCODE_T,              ALLEGRO_KEY_T},
        {SDL_SCANCODE_U,              ALLEGRO_KEY_U},
        {SDL_SCANCODE_V,              ALLEGRO_KEY_V},
        {SDL_SCANCODE_W,              ALLEGRO_KEY_W},
        {SDL_SCANCODE_X,              ALLEGRO_KEY_X},
        {SDL_SCANCODE_Y,              ALLEGRO_KEY_Y},
        {SDL_SCANCODE_Z,              ALLEGRO_KEY_Z},

        {SDL_SCANCODE_1,              ALLEGRO_KEY_1},
        {SDL_SCANCODE_2,              ALLEGRO_KEY_2},
        {SDL_SCANCODE_3,              ALLEGRO_KEY_3},
        {SDL_SCANCODE_4,              ALLEGRO_KEY_4},
        {SDL_SCANCODE_5,              ALLEGRO_KEY_5},
        {SDL_SCANCODE_6,              ALLEGRO_KEY_6},
        {SDL_SCANCODE_7,              ALLEGRO_KEY_7},
        {SDL_SCANCODE_8,              ALLEGRO_KEY_8},
        {SDL_SCANCODE_9,              ALLEGRO_KEY_9},
        {SDL_SCANCODE_0,              ALLEGRO_KEY_0},

        {SDL_SCANCODE_RETURN,         ALLEGRO_KEY_ENTER},
        {SDL_SCANCODE_ESCAPE,         ALLEGRO_KEY_ESCAPE},
        {SDL_SCANCODE_BACKSPACE,      ALLEGRO_KEY_BACKSPACE},
        {SDL_SCANCODE_TAB,            ALLEGRO_KEY_TAB},
        {SDL_SCANCODE_SPACE,          ALLEGRO_KEY_SPACE},

        {SDL_SCANCODE_MINUS,          ALLEGRO_KEY_MINUS},
        {SDL_SCANCODE_EQUALS,         ALLEGRO_KEY_EQUALS},
        {SDL_SCANCODE_LEFTBRACKET,    ALLEGRO_KEY_OPENBRACE},
        {SDL_SCANCODE_RIGHTBRACKET,   ALLEGRO_KEY_CLOSEBRACE},
        {SDL_SCANCODE_BACKSLASH,      ALLEGRO_KEY_BACKSLASH},
        {SDL_SCANCODE_NONUSHASH,      ALLEGRO_KEY_BACKSLASH2},

        {SDL_SCANCODE_SEMICOLON,      ALLEGRO_KEY_SEMICOLON},
        {SDL_SCANCODE_APOSTROPHE,     ALLEGRO_KEY_QUOTE},
        {SDL_SCANCODE_GRAVE,          ALLEGRO_KEY_TILDE},

        {SDL_SCANCODE_COMMA,          ALLEGRO_KEY_COMMA},
        {SDL_SCANCODE_PERIOD,         ALLEGRO_KEY_FULLSTOP},
        {SDL_SCANCODE_SLASH,          ALLEGRO_KEY_SLASH},

        {SDL_SCANCODE_CAPSLOCK,       ALLEGRO_KEY_CAPSLOCK},

        {SDL_SCANCODE_F1,             ALLEGRO_KEY_F1},
        {SDL_SCANCODE_F2,             ALLEGRO_KEY_F2},
        {SDL_SCANCODE_F3,             ALLEGRO_KEY_F3},
        {SDL_SCANCODE_F4,             ALLEGRO_KEY_F4},
        {SDL_SCANCODE_F5,             ALLEGRO_KEY_F5},
        {SDL_SCANCODE_F6,             ALLEGRO_KEY_F6},
        {SDL_SCANCODE_F7,             ALLEGRO_KEY_F7},
        {SDL_SCANCODE_F8,             ALLEGRO_KEY_F8},
        {SDL_SCANCODE_F9,             ALLEGRO_KEY_F9},
        {SDL_SCANCODE_F10,            ALLEGRO_KEY_F10},
        {SDL_SCANCODE_F11,            ALLEGRO_KEY_F11},
        {SDL_SCANCODE_F12,            ALLEGRO_KEY_F12},

        {SDL_SCANCODE_PRINTSCREEN,    ALLEGRO_KEY_PRINTSCREEN},
        {SDL_SCANCODE_SCROLLLOCK,     ALLEGRO_KEY_SCROLLLOCK},
        {SDL_SCANCODE_PAUSE,          ALLEGRO_KEY_PAUSE},
        {SDL_SCANCODE_INSERT,         ALLEGRO_KEY_INSERT},

        {SDL_SCANCODE_HOME,           ALLEGRO_KEY_HOME},
        {SDL_SCANCODE_PAGEUP,         ALLEGRO_KEY_PGUP},
        {SDL_SCANCODE_DELETE,         ALLEGRO_KEY_DELETE},
        {SDL_SCANCODE_END,            ALLEGRO_KEY_END},
        {SDL_SCANCODE_PAGEDOWN,       ALLEGRO_KEY_PGDN},
        {SDL_SCANCODE_RIGHT,          ALLEGRO_KEY_RIGHT},
        {SDL_SCANCODE_LEFT,           ALLEGRO_KEY_LEFT},
        {SDL_SCANCODE_DOWN,           ALLEGRO_KEY_DOWN},
        {SDL_SCANCODE_UP,             ALLEGRO_KEY_UP},

        {SDL_SCANCODE_NUMLOCKCLEAR,   ALLEGRO_KEY_NUMLOCK,},
        {SDL_SCANCODE_KP_DIVIDE,      ALLEGRO_KEY_PAD_SLASH},
        {SDL_SCANCODE_KP_MULTIPLY,    ALLEGRO_KEY_PAD_ASTERISK},
        {SDL_SCANCODE_KP_MINUS,       ALLEGRO_KEY_PAD_MINUS},
        {SDL_SCANCODE_KP_PLUS,        ALLEGRO_KEY_PAD_PLUS},
        {SDL_SCANCODE_KP_ENTER,       ALLEGRO_KEY_PAD_ENTER},
        {SDL_SCANCODE_KP_1,           ALLEGRO_KEY_PAD_0},
        {SDL_SCANCODE_KP_2,           ALLEGRO_KEY_PAD_1},
        {SDL_SCANCODE_KP_3,           ALLEGRO_KEY_PAD_2},
        {SDL_SCANCODE_KP_4,           ALLEGRO_KEY_PAD_3},
        {SDL_SCANCODE_KP_5,           ALLEGRO_KEY_PAD_4},
        {SDL_SCANCODE_KP_6,           ALLEGRO_KEY_PAD_5},
        {SDL_SCANCODE_KP_7,           ALLEGRO_KEY_PAD_6},
        {SDL_SCANCODE_KP_8,           ALLEGRO_KEY_PAD_7},
        {SDL_SCANCODE_KP_9,           ALLEGRO_KEY_PAD_8},
        {SDL_SCANCODE_KP_0,           ALLEGRO_KEY_PAD_9},

        {SDL_SCANCODE_NONUSBACKSLASH, ALLEGRO_KEY_TILDE /*ALLEGRO_KEY_GRAVE?*/},
        {SDL_SCANCODE_LCTRL,          ALLEGRO_KEY_LCTRL},
        {SDL_SCANCODE_LSHIFT,         ALLEGRO_KEY_LSHIFT},
        {SDL_SCANCODE_LALT,           ALLEGRO_KEY_ALT /**< alt, option */},
        {SDL_SCANCODE_LGUI,           ALLEGRO_KEY_LWIN, /**< windows, command (apple), meta */},
        {SDL_SCANCODE_RCTRL,          ALLEGRO_KEY_RCTRL},
        {SDL_SCANCODE_RSHIFT,         ALLEGRO_KEY_RSHIFT},
        {SDL_SCANCODE_RALT,           ALLEGRO_KEY_ALTGR},
        {SDL_SCANCODE_RGUI,           ALLEGRO_KEY_RWIN},
};

#endif

#endif //B_EM_KEYCODE_TO_ALLEGRO_H
