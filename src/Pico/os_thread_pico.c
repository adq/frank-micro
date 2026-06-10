/*
 * frank-micro — BBC Micro for RP2350
 * os_thread_pico.c — Thread stub.
 *
 * Architecture on RP2350:
 *   Core 0: BBC CPU (runs inline — the "thread" is just a stored function)
 *   Core 1: HDMI audio (frank_hdmi_run_core1 or I2S render loop)
 *
 * os_thread_create() stores the BBC CPU function pointer but does NOT
 * launch Core 1.  After bbc_run_async() returns, main.c calls
 * micro_run_bbc_cpu() to execute the BBC CPU loop directly on Core 0.
 */
#include "os_thread.h"
#include <stdlib.h>
#include <stdint.h>

struct os_thread_struct {
    void* (*p_func)(void*);
    void* p_arg;
};

static struct os_thread_struct g_bbc_thread;

struct os_thread_struct* os_thread_create(void* p_func, void* p_arg) {
    /* Store but do NOT launch — BBC CPU runs on Core 0 inline. */
    g_bbc_thread.p_func = (void* (*)(void*))p_func;
    g_bbc_thread.p_arg  = p_arg;
    return &g_bbc_thread;
}

intptr_t os_thread_destroy(struct os_thread_struct* p_thread_struct) {
    (void)p_thread_struct;
    return 0;
}

/* Called from main.c after HDMI is running on Core 1. */
void micro_run_bbc_cpu(void) {
    if (g_bbc_thread.p_func) {
        g_bbc_thread.p_func(g_bbc_thread.p_arg);
    }
}
