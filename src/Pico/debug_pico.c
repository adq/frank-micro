/*
 * frank-micro — BBC Micro for RP2350
 * debug_pico.c — Minimal debug stubs for Pico build.
 *
 * The full beebjit debugger is desktop-only (uses a terminal REPL).
 * On RP2350 we provide empty stubs so the rest of the emulator compiles
 * and links, but the debugger is never activated.
 */
#include "debug.h"
#include "cpu_driver.h"
#include <stdlib.h>
#include <stdint.h>

struct debug_struct {
    volatile int interrupt;
};

static struct debug_struct g_debug;

struct debug_struct* debug_create(struct bbc_struct* p_bbc,
                                  int debug_flag,
                                  struct bbc_options* p_options) {
    (void)p_bbc; (void)debug_flag; (void)p_options;
    g_debug.interrupt = 0;
    return &g_debug;
}

void debug_init(struct debug_struct* p_debug) {
    (void)p_debug;
}

void debug_destroy(struct debug_struct* p_debug) {
    (void)p_debug;
}

volatile int* debug_get_interrupt(struct debug_struct* p_debug) {
    return &p_debug->interrupt;
}

void debug_set_commands(struct debug_struct* p_debug, const char* p_commands) {
    (void)p_debug; (void)p_commands;
}

int debug_subsystem_active(void* p) {
    (void)p;
    return 0;  /* Debugger is always inactive on Pico */
}

void* debug_callback(struct cpu_driver* p_cpu_driver, int do_irq) {
    (void)p_cpu_driver; (void)do_irq;
    return NULL;  /* Never called since debug_subsystem_active returns 0 */
}
