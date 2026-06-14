/*
 * frank-micro — BBC Micro for RP2350
 * main.c — Hardware init + BBC emulator entry point.
 *
 * Core 0: hardware init, SD card, PS/2, video, BBC emulator (interpreter)
 * Core 1: I2S audio (VGA mode) or HDMI audio (frank_hdmi_run_core1)
 *
 * The BBC CPU runs on Core 1 via os_thread_create().
 * Core 0 handles vsync (micro_frame_present), keyboard, and serial console.
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "board_config.h"
#include "fpu_enable.h"
#include "HDMI.h"
#include "crash_handler.h"
#include "psram_init.h"
#include "crash_handler.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"
#include "usbhid_wrapper.h"

#if defined(HDMI_PIO_AUDIO)
#include "frank_hdmi.h"
#include "audio.h"
#endif

#include "pwm_audio.h"
#include "micro_settings.h"
#include "micro_boot.h"
#include "micro_loader.h"
#include "micro_ui.h"
#include "micro_serial_console.h"
#include "ui_draw.h"

/* beebjit headers */
#include "bbc.h"
#include "bbc_options.h"
#include "config.h"
#include "keyboard.h"
#include "render.h"
#include "video.h"
#include "sound.h"
#include "os_sound.h"
#include "os_channel.h"
#include "util.h"

#ifndef FRANK_MICRO_VERSION
#define FRANK_MICRO_VERSION "dev"
#endif

/* ── Framebuffer ─────────────────────────────────────────────────────────── */
#define FB_W  MICRO_FB_WIDTH
#define FB_H  MICRO_FB_HEIGHT

/* True double buffer: the BBC renderer draws into the back buffer while the
 * HDMI core scans out the front buffer, eliminating tearing.  80 KB each. */
static uint8_t __attribute__((aligned(4))) screen_mem[2][FB_W * MICRO_SCREEN_LINES];
uint8_t* SCREEN[2] = { screen_mem[0], screen_mem[1] };
volatile uint32_t current_buffer = 0;

/* ── Global BBC struct ───────────────────────────────────────────────────── */
struct bbc_struct* g_p_bbc = NULL;

/* ── Flash timing ─────────────────────────────────────────────────────────── */
static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz, int flash_max_mhz) {
    const int clock_hz     = cpu_mhz * 1000000;
    const int max_flash    = flash_max_mhz * 1000000;
    int divisor = (clock_hz + max_flash - (max_flash >> 4) - 1) / max_flash;
    if (divisor < 1) divisor = 1;
    if (divisor == 1 && clock_hz >= 166000000) divisor = 2;
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) rxdelay++;
    qmi_hw->m[0].timing = 0x60007000u
        | ((uint32_t)rxdelay << QMI_M0_TIMING_RXDELAY_LSB)
        | ((uint32_t)divisor << QMI_M0_TIMING_CLKDIV_LSB);
}

/* ── Platform hooks (declared in platform.c) ─────────────────────────────── */
extern void micro_init_palette(void);
extern void micro_frame_present(void);
extern void micro_keyboard_poll(void);
extern unsigned i2s_ring_push(const int16_t* samples, unsigned count);
extern unsigned i2s_ring_free(void);

#if defined(HDMI_PIO_AUDIO)
extern void micro_render_core_i2s(void);
extern volatile bool* micro_core1_ready_ptr(void);
#endif

/* ── pico_vsync_handler — called from within bbc.c at every BBC vsync ────── */
/* Render performance instrumentation (read via RPERF serial command). */
volatile uint32_t g_vsync_calls = 0;
volatile uint32_t g_render_calls = 0;
volatile uint64_t g_render_us_accum = 0;
volatile uint64_t g_present_us_accum = 0;

void pico_render_perf_get(uint32_t* p_vsync, uint32_t* p_render,
                          uint64_t* p_render_us, uint64_t* p_present_us) {
    *p_vsync = g_vsync_calls;
    *p_render = g_render_calls;
    *p_render_us = g_render_us_accum;
    *p_present_us = g_present_us_accum;
}
void pico_render_perf_reset(void) {
    g_vsync_calls = 0; g_render_calls = 0;
    g_render_us_accum = 0; g_present_us_accum = 0;
}

void pico_vsync_handler(int do_full_render) {
    /* In externally-clocked (polled CRTC) mode the per-tick rendering loop is
     * skipped for speed, so the frame is drawn here in one pass from current
     * CRTC + video memory state before being presented. */
    g_vsync_calls++;
    if (do_full_render && g_p_bbc) {
        uint64_t t0 = time_us_64();
        video_render_full_frame(bbc_get_video(g_p_bbc));
        g_render_us_accum += (time_us_64() - t0);
        g_render_calls++;
    }
    micro_keyboard_poll();
    uint64_t tp = time_us_64();
    micro_frame_present();
    g_present_us_accum += (time_us_64() - tp);
    crash_handler_feed();  /* keep watchdog alive — Master 128 is slow via PSRAM */
}

/* ── BBC machine setup ─────────────────────────────────────────────────────  */
static void beebjit_main(void) {
    micro_settings_load();

    /* DEBUG: hardcode Master 128 by default (POP cut-scene debugging). This
     * overrides saved settings and skips the crash→Model B fallback so the
     * board always comes up as a Master 128 ready to run Prince of Persia. */
    g_micro_settings.model = MICRO_MODEL_MASTER_128;

#ifdef FRANK_FORCE_MODEL_B
    /* Hard override — ignore saved settings, always boot Model B. */
    g_micro_settings.model = MICRO_MODEL_B;
    micro_settings_save();
    printf("FRANK_FORCE_MODEL_B: overriding to Model B\n");
#endif

    printf("Settings loaded (model=%d)\n", (int)g_micro_settings.model);

    /* ROM load buffers */
    static uint8_t os_rom[0x4000];
    static uint8_t side_rom[0x4000];

    /* Determine BBC model. */
    int mode         = 0; /* 0 = interpreter */
    int is_master    = 0;
    int has_sw_ram   = 0;
    int wd_1770_type = 0;
    const char* os_rom_name        = "roms/os12.rom";
    const char* rom_names[16]      = { NULL };
    int         sideways_ram[16]   = { 0 };

    switch (g_micro_settings.model) {
    case MICRO_MODEL_MASTER_128:
        is_master = 1;
        config_apply_master_128_mos350(&os_rom_name, rom_names,
                                        sideways_ram, &wd_1770_type);
        has_sw_ram = 1;
        break;
    case MICRO_MODEL_MASTER_COMPACT:
        is_master = 1;
        config_apply_master_compact(&os_rom_name, rom_names,
                                     sideways_ram, &wd_1770_type);
        has_sw_ram = 1;
        break;
    default: /* MICRO_MODEL_B */
        rom_names[12] = "roms/basic.rom";
        rom_names[13] = "roms/DFS-0.9.rom";
        break;
    }

    /* Load OS ROM */
    printf("Loading OS from %s...\n", os_rom_name);
    memset(os_rom, 0, sizeof(os_rom));
    uint64_t n = util_file_read_fully(os_rom_name, os_rom, sizeof(os_rom));
    if (n != sizeof(os_rom)) {
        printf("ERROR: OS ROM load failed (%llu/%u bytes from %s)\n",
               (unsigned long long)n, (unsigned)sizeof(os_rom), os_rom_name);
        while (true) sleep_ms(1000);
    }

    printf("BBC Model %s\n", is_master ? "Master 128" : "B");

    /* Create BBC */
    crash_handler_feed();
    g_p_bbc = bbc_create(mode,
                          is_master,
                          has_sw_ram,
                          os_rom,
                          wd_1770_type,
                          0,  /* debug_flag */
                          1,  /* run_flag */
                          0,  /* print_flag */
                          0,  /* fast_flag — we do our own speed limiting */
                          0,  /* accurate_flag */
                          0,  /* fasttape_flag */
                          0,  /* test_map_flag */
                          "",  /* p_opt_flags */
                          "");  /* p_log_flags */

    if (!g_p_bbc) {
        printf("ERROR: bbc_create failed!\n");
        while (true) sleep_ms(1000);
    }
    printf("BBC struct created OK\n");
    crash_handler_feed();

    /* Load sideways ROMs */
    for (int i = 0; i < 16; i++) {
        if (rom_names[i]) {
            crash_handler_feed();
            memset(side_rom, 0, sizeof(side_rom));
            util_file_read_fully(rom_names[i], side_rom, sizeof(side_rom));
            bbc_load_rom(g_p_bbc, (uint8_t)i, side_rom);
        }
        if (sideways_ram[i]) {
            printf("Making sideways RAM bank %d\n", i);
            bbc_make_sideways_ram(g_p_bbc, (uint8_t)i);
        }
    }
    crash_handler_feed();

    /* Set render buffer (our pico_render writes to SCREEN[] directly) */
    {
        struct render_struct* p_render = bbc_get_render(g_p_bbc);
        render_create_internal_buffer(p_render); /* no-op on Pico */
    }

    /* Power on reset */
    crash_handler_feed();
    bbc_power_on_reset(g_p_bbc);
    printf("BBC power on reset done\n");

    /* Set up audio driver.
     * Use a single 50Hz period (882 samples) so sound_set_driver allocates
     * only ~12KB total (driver_frames=1764B + sn_frames=~10KB), keeping
     * all buffers in SRAM and fitting easily in the Master 128 heap budget. */
    {
        struct os_sound_struct* p_drv = os_sound_create(NULL, 44100,
                                                          44100 / 50, 1);
        if (p_drv) {
            os_sound_init(p_drv);
            sound_set_driver(bbc_get_sound(g_p_bbc), p_drv);
        }
    }

    /* Auto-mount disk images */
    micro_disk_autoload();
    if (g_micro_settings.disk_a[0])
        micro_mount_disk(0, g_micro_settings.disk_a);
    if (g_micro_settings.disk_b[0])
        micro_mount_disk(1, g_micro_settings.disk_b);

    /* Set up channels (passed to bbc but bypassed in PICO_BUILD vsync) */
    intptr_t rr, wr, rc, wc;
    os_channel_get_handles(&rr, &wr, &rc, &wc);
    bbc_set_channel_handles(g_p_bbc, rr, wr, rc, wc);

    micro_ui_init();

    printf("Starting BBC emulator (Core 0 inline)...\n");

    /* bbc_run_async() stores the BBC CPU function via os_thread_create()
     * but does NOT actually launch Core 1 (we already launched HDMI there).
     * We then call micro_run_bbc_cpu() to run the BBC CPU directly here
     * on Core 0.  The vsync callback (pico_vsync_handler) fires inline
     * every 50Hz frame, handling display, keyboard, and serial. */
    bbc_run_async(g_p_bbc);

    extern void micro_run_bbc_cpu(void);
    micro_run_bbc_cpu();   /* blocks until BBC exits (never in normal use) */

    printf("BBC CPU exited unexpectedly\n");
    while (true) sleep_ms(1000);
}

volatile uint32_t g_cpacr_entry, g_cpacr_after;
int main(void) {
    g_cpacr_entry = *(volatile uint32_t*)0xE000ED88u;
    frank_enable_fpu();
    g_cpacr_after = *(volatile uint32_t*)0xE000ED88u;
#if CPU_CLOCK_MHZ > 252
    vreg_disable_voltage_limit();
    vreg_set_voltage(CPU_VOLTAGE);
    set_flash_timings(CPU_CLOCK_MHZ, FLASH_MAX_FREQ_MHZ);
    sleep_ms(100);
#endif

    if (!set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, false))
        set_sys_clock_khz(252 * 1000, true);

    stdio_init_all();

    /* USB CDC serial delay — allow host to enumerate */
    for (int i = 0; i < 10; ++i) sleep_ms(500);

    crash_handler_check_and_print();

    printf("\n========================================\n");
    printf("  frank-micro — BBC Micro for RP2350\n");
    printf("  version %s  board " FRANK_MICRO_VERSION "\n", FRANK_MICRO_VERSION);
    printf("  cpu=%lu MHz\n", clock_get_hz(clk_sys) / 1000000u);
    printf("  CPACR entry=0x%08lX after=0x%08lX now=0x%08lX\n",
           (unsigned long)g_cpacr_entry, (unsigned long)g_cpacr_after,
           (unsigned long)*(volatile uint32_t*)0xE000ED88u);
    printf("========================================\n");

    crash_handler_install();

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif

    /* PSRAM init (optional — frank-micro works without it) */
    uint psram_pin = get_psram_pin();
    bool psram_ok  = psram_init(psram_pin);
    printf("PSRAM: %s\n", psram_ok ? "OK" : "not present (OK)");

    memset(screen_mem, 0, sizeof(screen_mem));

    /* PS/2 keyboard */
    ps2kbd_init();
    printf("PS/2 keyboard ready\n");

    /* USB HID (stub — CDC stdio owns USB) */
    usbhid_wrapper_init();

    /* Video init */
#if defined(HDMI_PIO_AUDIO)
    {
        int link = testPins(HDMI_BASE_PIN, HDMI_BASE_PIN + 1);
        /* frank-micro target is HDMI_PIO_AUDIO + capture card pipeline.
         * Force HDMI to avoid false-positive VGA ribbon detection. */
        SELECT_VGA = false;
        printf("Video: link=0x%02X -> HDMI (forced)\n", (unsigned)link);
    }
#endif

    graphics_init(g_out_HDMI);
    graphics_set_buffer(SCREEN[0]);
    graphics_set_res(FB_W, MICRO_SCREEN_LINES);
    graphics_set_shift((320 - FB_W) / 2, 0);
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
    printf("Video initialized (%dx%d)\n", FB_W, MICRO_SCREEN_LINES);

    micro_init_palette();

#if defined(HDMI_PIO_AUDIO)
    if (!SELECT_VGA) {
        /* HDMI mode: Core 1 runs HDMI audio */
        multicore_launch_core1(frank_hdmi_run_core1);
        sleep_ms(50);
        printf("HDMI audio started\n");
    } else {
        /* VGA mode: Core 1 runs I2S audio */
        multicore_launch_core1(micro_render_core_i2s);
        volatile bool* ready = micro_core1_ready_ptr();
        while (!*ready) tight_loop_contents();
        printf("I2S audio started (44100 Hz)\n");
    }
#endif

    /* SD card.  Mounted after video is up so a mount failure can be shown
     * on-screen with the animated error screen (same as frank-cpc). */
    static FATFS g_fs;
    FRESULT fr = f_mount(&g_fs, "", 1);
    if (fr != FR_OK) {
        printf("ERROR: SD card mount failed (%d)\n", (int)fr);
        micro_boot_error(false, fr);   /* never returns */
    }
    printf("SD card mounted\n");

    /* Welcome screen */
    micro_boot_welcome(3000);

    /* Enter beebjit (blocks until BBC exits) */
    beebjit_main();

    while (true) tight_loop_contents();
    return 0;
}
