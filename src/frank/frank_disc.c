/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_disc.c — stream disc images (.ssd/.dsd/.adf/.adl/.img) from the SD card
 *                and mount them in b-em via the sector_read interface, with
 *                SHIFT-BREAK autoboot.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A whole image is far too big to hold in RAM alongside the BBC's RAM/ROM, so
 * we implement a sector_read provider that reads one sector at a time directly
 * from the open file on the SD card.  Two drives are supported (0 and 1), each
 * with its own open file handle and single-sector buffer.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ff.h"
#include "b-em.h"
#include "sdf.h"
#include "sector_read.h"
#include "disc.h"

#include "frank_disc.h"

#ifndef FRANK_DEFAULT_DISC
#define FRANK_DEFAULT_DISC "/micro/disk/PrinceOfPersia.ssd"
#endif

/* ── FatFs-backed sector reader (one per drive) ───────────────────────────*/
struct fatfs_drive {
    struct sector_read sr;
    FIL                fil;
    uint32_t           file_sectors;             /* sectors present in file   */
    struct sector_buffer secbuf;                 /* single in-flight buffer   */
    uint8_t            secdata[1024] __attribute__((aligned(4)));
    const struct sdf_geometry *geo;
    bool               have_image;
    char               path[160];
};

static struct fatfs_drive s_drv[2];

static struct sector_buffer *fatfs_acquire(struct sector_read *sr, uint32_t sector) {
    struct fatfs_drive *d = (struct fatfs_drive *)sr;
    if (sector >= sr->sector_count) return NULL;
    d->secbuf.in_use = 1;
    d->secbuf.buffer.size  = sr->sector_size;
    d->secbuf.buffer.bytes = d->secdata;
    if (sector < d->file_sectors) {
        FRESULT rc = f_lseek(&d->fil, (FSIZE_t)sector * sr->sector_size);
        UINT br = 0;
        if (rc != FR_OK || f_read(&d->fil, d->secdata, sr->sector_size, &br) != FR_OK || br != sr->sector_size)
            memset(d->secdata, 0, sr->sector_size);
    } else {
        /* Beyond the stored part of the image — return a blank sector. */
        memset(d->secdata, 0, sr->sector_size);
    }
    return &d->secbuf;
}

static void fatfs_release(struct sector_read *sr, struct sector_buffer *buffer) {
    (void)sr; buffer->in_use = 0;
}

static uint fatfs_check_available(struct sector_read *sr, struct sector_buffer *buffer,
                                  uint wanted, uint32_t timeout) {
    (void)sr; (void)wanted; (void)timeout;
    return buffer->buffer.size;
}

static void fatfs_close(struct sector_read *sr) {
    struct fatfs_drive *d = (struct fatfs_drive *)sr;
    if (d->have_image) {
        f_close(&d->fil);
        d->have_image = false;
    }
}

static const struct sector_read_funcs fatfs_funcs = {
    .acquire_buffer  = fatfs_acquire,
    .release_buffer  = fatfs_release,
    .check_available = fatfs_check_available,
    .close           = fatfs_close,
#ifndef NDEBUG
    .type = memory,
#endif
};

/* Pick a DFS/ADFS geometry from the image size + extension (mirrors b-em
 * find_geo_size).  .dsd images are double-sided interleaved DFS. */
static const struct sdf_geometry *geo_from_file(const char *path, uint32_t size) {
    const char *dot = strrchr(path, '.');
    char ext[5] = { 0 };
    if (dot && strlen(dot) <= 4) {
        for (int i = 1; dot[i] && i < 4; ++i) {
            char c = dot[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            ext[i - 1] = c;
        }
    }

    bool is_dsd = (ext[0] == 'd' && ext[1] == 's' && ext[2] == 'd');
    bool is_adfs = (ext[0] == 'a' && ext[1] == 'd');   /* .adf / .adl / .adm */

    if (is_adfs) {
        if (size <= 640u * 1024) return sdf_geo_tab + SDF_FMT_ADFS_L;
        return sdf_geo_tab + SDF_FMT_ADFS_D;
    }

    if (is_dsd) {
        /* Double-sided interleaved DFS (80 track, 10 sector). */
        return sdf_geo_tab + SDF_FMT_DFS_10S_INT_80T;
    }

    /* .ssd / .img / unknown: single-sided DFS by size. */
    switch (size) {
        case 100u * 1024: return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_40T;
        case 200u * 1024: return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
        case 400u * 1024: return sdf_geo_tab + SDF_FMT_DFS_10S_INT_80T;
        case 640u * 1024: return sdf_geo_tab + SDF_FMT_ADFS_L;
        case 800u * 1024: return sdf_geo_tab + SDF_FMT_ADFS_D;
        default: break;
    }
    if (size <= 200u * 1024) return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
    return NULL;
}

/* ── mount / eject ────────────────────────────────────────────────────────*/

int frank_disc_mount(int drive, const char *path) {
    if (drive < 0 || drive > 1) return -1;
    struct fatfs_drive *d = &s_drv[drive];

    /* Close any previously-mounted image on this drive first. */
    frank_disc_eject(drive);

    FRESULT fr = f_open(&d->fil, path, FA_READ);
    if (fr != FR_OK) {
        printf("DISC: open '%s' failed (%d)\n", path, (int)fr);
        return -1;
    }

    uint32_t fsize = (uint32_t)f_size(&d->fil);
    const struct sdf_geometry *geo = geo_from_file(path, fsize);
    if (!geo) {
        printf("DISC: bad geometry, size=%lu\n", (unsigned long)fsize);
        f_close(&d->fil);
        return -1;
    }

    uint32_t full = (uint32_t)geo->tracks * geo->sectors_per_track * geo->sector_size;
    if (geo->sides != SDF_SIDES_SINGLE) full *= 2;

    d->sr.funcs        = &fatfs_funcs;
    d->sr.sector_size  = geo->sector_size;
    d->sr.sector_count = full / geo->sector_size;
    d->file_sectors    = (fsize + geo->sector_size - 1) / geo->sector_size;
    d->geo             = geo;
    d->have_image      = true;
    snprintf(d->path, sizeof(d->path), "%s", path);

    disc_close(drive);
    sdf_load_image(drive, geo, &d->sr);
    printf("DISC: drive %d = '%s' (%s, %lu bytes, %lu sectors)\n",
           drive, path, geo->name, (unsigned long)fsize,
           (unsigned long)d->sr.sector_count);
    return 0;
}

void frank_disc_eject(int drive) {
    if (drive < 0 || drive > 1) return;
    struct fatfs_drive *d = &s_drv[drive];
    if (!d->have_image) return;
    disc_close(drive);
    f_close(&d->fil);
    d->have_image = false;
    d->path[0]    = '\0';
    printf("DISC: drive %d ejected\n", drive);
}

const char *frank_disc_name(int drive) {
    if (drive < 0 || drive > 1 || !s_drv[drive].have_image || !s_drv[drive].path[0])
        return NULL;
    const char *slash = strrchr(s_drv[drive].path, '/');
    return slash ? slash + 1 : s_drv[drive].path;
}

/* ── autoboot state machine (SHIFT-BREAK on drive 0) ──────────────────────*/
static int s_boot_frame = 0;
static int s_phase = 3;            /* 3 = idle/done */
static int s_shift_release_frame = 0;

/* Request a SHIFT-BREAK autoboot of whatever is mounted on drive 0. */
void frank_disc_request_boot(void) {
    if (!s_drv[0].have_image) return;
    s_boot_frame = 0;
    s_shift_release_frame = 0;
    s_phase = 0;                    /* settle ~25 frames, then SHIFT-BREAK */
}

/* Called by b-em's main() once initialisation is complete. */
void frank_post_init(void) { /* disc mounting happens in the run loop */ }

/* Open the autoboot disc image at boot (SD mounted, no emulation yet). */
void frank_disc_preload(void) {
    const char *path = FRANK_DEFAULT_DISC;
    FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) {
        printf("DISC: default '%s' not present (OK)\n", path);
        return;
    }
    if (frank_disc_mount(0, path) == 0) {
        /* Arm the SHIFT-BREAK autoboot once emulation is running. */
        s_boot_frame = 0;
        s_phase = 0;
    }
}

void frank_disc_autoboot_tick(void) {
    extern void key_down(int code);
    extern void key_up(int code);
    extern void m6502_reset(void);

    if (s_phase >= 3) return;
    s_boot_frame++;

    if (s_phase <= 1 && s_boot_frame >= (s_phase == 0 ? 25 : 1)) {
        key_down(215 /* LSHIFT */);
        m6502_reset();
        s_shift_release_frame = s_boot_frame + 75;
        printf("DISC: SHIFT-BREAK\n");
        s_phase = 2;
    } else if (s_phase == 2 && s_boot_frame >= s_shift_release_frame) {
        key_up(215 /* LSHIFT */);
        printf("DISC: autoboot done\n");
        s_phase = 3;
    }
}
