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
#include "frank_settings.h"
#include "frank_loader.h"
#include "frank_ui.h"
#include "usbhid_wrapper.h"   /* usbhid_wrapper_init() — stubs to {} when off */

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
    static bool     inited = false;

    /* One-shot init on the first frame, i.e. after b-em's main_init() has run
     * (so the disc subsystem and config are ready). */
    if (!inited) {
        inited = true;
        frank_settings_apply_live();   /* monitor palette, sound, volume, speed */
        frank_disk_autoload();         /* mount discs from settings / known names */
        if (frank_disc_name(0)) {
            frank_disc_request_boot();  /* SHIFT-BREAK an autoloaded disc */
        } else {
#ifndef FRANK_NO_AUTOBOOT
            frank_disc_preload();       /* fall back to the bundled demo disc */
#endif
        }
    }

    /* Release the autoboot SHIFT key once the disc has begun booting. */
#ifndef FRANK_NO_AUTOBOOT
    extern void frank_disc_autoboot_tick(void);
    frank_disc_autoboot_tick();
#endif

    /* Poll the USB-CDC serial console for injected keystrokes.  Disabled when
     * USB HID is enabled — the native USB port is then a HID host, not CDC. */
#ifndef USB_HID_ENABLED
    frank_console_poll();
#endif

    /* Feed the watchdog so a genuine hang (not a fault) is detected/rebooted. */
    crash_handler_feed();

    /* ── 50 Hz pacer ─────────────────────────────────────────────────────────
     *
     * Sleep until the next 20 ms wall-clock boundary so the emulator runs at
     * exactly real-time.  The audio producer (b-em) and the HDMI consumer run
     * off independent clocks, but the producer outputs the BBC's 31250 Hz
     * resampled to the consumer's standard 32000 Hz (frank_audio.c), so the
     * average rates match and the primed ring absorbs the jitter — no rate
     * lock needed (matches the working frank-cpc).
     *
     * When "Limit Speed" is Off the pacer is skipped so the emulator runs as
     * fast as the RP2350 allows (useful for fast loaders). */
    uint64_t now = time_us_64();
    if (!g_frank_limit_speed) {
        next_frame = 0;   /* re-prime when throttling resumes */
    } else if (next_frame == 0) {
        next_frame = now + FRAME_PERIOD_US;
    } else {
        if ((int64_t)(next_frame - now) > 0)
            busy_wait_until(from_us_since_boot(next_frame));
        next_frame += FRAME_PERIOD_US;
        /* Resync if we've fallen far behind (e.g. a slow frame). */
        now = time_us_64();
        if ((int64_t)(now - next_frame) > (int64_t)FRAME_PERIOD_US)
            next_frame = now + FRAME_PERIOD_US;
    }

    /* ── speed measurement ───────────────────────────────────────────────── */
    frames++;
    if (t_last == 0) { t_last = time_us_64(); return; }
    uint64_t dt = time_us_64() - t_last;
    if (dt >= 1000000u) {
#ifdef FRANK_PERF_LOG
        /*
         * Diagnostic telemetry.  OFF by default: printf() to USB-CDC can
         * block Core 0 for up to PICO_STDIO_USB_STDOUT_TIMEOUT_US whenever the
         * host has the port open but is not draining it, which stalls the
         * audio producer and causes a recurring dropout.  Enable only when a
         * terminal is actively reading the port.
         */
        float fps = frames * 1000000.0f / (float)dt;
        float pct = fps * 100.0f / 50.0f;  /* BBC frame rate is 50 Hz */
#if defined(HDMI_PIO_AUDIO)
        extern volatile uint32_t frank_hdmi_heartbeat_frames;
        static uint32_t hdmi_last = 0;
        uint32_t hdmi_now = frank_hdmi_heartbeat_frames;
        uint32_t hdmi_fps = hdmi_now - hdmi_last;
        hdmi_last = hdmi_now;
        printf("PERF: emu=%.1f fps (%.0f%%)  hdmi=%lu  fill=%lu/%lu  uf=%lu\n",
               (double)fps, (double)pct, (unsigned long)hdmi_fps,
               (unsigned long)frank_hdmi_audio_fill(),
               (unsigned long)frank_hdmi_audio_capacity(),
               (unsigned long)frank_hdmi_audio_underflows);
#else
        printf("PERF: emu=%.1f fps (%.0f%% real-time)\n", (double)fps, (double)pct);
#endif
#endif /* FRANK_PERF_LOG */
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
#ifndef USB_HID_ENABLED
    /* Wait for USB-CDC serial to enumerate so early printf output is visible.
     * Skipped when USB HID is enabled: the native USB port is in host mode,
     * there is no CDC device to wait for, and we want input ASAP. */
    for (int i = 0; i < 6; ++i) sleep_ms(250);  /* USB CDC enumeration */
#endif

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

    /* USB HID host (keyboard + gamepad).  usbhid_wrapper_init() is a no-op
     * stub when USB HID is disabled, so this is safe to call unconditionally. */
    usbhid_wrapper_init();
#ifdef USB_HID_ENABLED
    printf("USB HID host ready\n");
#endif

    /* Wired NES/SNES gamepad (independent of USB HID; always available). */
    frank_gamepad_init();
    printf("NES gamepad ready\n");

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
    frank_ui_init();      /* install the overlay palette (indices 240-247) */
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

    /* Load persisted settings before b-em starts so frank_boot_model() picks
     * the configured model.  Discs are mounted on the first emulated frame
     * (frank_perf_tick), once b-em's disc subsystem is initialised. */
    if (fr == FR_OK)
        frank_settings_load();

    printf("Starting b-em...\n");
    _al_mangled_main(0, NULL);

    while (true) tight_loop_contents();
    return 0;
}
