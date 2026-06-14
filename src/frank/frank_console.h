/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_console.h
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef FRANK_CONSOLE_H
#define FRANK_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Poll the USB-CDC serial console for key-injection commands; call once
 * per emulated frame. */
void frank_console_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* FRANK_CONSOLE_H */
