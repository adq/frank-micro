/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_screenshot.c — save the BBC framebuffer as an 8-bit BMP to SD.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Mirrors frank-cpc's cpc_screenshot_save(): a deferred flag is raised by a
 * trigger (PrintScreen key or the F12 menu) and serviced once per frame from
 * frank_gui.c, before the UI overlay is composited, so the saved image is a
 * clean 320x256 BBC frame.  Files are written to /micro/screenshot/ with an
 * auto-incrementing BBC_NNNN.BMP name.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ff.h"               /* FatFs */
#include "HDMI.h"             /* graphics_get_palette() */
#include "board_config.h"     /* MICRO_FB_WIDTH / MICRO_FB_HEIGHT */
#include "frank_screenshot.h"

volatile bool g_frank_screenshot_pending = false;

/* Persistent screenshot counter — set once on first call by scanning the
 * screenshot directory, then incremented on each subsequent call. */
static int screenshot_counter = -1;

int frank_screenshot_save(const uint8_t *fb) {
    if (!fb) return -1;

    /* On first call, scan the directory to find the highest existing number. */
    if (screenshot_counter < 0) {
        screenshot_counter = 0;
        DIR dir;
        FILINFO fi;
        if (f_opendir(&dir, "/micro/screenshot") == FR_OK) {
            while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
                if (fi.fname[0] == 'B' && fi.fname[1] == 'B' &&
                    fi.fname[2] == 'C' && fi.fname[3] == '_') {
                    int n = 0;
                    for (int j = 4; j < 8 && fi.fname[j] >= '0' && fi.fname[j] <= '9'; j++)
                        n = n * 10 + (fi.fname[j] - '0');
                    if (n > screenshot_counter) screenshot_counter = n;
                }
            }
            f_closedir(&dir);
        }
    }

    screenshot_counter++;
    if (screenshot_counter > 9999) screenshot_counter = 1;

    char path[40];
    snprintf(path, sizeof(path), "/micro/screenshot/BBC_%04d.BMP", screenshot_counter);

    f_mkdir("/micro/screenshot");

    const int W = MICRO_FB_WIDTH;   /* 320 */
    const int H = MICRO_FB_HEIGHT;  /* 256 */
    const int row_bytes = (W + 3) & ~3;
    const uint32_t palette_size    = 256 * 4;
    const uint32_t pixel_offset    = 14 + 40 + palette_size;
    const uint32_t pixel_data_size = (uint32_t)row_bytes * H;
    const uint32_t file_size       = pixel_offset + pixel_data_size;

    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return -1;

    UINT bw;

    /* Combined BMP file header (14) + DIB header (40) = 54 bytes. */
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (uint8_t)(file_size);         hdr[3]  = (uint8_t)(file_size >> 8);
    hdr[4]  = (uint8_t)(file_size >> 16);   hdr[5]  = (uint8_t)(file_size >> 24);
    hdr[10] = (uint8_t)(pixel_offset);      hdr[11] = (uint8_t)(pixel_offset >> 8);
    hdr[12] = (uint8_t)(pixel_offset >> 16); hdr[13] = (uint8_t)(pixel_offset >> 24);
    hdr[14] = 40;
    hdr[18] = (uint8_t)(W);       hdr[19] = (uint8_t)(W >> 8);
    hdr[22] = (uint8_t)(H);       hdr[23] = (uint8_t)(H >> 8);
    hdr[26] = 1;  /* planes */
    hdr[28] = 8;  /* bpp */
    f_write(&f, hdr, 54, &bw);

    /* Palette: read the authoritative RGB888 from the display driver via
     * graphics_get_palette().  Write 32 entries at a time (128-byte chunks). */
    uint8_t pal4[128];
    for (int chunk = 0; chunk < 8; chunk++) {
        memset(pal4, 0, sizeof(pal4));
        int base = chunk * 32;
        for (int i = 0; i < 32; i++) {
            int idx = base + i;
            uint32_t rgb = graphics_get_palette((uint8_t)idx);
            pal4[i * 4 + 0] = (uint8_t)(rgb & 0xFF);         /* B */
            pal4[i * 4 + 1] = (uint8_t)((rgb >> 8) & 0xFF);  /* G */
            pal4[i * 4 + 2] = (uint8_t)((rgb >> 16) & 0xFF); /* R */
        }
        f_write(&f, pal4, 128, &bw);
    }

    /* Pixel data — bottom-to-top row order. */
    uint8_t row_pad[4] = {0, 0, 0, 0};
    int pad = row_bytes - W;
    for (int y = H - 1; y >= 0; y--) {
        const uint8_t *src = fb + (size_t)y * W;
        f_write(&f, src, (UINT)W, &bw);
        if (pad > 0)
            f_write(&f, row_pad, (UINT)pad, &bw);
    }

    f_close(&f);
    printf("frank-micro: screenshot saved to %s\n", path);
    return 0;
}
