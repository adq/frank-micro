/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_screenshot.h — save the BBC framebuffer as a BMP to the SD card.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_SCREENSHOT_H
#define FRANK_SCREENSHOT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set by a trigger (PrintScreen key or the F12 settings menu) to request a
 * screenshot on the next completed frame.  The capture is performed before the
 * UI overlay is drawn so the saved image is a clean BBC frame. */
extern volatile bool g_frank_screenshot_pending;

/* Save the given 320x256 8-bpp indexed framebuffer as an 8-bit BMP to
 * /micro/screenshot/BBC_NNNN.BMP, using the live HDMI palette for colours.
 * Returns 0 on success, -1 on error. */
int frank_screenshot_save(const uint8_t *fb);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_SCREENSHOT_H */
