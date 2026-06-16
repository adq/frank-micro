/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_loader.c — SD card disc image scanner and navigation.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "frank_loader.h"
#include "frank_disc.h"
#include "frank_settings.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define DISK_ROOT "/micro/disk"

frank_disk_entry_t g_frank_disk_entries[FRANK_DISK_MAX_ENTRIES];
int                g_frank_disk_entry_count = 0;
char               g_frank_disk_dir[FRANK_DISK_PATH_LEN] = DISK_ROOT;

/* ── extension matching ───────────────────────────────────────────────── */

static int ext_is(const char *name, const char *ext3) {
    size_t n = strlen(name);
    if (n < 5) return 0;
    if (name[n - 4] != '.') return 0;
    char e1 = (char)tolower((unsigned char)name[n - 3]);
    char e2 = (char)tolower((unsigned char)name[n - 2]);
    char e3 = (char)tolower((unsigned char)name[n - 1]);
    return (e1 == ext3[0] && e2 == ext3[1] && e3 == ext3[2]);
}

int frank_is_disk_file(const char *name) {
    return ext_is(name, "ssd") || ext_is(name, "dsd") ||
           ext_is(name, "adf") || ext_is(name, "adl") ||
           ext_is(name, "img") || ext_is(name, "adm");
}

/* ── directory scan ───────────────────────────────────────────────────── */

static int cmp_entry(const void *pa, const void *pb) {
    const frank_disk_entry_t *a = (const frank_disk_entry_t *)pa;
    const frank_disk_entry_t *b = (const frank_disk_entry_t *)pb;
    if (a->is_dir && !b->is_dir) return -1;
    if (!a->is_dir && b->is_dir) return  1;
    return strcasecmp(a->name, b->name);
}

int frank_disk_rescan(void) {
    g_frank_disk_entry_count = 0;

    DIR dir;
    FILINFO fi;
    FRESULT fr = f_opendir(&dir, g_frank_disk_dir);
    if (fr != FR_OK) {
        snprintf(g_frank_disk_dir, sizeof(g_frank_disk_dir), DISK_ROOT);
        fr = f_opendir(&dir, g_frank_disk_dir);
        if (fr != FR_OK) return 0;
    }

    while (g_frank_disk_entry_count < FRANK_DISK_MAX_ENTRIES) {
        fr = f_readdir(&dir, &fi);
        if (fr != FR_OK || fi.fname[0] == 0) break;
        if (fi.fname[0] == '.') continue;

        bool is_dir = (fi.fattrib & AM_DIR) != 0;
        if (!is_dir && !frank_is_disk_file(fi.fname)) continue;

        size_t n = strlen(fi.fname);
        if (n >= FRANK_DISK_FILENAME_LEN) continue;

        frank_disk_entry_t *e = &g_frank_disk_entries[g_frank_disk_entry_count++];
        memcpy(e->name, fi.fname, n + 1);
        e->is_dir = is_dir;
    }
    f_closedir(&dir);

    qsort(g_frank_disk_entries, (size_t)g_frank_disk_entry_count,
          sizeof(g_frank_disk_entries[0]), cmp_entry);

    printf("frank_loader: %d entries in %s\n", g_frank_disk_entry_count, g_frank_disk_dir);
    return g_frank_disk_entry_count;
}

int frank_disk_enter_subdir(const char *name) {
    size_t cur = strlen(g_frank_disk_dir);
    size_t add = strlen(name);
    if (cur + 1 + add + 1 >= sizeof(g_frank_disk_dir)) return -1;
    if (strcmp(g_frank_disk_dir, "/") != 0) {
        g_frank_disk_dir[cur] = '/';
        memcpy(g_frank_disk_dir + cur + 1, name, add + 1);
    } else {
        memcpy(g_frank_disk_dir + 1, name, add + 1);
    }
    return frank_disk_rescan();
}

int frank_disk_enter_parent(void) {
    if (strcmp(g_frank_disk_dir, "/") == 0) return g_frank_disk_entry_count;
    char *slash = strrchr(g_frank_disk_dir, '/');
    if (!slash) return g_frank_disk_entry_count;
    if (slash == g_frank_disk_dir) g_frank_disk_dir[1] = 0;
    else                           *slash = 0;
    return frank_disk_rescan();
}

void frank_disk_entry_path(int idx, char *buf, size_t sz) {
    if (idx < 0 || idx >= g_frank_disk_entry_count) { if (sz) buf[0] = 0; return; }
    const char *name = g_frank_disk_entries[idx].name;
    if (strcmp(g_frank_disk_dir, "/") == 0)
        snprintf(buf, sz, "/%s", name);
    else
        snprintf(buf, sz, "%s/%s", g_frank_disk_dir, name);
}

/* ── autoload ─────────────────────────────────────────────────────────── */

void frank_disk_autoload(void) {
    FILINFO fi;

    /* Priority 1: explicit paths from settings. */
    if (g_frank_settings.disk_a[0])
        frank_disc_mount(0, g_frank_settings.disk_a);
    if (g_frank_settings.disk_b[0])
        frank_disc_mount(1, g_frank_settings.disk_b);

    /* Priority 2: well-known fixed names. */
    const char *paths[2] = { DISK_ROOT "/drivea.ssd", DISK_ROOT "/driveb.ssd" };
    for (int d = 0; d < 2; ++d) {
        if (frank_disc_name(d)) continue;
        if (f_stat(paths[d], &fi) == FR_OK)
            frank_disc_mount(d, paths[d]);
    }
}
