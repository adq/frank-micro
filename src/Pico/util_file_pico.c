/*
 * frank-micro — BBC Micro for RP2350
 * util_file_pico.c — FatFS-backed file I/O for beebjit's util_file_* API.
 *
 * beebjit's util.c implements util_file_* using POSIX FILE*.  On RP2350
 * with FatFS we need to redirect those calls through f_open / f_read etc.
 *
 * We compile this file and DO NOT compile the file-operations portion of
 * util.c (controlled by -DPICO_BUILD in util.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "util.h"
#include "ff.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* util_file is just a wrapper around FATFS FIL. */
struct util_file {
    FIL   fil;
    int   is_open;
};

/* Convert a relative "roms/os12.rom" path to an absolute SD card path.
 * All BBC ROM/disk paths are prefixed with /micro/. */
static void make_sdcard_path(char* dst, size_t dsz, const char* src) {
    /* Already absolute (starts with /) */
    if (src[0] == '/') {
        snprintf(dst, dsz, "%s", src);
        return;
    }
    snprintf(dst, dsz, "/micro/%s", src);
}

struct util_file* util_file_open(const char* p_file_name,
                                  int writeable,
                                  int create) {
    char path[256];
    make_sdcard_path(path, sizeof(path), p_file_name);

    struct util_file* p = calloc(1, sizeof(*p));
    if (!p) {
        util_bail("util_file_open: malloc failed");
        return NULL;
    }

    BYTE mode = writeable ? (FA_READ | FA_WRITE | (create ? FA_CREATE_ALWAYS : FA_OPEN_EXISTING))
                          : FA_READ;
    FRESULT fr = f_open(&p->fil, path, mode);
    if (fr != FR_OK) {
        free(p);
        util_bail("util_file_open: can't open %s (fr=%d)", path, (int)fr);
        return NULL;
    }
    p->is_open = 1;
    return p;
}

struct util_file* util_file_try_open(const char* p_file_name,
                                      int writeable,
                                      int create) {
    char path[256];
    make_sdcard_path(path, sizeof(path), p_file_name);

    struct util_file* p = calloc(1, sizeof(*p));
    if (!p) return NULL;

    BYTE mode = writeable ? (FA_READ | FA_WRITE | (create ? FA_CREATE_ALWAYS : FA_OPEN_EXISTING))
                          : FA_READ;
    FRESULT fr = f_open(&p->fil, path, mode);
    if (fr != FR_OK) {
        free(p);
        return NULL;
    }
    p->is_open = 1;
    return p;
}

struct util_file* util_file_try_read_open(const char* p_file_name) {
    return util_file_try_open(p_file_name, 0, 0);
}

void util_file_close(struct util_file* p) {
    if (p && p->is_open) {
        f_close(&p->fil);
        p->is_open = 0;
    }
    free(p);
}

uint64_t util_file_get_pos(struct util_file* p) {
    return (uint64_t)f_tell(&p->fil);
}

uint64_t util_file_get_size(struct util_file* p) {
    return (uint64_t)f_size(&p->fil);
}

void util_file_seek(struct util_file* p, uint64_t pos) {
    FRESULT fr = f_lseek(&p->fil, (FSIZE_t)pos);
    if (fr != FR_OK) util_bail("util_file_seek failed (fr=%d)", (int)fr);
}

uint64_t util_file_read(struct util_file* p, void* p_buf, uint64_t length) {
    UINT br = 0;
    f_read(&p->fil, p_buf, (UINT)length, &br);
    return (uint64_t)br;
}

void util_file_write(struct util_file* p,
                     const void* p_buf,
                     uint64_t length) {
    UINT bw = 0;
    FRESULT fr = f_write(&p->fil, p_buf, (UINT)length, &bw);
    if (fr != FR_OK || bw != (UINT)length)
        util_bail("util_file_write short write (fr=%d)", (int)fr);
}

void util_file_flush(struct util_file* p) {
    f_sync(&p->fil);
}

uint64_t util_file_read_fully(const char* p_file_name,
                               uint8_t* p_buf,
                               uint64_t max_length) {
    struct util_file* p_file = util_file_try_open(p_file_name, 0, 0);
    if (!p_file) return 0;

    uint64_t size = util_file_get_size(p_file);
    if (size > max_length) size = max_length;
    uint64_t read = util_file_read(p_file, p_buf, size);
    util_file_close(p_file);
    return read;
}

void util_file_write_fully(const char* p_file_name,
                            const uint8_t* p_buf,
                            uint64_t length) {
    struct util_file* p = util_file_open(p_file_name, 1, 1);
    util_file_write(p, p_buf, length);
    util_file_close(p);
}

void util_file_copy(const char* p_src, const char* p_dst) {
    struct util_file* s = util_file_try_open(p_src, 0, 0);
    if (!s) return;
    struct util_file* d = util_file_open(p_dst, 1, 1);
    uint64_t size = util_file_get_size(s);
    static uint8_t buf[512];
    uint64_t done = 0;
    while (done < size) {
        uint64_t chunk = size - done;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        uint64_t r = util_file_read(s, buf, chunk);
        if (r == 0) break;
        util_file_write(d, buf, r);
        done += r;
    }
    util_file_close(s);
    util_file_close(d);
}
