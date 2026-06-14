/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_disc.c — stream a disc image (.ssd) from the SD card and mount it in
 *                b-em via the sector_read interface, with SHIFT-BREAK autoboot.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The whole image is far too big to hold in RAM alongside the BBC's RAM/ROM
 * (a 200K DFS disc would exhaust the heap and OOM-panic b-em's mem_init), so
 * we implement a sector_read provider that reads one sector at a time directly
 * from the open file on the SD card.  Only a single sector (256 bytes) is
 * buffered.
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

/* ── FatFs-backed sector reader ───────────────────────────────────────────*/
struct fatfs_sector_read {
    struct sector_read sr;
    FIL    fil;
    uint32_t file_sectors;       /* sectors actually present in the file */
};

static struct fatfs_sector_read s_fsr;     /* single mounted image           */
static struct sector_buffer     s_secbuf;  /* single in-flight sector buffer */
static uint8_t                  s_secdata[1024] __attribute__((aligned(4)));
static bool                     s_have_image = false;

static struct sector_buffer *fatfs_acquire(struct sector_read *sr, uint32_t sector) {
    struct fatfs_sector_read *fr = (struct fatfs_sector_read *)sr;
    if (sector >= sr->sector_count) return NULL;
    s_secbuf.in_use = 1;
    s_secbuf.buffer.size  = sr->sector_size;
    s_secbuf.buffer.bytes = s_secdata;
    if (sector < fr->file_sectors) {
        FRESULT rc = f_lseek(&fr->fil, (FSIZE_t)sector * sr->sector_size);
        UINT br = 0;
        if (rc != FR_OK || f_read(&fr->fil, s_secdata, sr->sector_size, &br) != FR_OK || br != sr->sector_size)
            memset(s_secdata, 0, sr->sector_size);
    } else {
        /* Beyond the stored part of the image — return a blank sector. */
        memset(s_secdata, 0, sr->sector_size);
    }
    return &s_secbuf;
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
    struct fatfs_sector_read *fr = (struct fatfs_sector_read *)sr;
    f_close(&fr->fil);
    s_have_image = false;
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

/* Pick a DFS/ADFS geometry from the image size (mirrors b-em find_geo_size). */
static const struct sdf_geometry *geo_from_size(uint32_t size) {
    switch (size) {
        case 100u*1024: return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_40T;
        case 200u*1024: return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
        case 400u*1024: return sdf_geo_tab + SDF_FMT_DFS_10S_INT_80T;
        case 640u*1024: return sdf_geo_tab + SDF_FMT_ADFS_L;
        case 800u*1024: return sdf_geo_tab + SDF_FMT_ADFS_D;
        default: break;
    }
    if (size <= 200u*1024) return sdf_geo_tab + SDF_FMT_DFS_10S_SIN_80T;
    return NULL;
}

static const struct sdf_geometry *s_geo = NULL;

/* Open the disc image at boot (SD mounted, no emulation yet). */
void frank_disc_preload(void) {
    const char *path = FRANK_DEFAULT_DISC;
    printf("DISC: open '%s'\n", path);
    FRESULT fr = f_open(&s_fsr.fil, path, FA_READ);
    if (fr != FR_OK) { printf("DISC: open failed (%d)\n", (int)fr); return; }

    uint32_t fsize = (uint32_t)f_size(&s_fsr.fil);
    const struct sdf_geometry *geo = geo_from_size(fsize);
    if (!geo) { printf("DISC: bad geometry, size=%lu\n", (unsigned long)fsize); f_close(&s_fsr.fil); return; }

    uint32_t full = (uint32_t)geo->tracks * geo->sectors_per_track * geo->sector_size;
    if (geo->sides != SDF_SIDES_SINGLE) full *= 2;

    s_fsr.sr.funcs       = &fatfs_funcs;
    s_fsr.sr.sector_size = geo->sector_size;
    s_fsr.sr.sector_count = full / geo->sector_size;
    s_fsr.file_sectors   = (fsize + geo->sector_size - 1) / geo->sector_size;
    s_geo = geo;
    s_have_image = true;
    printf("DISC: ready '%s' (%s, %lu bytes, %lu sectors)\n",
           path, geo->name, (unsigned long)fsize,
           (unsigned long)s_fsr.sr.sector_count);
}

void frank_post_init(void) { /* mount happens in the run loop */ }

/* ── autoboot state machine (per frame, no SD setup work) ─────────────────*/
static int s_boot_frame = 0;
static int s_phase = 0;
static int s_shift_release_frame = 0;

void frank_disc_autoboot_tick(void) {
    extern void key_down(int code);
    extern void key_up(int code);
    extern void m6502_reset(void);

    if (s_phase >= 3 || !s_have_image) return;
    s_boot_frame++;

    if (s_phase == 0 && s_boot_frame >= 25) {
        disc_close(0);
        sdf_load_image(0, s_geo, &s_fsr.sr);
        printf("DISC: mounted on drive 0\n");
        s_phase = 1;
    } else if (s_phase == 1) {
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
