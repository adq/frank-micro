/* B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 */
#include <stdio.h>
#include <stdlib.h>
#include "pico/types.h"

#include <allegro5/allegro.h>
#include <video_render.h>
#include <logging.h>
#include "pico.h"

#ifdef NO_USE_DEBUGGER
#define static_without_debugger
#else
#define static_without_debugger static
#endif

enum vid_disptype vid_dtype_user;
bool vid_pal;

void gui_allegro_set_eject_text(int drive, ALLEGRO_PATH *path) {
    if (path) {
//        printf( "Eject drive %s: %s\n", drive ? "1/3" : "0/2", al_get_path_filename(path));
    }
}

void cataddname(char *s) {
    assert(false);
}

void gui_tapecat_close(void) {
    assert(false);
}

void gui_set_disc_wprot(int drive, bool enabled) {
    // just showing the state
}

int keydef_lookup_name(const char *name) {
    panic_unsupported();
}

void video_toggle_fullscreen(void) {
}

void video_update_window_size(ALLEGRO_EVENT *event) {
}
