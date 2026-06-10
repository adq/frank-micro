/*
 * frank-micro — BBC Micro for RP2350
 * micro_serial_console.c — USB serial command console.
 *
 * Protocol (line-based, \r or \n terminated):
 *   PING                   → OK PONG
 *   RESET                  → OK (reboot)
 *   DISK A|B INSERT <path> → OK / ERR
 *   DISK A|B EJECT         → OK
 *   DISK A|B STATUS        → OK INSERTED <name> / OK EMPTY
 *   CAT [path]             → OK <listing>
 *   STATUS                 → OK ALIVE model=<m>
 *   HELP                   → OK ...
 */
#include "micro_serial_console.h"
#include "micro_loader.h"
#include "micro_settings.h"
#include "micro_keys.h"
#include "ff.h"
#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define CMD_BUF_SIZE 256

static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_pos = 0;

static const char* match_prefix(const char* s, const char* prefix) {
    while (*prefix) {
        if (toupper((unsigned char)*s) != toupper((unsigned char)*prefix)) return NULL;
        s++; prefix++;
    }
    return s;
}

static void cmd_cat(const char* path) {
    DIR dir; FILINFO fi;
    const char* p = (path && *path) ? path : g_micro_disk_dir;
    if (f_opendir(&dir, p) != FR_OK) { printf("ERR cannot open %s\n", p); return; }
    printf("OK DIR=%s\n", p);
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        printf("# %s%s\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "");
    }
    f_closedir(&dir);
    printf("# END\n");
}

static void process_command(char* cmd) {
    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;

    const char* r;

    if ((r = match_prefix(cmd, "PING"))) {
        printf("OK PONG\n");
    } else if ((r = match_prefix(cmd, "RESET"))) {
        printf("OK RESETTING\n");
        sleep_ms(100);
        watchdog_reboot(0, 0, 0);
    } else if ((r = match_prefix(cmd, "STATUS"))) {
        printf("OK ALIVE model=%d\n", (int)g_micro_settings.model);
    } else if ((r = match_prefix(cmd, "DISK "))) {
        int drive = -1;
        if (toupper((unsigned char)*r) == 'A') { drive = 0; r++; }
        else if (toupper((unsigned char)*r) == 'B') { drive = 1; r++; }
        else { printf("ERR unknown drive\n"); return; }
        while (*r == ' ') r++;
        const char* q;
        if ((q = match_prefix(r, "INSERT "))) {
            while (*q == ' ') q++;
            if (micro_mount_disk(drive, q) == 0) printf("OK drive %c = %s\n", 'A'+drive, q);
            else printf("ERR cannot mount %s\n", q);
        } else if ((q = match_prefix(r, "EJECT"))) {
            micro_eject_disk(drive);
            printf("OK\n");
        } else if ((q = match_prefix(r, "STATUS"))) {
            const char* n = micro_mounted_disk_name(drive);
            if (n) printf("OK INSERTED %s\n", n);
            else   printf("OK EMPTY\n");
        } else {
            printf("ERR unknown DISK sub-command\n");
        }
    } else if ((r = match_prefix(cmd, "CAT"))) {
        while (*r == ' ') r++;
        cmd_cat(*r ? r : NULL);
    } else if ((r = match_prefix(cmd, "TYPE "))) {
        /* Inject ASCII text as keystrokes (does NOT auto-press RETURN). */
        int n = micro_key_type(r);
        printf("OK TYPED %d\n", n);
    } else if ((r = match_prefix(cmd, "RUN "))) {
        /* Type text followed by RETURN. */
        int n = micro_key_type(r);
        micro_key_return();
        printf("OK RAN %d\n", n);
    } else if ((r = match_prefix(cmd, "KEY "))) {
        while (*r == ' ') r++;
        if (match_prefix(r, "RETURN") || match_prefix(r, "ENTER")) {
            micro_key_return(); printf("OK\n");
        } else if (match_prefix(r, "SHIFTBREAK") || match_prefix(r, "BOOT")) {
            micro_key_break(1); printf("OK SHIFT+BREAK\n");
        } else if (match_prefix(r, "BREAK")) {
            micro_key_break(0); printf("OK BREAK\n");
        } else if (match_prefix(r, "SPACE")) {
            micro_key_enqueue(' ', 0, 3); printf("OK\n");
        } else if (match_prefix(r, "ESCAPE") || match_prefix(r, "ESC")) {
            micro_key_enqueue(128 /*k_keyboard_key_escape*/, 0, 3); printf("OK\n");
        } else {
            printf("ERR unknown KEY\n");
        }
    } else if ((r = match_prefix(cmd, "BOOT"))) {
        /* SHIFT+BREAK to boot the mounted disk. */
        micro_key_break(1);
        printf("OK BOOTING\n");
    } else if ((r = match_prefix(cmd, "BREAK"))) {
        micro_key_break(0);
        printf("OK BREAK\n");
    } else if ((r = match_prefix(cmd, "RESETCFG"))) {
        /* Restore default settings (Model B) and reboot. */
        micro_settings_defaults();
        micro_settings_save();
        printf("OK defaults restored, rebooting\n");
        sleep_ms(150);
        watchdog_reboot(0, 0, 0);
    } else if ((r = match_prefix(cmd, "MODEL"))) {
        while (*r == ' ') r++;
        if (*r == '\0') {
            static const char* names[] = { "B", "Master128", "Compact" };
            printf("OK MODEL %s\n", names[g_micro_settings.model % MICRO_MODEL_COUNT]);
        } else {
            int m = -1;
            if (match_prefix(r, "MASTER128") || match_prefix(r, "MASTER") ||
                match_prefix(r, "M128"))               m = MICRO_MODEL_MASTER_128;
            else if (match_prefix(r, "COMPACT"))       m = MICRO_MODEL_MASTER_COMPACT;
            else if (match_prefix(r, "B") || match_prefix(r, "MODELB"))
                                                       m = MICRO_MODEL_B;
            if (m < 0) { printf("ERR unknown model\n"); return; }
            g_micro_settings.model = (uint8_t)m;
            micro_settings_save();
            /* Clear crash sentinel so the new model gets a clean first boot. */
            volatile uint32_t* p_scratch = (volatile uint32_t*)(0x400d800cu);
            *p_scratch = 0;
            printf("OK MODEL set, rebooting\n");
            sleep_ms(150);
            watchdog_reboot(0, 0, 0);
        }
    } else if ((r = match_prefix(cmd, "AUTODISK "))) {
        /* Persist a disc to auto-mount on next boot (drive A). */
        while (*r == ' ') r++;
        strncpy(g_micro_settings.disk_a, r, sizeof(g_micro_settings.disk_a) - 1);
        g_micro_settings.disk_a[sizeof(g_micro_settings.disk_a) - 1] = '\0';
        micro_settings_save();
        printf("OK AUTODISK A=%s\n", g_micro_settings.disk_a);
    } else if ((r = match_prefix(cmd, "CD "))) {
        while (*r == ' ') r++;
        strncpy(g_micro_disk_dir, r, MICRO_DISK_PATH_LEN - 1);
        micro_disk_rescan();
        printf("OK DIR=%s ENTRIES=%d\n", g_micro_disk_dir, g_micro_disk_entry_count);
    } else if ((r = match_prefix(cmd, "HELP"))) {
        printf("OK Commands: PING RESET STATUS DISK A|B INSERT|EJECT|STATUS "
               "CAT CD TYPE RUN KEY BOOT BREAK MODEL AUTODISK HELP\n");
    } else {
        printf("ERR unknown command: %s\n", cmd);
    }
}

void micro_serial_poll(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (cmd_pos > 0) {
                cmd_buf[cmd_pos] = '\0';
                process_command(cmd_buf);
                cmd_pos = 0;
            }
        } else if (c == '\b' || c == 127) {
            if (cmd_pos > 0) cmd_pos--;
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = (char)c;
        }
    }
}
