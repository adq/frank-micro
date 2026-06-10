/*
 * frank-micro — BBC Micro for RP2350
 * os_time_pico.c — Time/sleep using RP2350 hardware timer.
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
        sleep_us(p_sleeper->next_wake_us - now);
    }
}
