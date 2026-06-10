/*
 * frank-micro — BBC Micro for RP2350
 * micro_loader.c — Disk/tape image browser and mount.
 */
#include "micro_loader.h"
#include "bbc.h"
#include "disc.h"
#include "disc_drive.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* External BBC struct — set by platform.c at init. */
extern struct bbc_struct* g_p_bbc;

micro_disk_entry_t g_micro_disk_entries[MICRO_DISK_MAX_ENTRIES];
int                g_micro_disk_entry_count = 0;
char               g_micro_disk_dir[MICRO_DISK_PATH_LEN] = "/micro/disk";

static char g_mounted_disk_name[2][MICRO_DISK_FILENAME_LEN] = { "", "" };

/* ---- helpers ---------------------------------------------------------------- */

static int ext_matches(const char* name, const char* ext) {
    size_t nl = strlen(name);
    size_t el = strlen(ext);
    if (nl < el + 1) return 0;
    const char* p = name + nl - el;
    while (*p && *ext) {
        if (tolower((unsigned char)*p) != tolower((unsigned char)*ext)) return 0;
        p++; ext++;
    }
    return 1;
}

int micro_is_disk_file(const char* name) {
    return ext_matches(name, ".ssd") || ext_matches(name, ".dsd") ||
           ext_matches(name, ".adl") || ext_matches(name, ".hfe");
}

int micro_is_tape_file(const char* name) {
    return ext_matches(name, ".uef") || ext_matches(name, ".csw");
}

/* ---- disk scan ---------------------------------------------------------------- */

int micro_disk_rescan(void) {
    DIR dir;
    FILINFO fi;
    g_micro_disk_entry_count = 0;

    FRESULT fr = f_opendir(&dir, g_micro_disk_dir);
    if (fr != FR_OK) {
        snprintf(g_micro_disk_dir, sizeof(g_micro_disk_dir), "/micro/disk");
        fr = f_opendir(&dir, g_micro_disk_dir);
        if (fr != FR_OK) return 0;
    }

    while (g_micro_disk_entry_count < MICRO_DISK_MAX_ENTRIES) {
        fr = f_readdir(&dir, &fi);
        if (fr != FR_OK || fi.fname[0] == 0) break;
        if (fi.fname[0] == '.') continue;

        bool is_dir = (fi.fattrib & AM_DIR) != 0;
        if (!is_dir && !micro_is_disk_file(fi.fname) && !micro_is_tape_file(fi.fname)) continue;

        micro_disk_entry_t* e = &g_micro_disk_entries[g_micro_disk_entry_count++];
        strncpy(e->name, fi.fname, MICRO_DISK_FILENAME_LEN - 1);
        e->name[MICRO_DISK_FILENAME_LEN - 1] = '\0';
        e->is_dir = is_dir;
    }
    f_closedir(&dir);
    printf("micro_loader: %d entries in %s\n", g_micro_disk_entry_count, g_micro_disk_dir);
    return g_micro_disk_entry_count;
}

int micro_disk_enter_subdir(const char* name) {
    size_t cur = strlen(g_micro_disk_dir);
    size_t add = strlen(name);
    if (cur + 1 + add + 1 >= sizeof(g_micro_disk_dir)) return -1;
    if (strcmp(g_micro_disk_dir, "/") != 0) {
        g_micro_disk_dir[cur] = '/';
        memcpy(g_micro_disk_dir + cur + 1, name, add + 1);
    } else {
        memcpy(g_micro_disk_dir + 1, name, add + 1);
    }
    return micro_disk_rescan();
}

int micro_disk_enter_parent(void) {
    if (strcmp(g_micro_disk_dir, "/") == 0) return g_micro_disk_entry_count;
    char* slash = strrchr(g_micro_disk_dir, '/');
    if (!slash) return g_micro_disk_entry_count;
    if (slash == g_micro_disk_dir) g_micro_disk_dir[1] = 0;
    else *slash = '\0';
    if (strcmp(g_micro_disk_dir, "/") == 0)
        snprintf(g_micro_disk_dir, sizeof(g_micro_disk_dir), "/micro/disk");
    return micro_disk_rescan();
}

void micro_disk_entry_path(int idx, char* buf, size_t sz) {
    if (idx < 0 || idx >= g_micro_disk_entry_count) { buf[0] = '\0'; return; }
    snprintf(buf, sz, "%s/%s", g_micro_disk_dir, g_micro_disk_entries[idx].name);
}

/* ---- mount ------------------------------------------------------------------- */

int micro_mount_disk(int drive, const char* path) {
    if (!g_p_bbc) return -1;
    if (drive < 0 || drive > 1) return -1;

    bbc_add_disc(g_p_bbc, path, drive, 0, 1, 0, 0, 0);

    /* beebjit only loads disc surfaces during disc_drive_power_on_reset().
     * Since we mount discs after power-on (and at runtime via the serial
     * console), force the surface to load now, otherwise the FDC sees an
     * empty disc and *CAT / boots fail. */
    {
        struct disc_drive_struct* p_drive =
            (drive == 0) ? bbc_get_drive_0(g_p_bbc) : bbc_get_drive_1(g_p_bbc);
        if (p_drive) {
            struct disc_struct* p_disc = disc_drive_get_disc(p_drive);
            if (p_disc) disc_load(p_disc);
        }
    }

    /* Remember name */
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    strncpy(g_mounted_disk_name[drive], base, MICRO_DISK_FILENAME_LEN - 1);
    g_mounted_disk_name[drive][MICRO_DISK_FILENAME_LEN - 1] = '\0';

    printf("micro_loader: drive %d → %s\n", drive, path);
    return 0;
}

void micro_eject_disk(int drive) {
    (void)drive;
    /* beebjit doesn't have a direct eject API; clear name. */
    if (drive >= 0 && drive <= 1)
        g_mounted_disk_name[drive][0] = '\0';
}

const char* micro_mounted_disk_name(int drive) {
    if (drive < 0 || drive > 1) return NULL;
    return g_mounted_disk_name[drive][0] ? g_mounted_disk_name[drive] : NULL;
}

void micro_disk_autoload(void) {
    static const char* paths[2] = {
        "/micro/disk/drive_a.ssd",
        "/micro/disk/drive_b.ssd"
    };
    for (int i = 0; i < 2; i++) {
        FILINFO fi;
        if (f_stat(paths[i], &fi) == FR_OK)
            micro_mount_disk(i, paths[i]);
    }
}
