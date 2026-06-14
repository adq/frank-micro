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
#include "fpu_enable.h"
#include "HDMI.h"
#include "psram_init.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"

#if defined(HDMI_PIO_AUDIO)
#include "frank_hdmi.h"
#endif

#include "frank_gui.h"

#ifndef FRANK_MICRO_VERSION
#define FRANK_MICRO_VERSION "dev"
#endif

#define FB_W 320
#define FB_H 256

/* b-em entry point (main.c: #define main _al_mangled_main). */
extern int _al_mangled_main(int argc, char **argv);

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

    printf("\n========================================\n");
    printf("  frank-micro — BBC Micro for RP2350 (b-em)\n");
    printf("  version %s\n", FRANK_MICRO_VERSION);
    printf("  cpu=%lu MHz\n", clock_get_hz(clk_sys) / 1000000u);
    printf("========================================\n");

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

    printf("Starting b-em...\n");
    _al_mangled_main(0, NULL);

    while (true) tight_loop_contents();
    return 0;
}
