/*
 * frank-micro — BBC Micro for RP2350
 * jit_stub.c — Stubs for CPU backends not supported on Pico.
 *
 * JIT and inturbo CPU modes are not available on Cortex-M33.
 * The null ASM backends (asm/null/) handle all other JIT symbols.
 * This file only provides stubs for the two CPU driver factory functions.
 */
#include "inturbo.h"
#include "jit.h"
#include <stddef.h>

struct cpu_driver* inturbo_create(struct cpu_driver_funcs* p_funcs) {
    (void)p_funcs;
    return NULL;
}

struct cpu_driver* jit_create(struct cpu_driver_funcs* p_funcs) {
    (void)p_funcs;
    return NULL;
}
