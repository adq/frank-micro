/*
 * frank-micro — BBC Micro for RP2350
 * micro_loader.h — SD card disk image browser and mount helpers.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef MICRO_LOADER_H
#define MICRO_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MICRO_DISK_MAX_ENTRIES  200
#define MICRO_DISK_FILENAME_LEN  64
#define MICRO_DISK_PATH_LEN     160

typedef struct {
    char name[MICRO_DISK_FILENAME_LEN];
    bool is_dir;
} micro_disk_entry_t;

extern micro_disk_entry_t g_micro_disk_entries[MICRO_DISK_MAX_ENTRIES];
extern int                g_micro_disk_entry_count;
extern char               g_micro_disk_dir[MICRO_DISK_PATH_LEN];

/* Scan g_micro_disk_dir for BBC disk/tape images. */
int micro_disk_rescan(void);

/* Directory navigation. */
int micro_disk_enter_subdir(const char* name);
int micro_disk_enter_parent(void);

/* Build full path for entry idx. */
void micro_disk_entry_path(int idx, char* buf, size_t sz);

/* Mount a disk image (.ssd/.dsd/.adl/.hfe) into drive 0 or 1. */
int micro_mount_disk(int drive, const char* path);
void micro_eject_disk(int drive);
const char* micro_mounted_disk_name(int drive);

/* Auto-mount drive_a.ssd / drive_b.ssd on startup. */
void micro_disk_autoload(void);

/* File type helpers. */
int micro_is_disk_file(const char* name);
int micro_is_tape_file(const char* name);

#endif /* MICRO_LOADER_H */
