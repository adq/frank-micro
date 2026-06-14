/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_DISCS_H
#define B_EM_PICO_DISCS_H

#include <stdint.h>
#include "sdf.h"

typedef struct {
    const char *name;
    const struct sdf_geometry *geometry;
    const uint8_t *data;
    uint data_size;
} embedded_disc_t;

#ifndef NO_USE_CMD_LINE
extern embedded_disc_t cmd_line_disc;
#endif
extern const embedded_disc_t embedded_discs[];
extern const uint embedded_disc_count;
extern const uint embedded_disc_default;
#endif
