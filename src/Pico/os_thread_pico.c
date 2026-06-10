/*
 * frank-micro — BBC Micro for RP2350
 * os_thread_pico.c — Thread stub.
 *
 * Architecture on RP2350:
 *   Core 0: BBC CPU (runs inline — the "thread" is just a stored function)
 *   Core 1: HDMI audio (frank_hdmi_run_core1 or I2S render loop)
 *
 * bbc_run_async() calls os_thread_create() TWICE:
 *   1. bbc_cpu_thread    ← this is the BBC CPU — we must run this one
 *   2. sound_play_thread ← spawned by sound_start_playing() — IGNORE on Pico
 *                          (sound is pushed to HDMI ring by os_sound_pico.c)
 *
 * We keep only the FIRST registration.  After bbc_run_async() returns,
 * main.c calls micro_run_bbc_cpu() to execute the BBC CPU loop on Core 0.
 */
#include "os_thread.h"
#include <stdlib.h>
#include <stdint.h>

struct os_thread_struct {
    void* (*p_func)(void*);
    void* p_arg;
};

static struct os_thread_struct g_bbc_thread;
static int g_bbc_thread_set = 0;

struct os_thread_struct* os_thread_create(void* p_func, void* p_arg) {
    if (!g_bbc_thread_set) {
        /* First registration = BBC CPU thread. */
        g_bbc_thread.p_func = (void* (*)(void*))p_func;
        g_bbc_thread.p_arg  = p_arg;
        g_bbc_thread_set    = 1;
    }
    /* Subsequent registrations (sound_play_thread etc.) are silently ignored.
     * Sound output is handled by os_sound_pico.c → HDMI ring buffer. */
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
