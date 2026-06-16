/*
 * frank-micro — BBC Micro for RP2350
 * os_window_pico.c — Window stub (no GUI window on RP2350).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_window.h"
#include <stdlib.h>

struct os_window_struct { int dummy; };

void os_window_main_thread_start(void (*p_beebjit_main)(void)) {
    /* On Pico there is no platform event loop — just call main directly. */
    p_beebjit_main();
}

struct os_window_struct* os_window_create(uint32_t width, uint32_t height) {
    (void)width; (void)height;
    struct os_window_struct* p = malloc(sizeof(*p));
    return p;
}

void os_window_destroy(struct os_window_struct* p_window) { free(p_window); }
void os_window_set_name(struct os_window_struct* p, const char* n) { (void)p; (void)n; }
void os_window_set_keyboard_callback(struct os_window_struct* p, struct keyboard_struct* k) { (void)p; (void)k; }
void os_window_set_focus_lost_callback(struct os_window_struct* p, void (*cb)(void*), void* obj) { (void)p; (void)cb; (void)obj; }
uint32_t* os_window_get_buffer(struct os_window_struct* p) { (void)p; return NULL; }
intptr_t os_window_get_handle(struct os_window_struct* p) { (void)p; return 0; }
void os_window_sync_buffer_to_screen(struct os_window_struct* p) { (void)p; }
void os_window_process_events(struct os_window_struct* p) { (void)p; }
int os_window_is_closed(struct os_window_struct* p) { (void)p; return 0; }
