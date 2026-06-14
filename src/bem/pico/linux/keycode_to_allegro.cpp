/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include "allegro5/allegro.h"
#ifndef X_GUI
#include <SDL_keycode.h>
#endif
#include "keycode_to_allegro.h"
#include "video/menu.h"
#include "tsqueue/tsqueue.h"

#if X_GUI
#include "x_gui.h"
#else
#include "pico/scanvideo.h"
#endif

static Tsqueue<ALLEGRO_KEYBOARD_EVENT> events(32);

static inline int translate_scancode(int scancode) {
#if X_GUI
    scancode = linux_to_sdl_keycodes[scancode - 8];
#endif
    return scancode;
}

void pico_key_down(int scancode, int keysym, int modifiers) {
    ALLEGRO_KEYBOARD_EVENT ev;
    ev.keycode = translate_scancode(scancode);
    ev.timestamp = al_get_time();
    ev.type = ALLEGRO_EVENT_KEY_DOWN;
    events.push(ev);
}

void pico_key_up(int scancode, int keysym, int modifiers) {
    ALLEGRO_KEYBOARD_EVENT ev;
    ev.keycode = translate_scancode(scancode);
    ev.timestamp = al_get_time();
    ev.type = ALLEGRO_EVENT_KEY_UP;
    events.push(ev);
}

void sdl_to_allegro_init() {
    platform_key_down = pico_key_down;
    platform_key_up = pico_key_up;
}

bool get_gui_key_event(ALLEGRO_EVENT *event_out) {
    if (!events.empty()) {
        event_out->keyboard = events.pop().value();
#if DISPLAY_MENU
        if (!handle_menu_key(event_out->keyboard.keycode, event_out->keyboard.type == ALLEGRO_EVENT_KEY_UP))
#endif
        {
            event_out->keyboard.keycode = sdl_to_allegro_keycodes[event_out->keyboard.keycode];
            return true;
        }
    }
    return false;
}

