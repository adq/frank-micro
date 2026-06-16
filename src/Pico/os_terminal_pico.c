/*
 * frank-micro — BBC Micro for RP2350
 * os_terminal_pico.c — Terminal stub (USB CDC printf handles I/O).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_terminal.h"
#include <stdio.h>
#include <stdint.h>

intptr_t os_terminal_get_stdin_handle(void)  { return 0; }
intptr_t os_terminal_get_stdout_handle(void) { return 1; }
void os_terminal_setup(intptr_t handle)      { (void)handle; }

int os_terminal_has_readable_bytes(intptr_t handle) {
    (void)handle;
    return 0; /* No interactive terminal on Pico */
}

int os_terminal_handle_read_byte(intptr_t handle, uint8_t* p_byte) {
    (void)handle; (void)p_byte;
    return 0;
}

int os_terminal_handle_write_byte(intptr_t handle, uint8_t byte) {
    (void)handle;
    putchar(byte);
    return 1;
}

void os_terminal_set_ctrl_c_callback(void (*p_interrupt_callback)(void)) {
    (void)p_interrupt_callback;
}
