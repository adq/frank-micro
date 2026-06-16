/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_loader.h — SD card disc image browser.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_LOADER_H
#define FRANK_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRANK_DISK_MAX_ENTRIES   200
#define FRANK_DISK_FILENAME_LEN   64
#define FRANK_DISK_PATH_LEN      160

typedef struct {
    char name[FRANK_DISK_FILENAME_LEN];
    bool is_dir;
} frank_disk_entry_t;

extern frank_disk_entry_t g_frank_disk_entries[FRANK_DISK_MAX_ENTRIES];
extern int                g_frank_disk_entry_count;
extern char               g_frank_disk_dir[FRANK_DISK_PATH_LEN];

/* Scan g_frank_disk_dir for BBC disc images and subdirs. Returns entry count. */
int frank_disk_rescan(void);

/* Enter a subdirectory by name; rescans. Returns new entry count or -1. */
int frank_disk_enter_subdir(const char *name);

/* Leave one directory level; rescans. */
int frank_disk_enter_parent(void);

/* Build the full absolute path for entry idx into buf. */
void frank_disk_entry_path(int idx, char *buf, size_t sz);

/* Returns true if name has a supported BBC disc extension. */
int frank_is_disk_file(const char *name);

/* Auto-mount discs from settings / well-known names on startup. */
void frank_disk_autoload(void);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_LOADER_H */
