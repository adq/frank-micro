/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_platform.c — board bring-up + entry point.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Core 0: hardware init, then the b-em emulator (CPU + synchronous video).
 * Core 1: frank HDMI encoder (frank_hdmi_run_core1), scanning out SCREEN[].
 *
 * We replace b-em's Allegro entry shim: al_stub's main() is disabled via
 * -DFRANK_MAIN and we call the b-em main (_al_mangled_main) ourselves after
 * bringing up the frank hardware.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "hardware/gpio.h"

#include "board_config.h"
#include "crash_handler.h"
#include "fpu_enable.h"
#include "HDMI.h"
#include "psram_init.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"

#if defined(HDMI_PIO_AUDIO)
#include "frank_hdmi.h"
#endif

#include "frank_gui.h"
#include "frank_disc.h"
#include "frank_console.h"

#ifndef FRANK_MICRO_VERSION
#define FRANK_MICRO_VERSION "dev"
#endif

#define FB_W 320
#define FB_H 256

/* Crash diagnostics mirror (populated by crash_handler_check_and_print;
 * readable via the debug probe even when USB-CDC serial misses the print). */
volatile uint32_t g_crash_fault = 0;
volatile uint32_t g_crash_pc    = 0;
volatile uint32_t g_crash_cfsr  = 0;
volatile uint32_t g_crash_count = 0;

/* b-em entry point (main.c: #define main _al_mangled_main). */
extern int _al_mangled_main(int argc, char **argv);

/*
 * Per-frame pacing + speed measurement.  al_wait_for_event() calls this
 * exactly once per emulated 50Hz frame (each m6502_exec runs 40000 cycles ==
 * 20ms of BBC time).  We sleep until the next 20ms wall-clock boundary so the
 * emulator runs at exactly real-time (the asm CPU is faster than real-time at
 * 252MHz, so without this it would run ~6% fast).  Every wall-clock second we
 * also print the achieved frame rate and HDMI encoder heartbeat.
 */
#define FRAME_PERIOD_US 20000u   /* 50 Hz */

void frank_perf_tick(void) {
    static uint32_t frames = 0;
    static uint64_t t_last = 0;
    static uint64_t next_frame = 0;

    /* Release the autoboot SHIFT key once the disc has begun booting. */
#ifndef FRANK_NO_AUTOBOOT
    extern void frank_disc_autoboot_tick(void);
    frank_disc_autoboot_tick();
#endif

    /* Poll the USB-CDC serial console for injected keystrokes. */
    frank_console_poll();

    /* Feed the watchdog so a genuine hang (not a fault) is detected/rebooted. */
    crash_handler_feed();

    /* ── 50 Hz pacer with audio-ring rate lock ───────────────────────────────
     *
     * The producer (BBC emulation on core 0) and the HDMI audio consumer
     * (core 1) run off independent clocks: the producer is paced here against
     * time_us_64(), the consumer drains the ring at the HDMI audio rate locked
     * to the TMDS pixel clock.  Even a tiny mismatch makes the ring slowly
     * drift toward empty (or full) and underflow (or overflow) every few
     * seconds — heard as a brief recurring stop in the music, even on the
     * title screen where there is no disc I/O.
     *
     * Fix: close the loop.  Each frame we trim the pacing period by a small
     * proportional term derived from the ring fill, so the emulation runs a
     * hair faster when the ring is draining and a hair slower when it is
     * filling.  This locks the average production rate to the consumer's clock
     * and parks the fill at ~half.  The trim is bounded to a fraction of a
     * percent, so neither video nor audio pitch is perceptibly affected. */
    uint32_t period = FRAME_PERIOD_US;
#if defined(HDMI_PIO_AUDIO)
    {
        uint32_t cap  = frank_hdmi_audio_capacity();
        if (cap) {
            int target = (int)(cap >> 1);          /* park the ring half-full   */
            int fill   = (int)frank_hdmi_audio_fill();
            int err    = target - fill;            /* >0 ⇒ draining ⇒ go faster */
            int trim   = err / 8;                  /* gentle proportional gain  */
            if (trim >  200) trim =  200;          /* clamp to ±1% of 20 ms     */
            if (trim < -200) trim = -200;
            period = (uint32_t)((int)FRAME_PERIOD_US - trim);
        }
    }
#endif

    /* ── 50 Hz pacer ─────────────────────────────────────────────────────── */
    uint64_t now = time_us_64();
    if (next_frame == 0) {
        next_frame = now + period;
    } else {
        if ((int64_t)(next_frame - now) > 0)
            busy_wait_until(from_us_since_boot(next_frame));
        next_frame += period;
        /* Resync if we've fallen far behind (e.g. a slow frame). */
        now = time_us_64();
        if ((int64_t)(now - next_frame) > (int64_t)FRAME_PERIOD_US)
            next_frame = now + period;
    }

    /* ── speed measurement ───────────────────────────────────────────────── */
    frames++;
    if (t_last == 0) { t_last = time_us_64(); return; }
    uint64_t dt = time_us_64() - t_last;
    if (dt >= 1000000u) {
        float fps = frames * 1000000.0f / (float)dt;
        float pct = fps * 100.0f / 50.0f;  /* BBC frame rate is 50 Hz */
#if defined(HDMI_PIO_AUDIO)
        extern volatile uint32_t frank_hdmi_heartbeat_frames;
        static uint32_t hdmi_last = 0;
        uint32_t hdmi_now = frank_hdmi_heartbeat_frames;
        uint32_t hdmi_fps = hdmi_now - hdmi_last;
        hdmi_last = hdmi_now;
        static uint32_t uf_last = 0;
        uint32_t uf_now  = frank_hdmi_audio_underflows;
        uint32_t uf_rate = uf_now - uf_last;
        uf_last = uf_now;
        extern volatile uint32_t frank_audio_produced, frank_audio_dropped, frank_audio_rate_hz, frank_audio_maxrun, frank_audio_gaps;
        extern volatile int32_t frank_audio_gapval;
        static uint32_t prod_last = 0, drop_last = 0;
        uint32_t prod_now = frank_audio_produced, drop_now = frank_audio_dropped;
        uint32_t prod_rate = prod_now - prod_last, drop_rate = drop_now - drop_last;
        prod_last = prod_now; drop_last = drop_now;
        uint32_t maxrun = frank_audio_maxrun; frank_audio_maxrun = 0;
        uint32_t gaps = frank_audio_gaps; frank_audio_gaps = 0;
        printf("PERF: emu=%.1f fps (%.0f%%)  hdmi=%lu  fill=%lu/%lu  "
               "uf=%lu  prod=%lu/s drop=%lu/s rate=%lu  maxrun=%lu gaps=%lu gapval=%ld\n",
               (double)fps, (double)pct, (unsigned long)hdmi_fps,
               (unsigned long)frank_hdmi_audio_fill(),
               (unsigned long)frank_hdmi_audio_capacity(),
               (unsigned long)uf_rate,
               (unsigned long)prod_rate, (unsigned long)drop_rate,
               (unsigned long)frank_audio_rate_hz,
               (unsigned long)maxrun, (unsigned long)gaps, (long)frank_audio_gapval);
#else
        printf("PERF: emu=%.1f fps (%.0f%% real-time)\n", (double)fps, (double)pct);
#endif
        frames = 0;
        t_last = time_us_64();
    }
}

/* ── Flash timing for overclocked operation ─────────────────────────────── */
static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz, int flash_max_mhz) {
    const int clock_hz  = cpu_mhz * 1000000;
    const int max_flash = flash_max_mhz * 1000000;
    int divisor = (clock_hz + max_flash - (max_flash >> 4) - 1) / max_flash;
    if (divisor < 1) divisor = 1;
    if (divisor == 1 && clock_hz >= 166000000) divisor = 2;
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) rxdelay++;
    qmi_hw->m[0].timing = 0x60007000u
        | ((uint32_t)rxdelay << QMI_M0_TIMING_RXDELAY_LSB)
        | ((uint32_t)divisor << QMI_M0_TIMING_CLKDIV_LSB);
}

/* Standard BBC physical-colour palette (indices 0..7). */
static void install_palette(void) {
    static const uint32_t bbc[8] = {
        0x000000, 0xFF0000, 0x00FF00, 0xFFFF00,
        0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };
    for (int i = 0; i < 256; i++)
        graphics_set_palette((uint8_t)i, bbc[i & 7]);
}

int main(void) {
    frank_enable_fpu();
#if CPU_CLOCK_MHZ > 252
    vreg_disable_voltage_limit();
    vreg_set_voltage(CPU_VOLTAGE);
    set_flash_timings(CPU_CLOCK_MHZ, FLASH_MAX_FREQ_MHZ);
    sleep_ms(100);
#endif
    if (!set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, false))
        set_sys_clock_khz(252 * 1000, true);

    stdio_init_all();
    for (int i = 0; i < 6; ++i) sleep_ms(250);  /* USB CDC enumeration */

    crash_handler_check_and_print();   /* report a fault from the previous run */

    printf("\n========================================\n");
    printf("  frank-micro — BBC Micro for RP2350 (b-em)\n");
    printf("  version %s\n", FRANK_MICRO_VERSION);
    printf("  cpu=%lu MHz\n", clock_get_hz(clk_sys) / 1000000u);
    printf("========================================\n");

    crash_handler_install();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif

    bool psram_ok = psram_init(get_psram_pin());
    printf("PSRAM: %s\n", psram_ok ? "OK" : "not present (OK)");

    ps2kbd_init();
    printf("PS/2 keyboard ready\n");

    /* Video: force HDMI output (frank capture-card pipeline). */
#if defined(HDMI_PIO_AUDIO)
    SELECT_VGA = false;
#endif
    graphics_init(g_out_HDMI);
    graphics_set_buffer(SCREEN[0]);
    graphics_set_res(FB_W, FB_H);
    graphics_set_shift((320 - FB_W) / 2, 0);
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
    install_palette();
    printf("Video initialised (%dx%d)\n", FB_W, FB_H);

#if defined(HDMI_PIO_AUDIO)
    multicore_launch_core1(frank_hdmi_run_core1);
    sleep_ms(50);
    printf("HDMI encoder started on core1\n");
#endif

    /* SD card (optional for BASIC — ROMs are embedded in flash). */
    static FATFS g_fs;
    FRESULT fr = f_mount(&g_fs, "", 1);
    printf("SD card mount: %s\n", fr == FR_OK ? "OK" : "not mounted");

    /* Read the autoboot disc image while the SD bus is idle (no emulation). */
    if (fr == FR_OK)
        frank_disc_preload();

    printf("Starting b-em...\n");
    _al_mangled_main(0, NULL);

    while (true) tight_loop_contents();
    return 0;
}
