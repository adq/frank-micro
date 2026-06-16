/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_disc.h
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_DISC_H
#define FRANK_DISC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Read the default disc image from the SD card into the drive-0 streamer
 * (call at boot, after the SD card is mounted, before starting the emulator).
 * Also arms the SHIFT-BREAK autoboot. */
void frank_disc_preload(void);

/* Per-frame autoboot driver (SHIFT-BREAK); call once per frame. */
void frank_disc_autoboot_tick(void);

/* Mount a disc image at `path` into `drive` (0 or 1).  Returns 0 on success. */
int frank_disc_mount(int drive, const char *path);

/* Eject the disc from `drive` (0 or 1). */
void frank_disc_eject(int drive);

/* Basename of the disc mounted in `drive`, or NULL if empty. */
const char *frank_disc_name(int drive);

/* Trigger a SHIFT-BREAK autoboot of whatever is mounted on drive 0. */
void frank_disc_request_boot(void);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_DISC_H */
