/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_disc.h
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_DISC_H
#define FRANK_DISC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the default disc image from the SD card into RAM (call at boot, after
 * the SD card is mounted, before starting the emulator). */
void frank_disc_preload(void);

/* Per-frame autoboot driver (mount + SHIFT-BREAK); call once per frame. */
void frank_disc_autoboot_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_DISC_H */
