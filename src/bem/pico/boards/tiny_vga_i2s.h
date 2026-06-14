/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_TINY_VGA_I2S_H
#define _BOARDS_TINY_VGA_I2S_H

#define PICO_SCANVIDEO_COLOR_PIN_BASE 2
#define PICO_SCANVIDEO_COLOR_PIN_COUNT 3
#define PICO_SCANVIDEO_SYNC_PIN_BASE 26

#define PICO_SCANVIDEO_ALPHA_PIN 3

// don't want these used
#define PICO_SCANVIDEO_PIXEL_RSHIFT 0
#define PICO_SCANVIDEO_PIXEL_GSHIFT 1
#define PICO_SCANVIDEO_PIXEL_BSHIFT 2
#define PICO_SCANVIDEO_PIXEL_RCOUNT 1
#define PICO_SCANVIDEO_PIXEL_GCOUNT 1
#define PICO_SCANVIDEO_PIXEL_BCOUNT 1

#define PICO_SCANVIDEO_PIXEL_FROM_RGB8(r, g, b) ((((b)>>7u)<<2u)|(((g)>>7u)<<1u)|(((r)>>7u)<<0u))
#define PICO_SCANVIDEO_PIXEL_FROM_RGB5(r, g, b) ((((b)>>4u)<<2u)|(((g)>>4u)<<1u)|(((r)>>4u)<<0u))
#define PICO_SCANVIDEO_R5_FROM_PIXEL(p) (((p)&1u)*0x1fu)
#define PICO_SCANVIDEO_G5_FROM_PIXEL(p) ((((p)&2u)>>1u)*0x1fu)
#define PICO_SCANVIDEO_B5_FROM_PIXEL(p) ((((p)&4u)>>2u)*0x1fu)

#define PICO_AUDIO_I2S_DATA_PIN 5
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 6

#include "boards/pimoroni_tiny2040.h"
#endif
