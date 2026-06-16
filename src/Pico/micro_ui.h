/*
 * frank-micro — BBC Micro for RP2350
 * micro_ui.h — OSD menu.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MICRO_UI_H
#define MICRO_UI_H

#include <stdint.h>

void micro_ui_init(void);
int  micro_ui_is_visible(void);
void micro_ui_show(void);
void micro_ui_hide(void);
void micro_ui_toggle(void);
/* Render UI overlay into fb (320×256). */
void micro_ui_render(uint8_t* fb, int stride, int height);
/* Process a keypress from PS/2. Returns 1 if consumed by UI. */
int  micro_ui_handle_key(int ps2_keycode, int pressed);

#endif /* MICRO_UI_H */
