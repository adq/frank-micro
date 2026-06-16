/*
 * frank-micro — BBC Micro for RP2350
 * os_fault_pico.c — Fault handler stub.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_fault.h"
#include "pico/stdlib.h"
#include <stdio.h>

void os_fault_register_handler(void (*p_fault_callback)(uintptr_t* p_host_rip,
                                                         uintptr_t  host_fault_addr,
                                                         int        is_illegal,
                                                         int        is_exec,
                                                         int        is_write,
                                                         uintptr_t  host_rdi)) {
    (void)p_fault_callback; /* No signal-based fault handling on Pico */
}

void os_fault_bail(void) {
    printf("os_fault_bail called\n");
    while (true) tight_loop_contents();
}

void os_debug_trap(void) {
    __breakpoint();
}
