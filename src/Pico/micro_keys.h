/*
 * frank-micro — BBC Micro for RP2350
 * micro_keys.h — Programmatic key injection into the BBC keyboard matrix.
 *
 * Used by the USB-serial control framework to type text, press special
 * keys and boot disks (SHIFT+BREAK) without a physical keyboard.
 *
 * Keys are pushed into a small queue and clocked out one at a time by
 * micro_key_tick() (called once per 50 Hz frame from the keyboard poll).
 * Each key is held for a few frames so the BBC OS keyboard scan registers
 * it reliably, then released before the next key.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MICRO_KEYS_H
#define MICRO_KEYS_H

#include <stdint.h>

struct keyboard_struct;

/* Clock the key queue forward by one frame. Safe to call with NULL. */
void micro_key_tick(struct keyboard_struct* p_kbd);

/* Enqueue a single BBC key code (see keyboard.h), optionally with SHIFT. */
void micro_key_enqueue(uint8_t key, int shift, int hold_frames);

/* Enqueue a run of ASCII text mapped to the BBC keyboard layout.
 * Newline ('\n') is sent as RETURN. Returns number of chars queued. */
int micro_key_type(const char* text);

/* Press RETURN. */
void micro_key_return(void);

/* BREAK (soft reset). If with_shift, performs SHIFT+BREAK (boot disk). */
void micro_key_break(int with_shift);

/* True while keys remain queued or are being held. */
int micro_key_busy(void);

#endif /* MICRO_KEYS_H */
