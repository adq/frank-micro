/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_gui.h — shared declarations for the frank video/audio glue.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_GUI_H
#define FRANK_GUI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t *SCREEN[2];
extern volatile uint32_t current_buffer;

/* PS/2 keyboard poll: read pending key events and feed them to b-em. */
void frank_keyboard_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_GUI_H */
