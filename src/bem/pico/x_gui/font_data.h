/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_FONT_DATA_H
#define B_EM_PICO_FONT_DATA_H

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct font_char {
    int codePoint, x, y, width, height, originX, originY, advance;
} font_char;

typedef struct font_definition {
    const char *name;
    int size, glyph_height, bold, italic, width, height, characterCount;
    font_char *characters;
    const uint8_t *alphas;
} font_definition;

extern font_definition font_Ubuntu;

#ifdef __cplusplus
};
#endif

#endif
