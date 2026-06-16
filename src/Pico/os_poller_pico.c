/*
 * frank-micro — BBC Micro for RP2350
 * os_poller_pico.c — Poller stub.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_poller.h"
#include <stdlib.h>

struct os_poller_struct { int dummy; };

struct os_poller_struct* os_poller_create(void) {
    return malloc(sizeof(struct os_poller_struct));
}
void os_poller_destroy(struct os_poller_struct* p) { free(p); }
void os_poller_add_handle(struct os_poller_struct* p, intptr_t h) { (void)p; (void)h; }
void os_poller_poll(struct os_poller_struct* p) { (void)p; }
int  os_poller_handle_triggered(struct os_poller_struct* p, uint32_t i) { (void)p; (void)i; return 0; }
