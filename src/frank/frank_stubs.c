/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_stubs.c — definitions for b-em symbols whose host implementations
 *                 (linux.c / midi-linux.c) are not built on the Pico.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>
#include "b-em.h"

/* "Garbage" read/write target for unmapped memory accesses.  On device mem.c
 * points these at XIP_NOCACHE_NOALLOC_BASE; we just provide the storage. */
uint8_t *g_garbage_read;
uint8_t *g_garbage_write;

/* WAV loader: only used by the filesystem ddnoise/tapenoise path, which we do
 * not use (USE_MEM_DDNOISE provides in-memory samples).  Return NULL. */
ALLEGRO_SAMPLE *find_load_wav(ALLEGRO_PATH *dir, const char *name) {
    (void)dir; (void)name;
    return NULL;
}

/* MIDI is not supported on the Pico. */
void midi_init(void) {}
void midi_close(void) {}
