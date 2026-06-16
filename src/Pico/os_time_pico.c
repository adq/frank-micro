/*
 * frank-micro — BBC Micro for RP2350
 * os_time_pico.c — Time/sleep using RP2350 hardware timer.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_time.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdlib.h>
#include <stdint.h>

struct os_time_sleeper {
    uint64_t next_wake_us;
};

void os_time_setup_hi_res(void) {
    /* Nothing to do — RP2350 timer is always high-res (1 µs). */
}

uint64_t os_time_get_us(void) {
    return time_us_64();
}

struct os_time_sleeper* os_time_create_sleeper(void) {
    struct os_time_sleeper* p = malloc(sizeof(*p));
    if (p) p->next_wake_us = 0;
    return p;
}

void os_time_free_sleeper(struct os_time_sleeper* p_sleeper) {
    free(p_sleeper);
}

void os_time_sleeper_sleep_us(struct os_time_sleeper* p_sleeper, uint64_t us) {
    if (p_sleeper->next_wake_us == 0) {
        p_sleeper->next_wake_us = time_us_64() + us;
    } else {
        p_sleeper->next_wake_us += us;
    }
    uint64_t now = time_us_64();
    if (p_sleeper->next_wake_us > now) {
        uint64_t delta = p_sleeper->next_wake_us - now;
        /* This runs from the 6502 timer callback on the CPU core. The SDK's
         * sleep_us() arms a timer alarm and blocks on the alarm-pool IRQ, which
         * is unsafe to re-enter from here (it HardFaults the core). Use a pure
         * hardware-timer busy-wait, which touches no IRQ/alarm machinery. The
         * emulation runs slower than real time, so this pacing sleep is short
         * and rare in practice; clamp it so a stale timestamp can't wedge the
         * core in a long spin. */
        if (delta > 20000) {
            delta = 20000;
            p_sleeper->next_wake_us = now + delta;
        }
        busy_wait_us(delta);
    }
}
