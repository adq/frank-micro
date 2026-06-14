/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_ROMS_H
#define B_EM_PICO_ROMS_H

#include <stdint.h>
#include "sdf.h"

typedef struct {
    const char *name;
    const uint8_t *data;
    uint data_size;
} embedded_rom_t;

extern const embedded_rom_t embedded_roms[];
extern const uint embedded_rom_count;
#endif
