/*
 * frank-micro — BBC Micro for RP2350
 *
 * Copyright (c) 2026 Andrew de Quincey <adq@lidskialf.net>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * hstx_pins.h — HSTX output pins.
 *
 * Replaces the upstream file, which hardcoded the Adafruit Metro pinout.
 * The pin numbers come from the board header, so adding a second HSTX board
 * means declaring HDMI_PIN_* there and changing nothing here.
 *
 * video_output.c names the positive pin of each pair.  GPIOHSTXINVERTED says
 * where the negative pin of that pair is: 1 means one GPIO below the positive
 * pin, 0 means one above.
 */

#ifndef HSTX_PINS_H
#define HSTX_PINS_H

#include "board_config.h"

#ifndef HDMI_PIN_CLKP
#error "This board declares no HDMI_PIN_CLKP, so it cannot drive HSTX video"
#endif

/* TMDS lane 0 is blue, lane 1 green, lane 2 red. */
#define GPIOHSTXCK HDMI_PIN_CLKP
#define GPIOHSTXD0 HDMI_PIN_D0P
#define GPIOHSTXD1 HDMI_PIN_D1P
#define GPIOHSTXD2 HDMI_PIN_D2P

/* On the Fruit Jam the negative pin of each pair is the even one, so it sits
 * one GPIO below the positive pin.  Same ordering as board_m2.h. */
#if (HDMI_PIN_CLKN + 1) == HDMI_PIN_CLKP
#define GPIOHSTXINVERTED 1
#else
#define GPIOHSTXINVERTED 0
#endif

/* Lowest GPIO of the eight-pin HSTX block.  HSTX output bit 0 is GPIO 12 on
 * every RP2350, so this is a property of the chip, not of the board. */
#define HSTX_GPIO_BASE 12

/* Calculate the HSTX output bit driving a GPIO. */
#define HSTX_BIT_FROM_GPIO(gpio) ((gpio) - HSTX_GPIO_BASE)

#endif /* HSTX_PINS_H */
