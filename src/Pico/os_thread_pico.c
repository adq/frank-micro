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
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-micro
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "os_thread.h"
#include <stdlib.h>
#include <stdint.h>
#include "fpu_enable.h"

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
    /* Enable the FPU on this core immediately before the BBC CPU loop. The
     * boot/init path leaves Core 0's CPACR with the FPU disabled, but GCC emits
     * VFP spills (e.g. vpush {d8}) throughout the interpreter/timing code; those
     * HardFault if the FPU is off. Enabling it here — the last point before the
     * loop runs — is robust against anything earlier in boot clearing CPACR. */
    frank_enable_fpu();
    if (g_bbc_thread.p_func) {
        g_bbc_thread.p_func(g_bbc_thread.p_arg);
    }
}
