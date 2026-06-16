/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_ui.h — settings overlay + disc browser UI.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * F12 → Settings screen, F11 → Media browser (Drive A / Drive B).
 */
#ifndef FRANK_UI_H
#define FRANK_UI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual key codes routed to the UI (set by frank_keyboard). */
#define FRANK_KS_Escape    0xFF1B
#define FRANK_KS_Return    0xFF0D
#define FRANK_KS_Up        0xFF52
#define FRANK_KS_Down      0xFF54
#define FRANK_KS_Left      0xFF51
#define FRANK_KS_Right     0xFF53
#define FRANK_KS_Page_Up   0xFF55
#define FRANK_KS_Page_Down 0xFF56
#define FRANK_KS_BackSpace 0xFF08
#define FRANK_KS_Delete    0xFFFF

/* Call once during platform init (installs the UI palette). */
void frank_ui_init(void);

/* True while the overlay or a toast is visible. */
bool frank_ui_is_visible(void);

/* True when UI panels are open and should capture keyboard input. */
bool frank_ui_wants_keys(void);

/* Toggle the settings overlay (mapped to F12). */
void frank_ui_toggle(void);

/* Open the media (drive) selection menu (mapped to F11). */
void frank_ui_open_disk_menu(void);

/* Non-blocking key event. Returns true if consumed by the UI. */
bool frank_ui_handle_key(unsigned int ks);

/* Render the overlay onto the 8-bpp framebuffer.
 * stride = row width in bytes (320), height = number of rows (256). */
void frank_ui_render(uint8_t *fb, int stride, int height);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_UI_H */
