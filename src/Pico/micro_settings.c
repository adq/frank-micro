/*
 * frank-micro — BBC Micro for RP2350
 * micro_settings.c — Load/save INI settings from SD card.
 */
#include "micro_settings.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

micro_settings_t g_micro_settings;

void micro_settings_defaults(void) {
    memset(&g_micro_settings, 0, sizeof(g_micro_settings));
    g_micro_settings.model        = MICRO_MODEL_B;
    g_micro_settings.audio_driver = MICRO_AUDIO_HDMI;
    g_micro_settings.volume       = 80;
    g_micro_settings.limit_speed  = 1;
}

static void parse_line(char* line) {
    /* Trim */
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == ';' || *p == '\0') return;

    char* eq = strchr(p, '=');
    if (!eq) return;
    *eq = '\0';
    char* key = p;
    char* val = eq + 1;

    /* Trim key */
    char* e = key + strlen(key) - 1;
    while (e > key && (*e == ' ' || *e == '\t')) *e-- = '\0';

    /* Trim val */
    while (*val == ' ' || *val == '\t') val++;
    e = val + strlen(val) - 1;
    while (e > val && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = '\0';

    if (strcasecmp(key, "model") == 0) {
        if (strcasecmp(val, "master128") == 0)   g_micro_settings.model = MICRO_MODEL_MASTER_128;
        else if (strcasecmp(val, "compact") == 0) g_micro_settings.model = MICRO_MODEL_MASTER_COMPACT;
        else                                       g_micro_settings.model = MICRO_MODEL_B;
    } else if (strcasecmp(key, "audio") == 0) {
        if (strcasecmp(val, "pwm") == 0)          g_micro_settings.audio_driver = MICRO_AUDIO_PWM;
        else if (strcasecmp(val, "i2s") == 0)     g_micro_settings.audio_driver = MICRO_AUDIO_I2S;
        else                                       g_micro_settings.audio_driver = MICRO_AUDIO_HDMI;
    } else if (strcasecmp(key, "volume") == 0) {
        g_micro_settings.volume = (uint8_t)atoi(val);
    } else if (strcasecmp(key, "limit_speed") == 0) {
        g_micro_settings.limit_speed = (uint8_t)atoi(val);
    } else if (strcasecmp(key, "disk_a") == 0) {
        if (val[0] != '/') snprintf(g_micro_settings.disk_a, sizeof(g_micro_settings.disk_a), "/micro/disk/%s", val);
        else strncpy(g_micro_settings.disk_a, val, sizeof(g_micro_settings.disk_a) - 1);
    } else if (strcasecmp(key, "disk_b") == 0) {
        if (val[0] != '/') snprintf(g_micro_settings.disk_b, sizeof(g_micro_settings.disk_b), "/micro/disk/%s", val);
        else strncpy(g_micro_settings.disk_b, val, sizeof(g_micro_settings.disk_b) - 1);
    }
}

void micro_settings_load(void) {
    micro_settings_defaults();
    FIL f;
    if (f_open(&f, MICRO_INI_PATH, FA_READ) != FR_OK) return;

    char line[256];
    UINT br;
    char buf[256];
    int pos = 0;

    while (f_read(&f, buf + pos, 1, &br) == FR_OK && br == 1) {
        char c = buf[pos];
        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            if (pos > 0) parse_line(buf);
            pos = 0;
        } else {
            pos++;
            if (pos >= (int)sizeof(buf) - 1) pos = 0;
        }
    }
    if (pos > 0) { buf[pos] = '\0'; parse_line(buf); }
    f_close(&f);
}

void micro_settings_save(void) {
    FIL f;
    char buf[512];
    if (f_open(&f, MICRO_INI_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return;

    static const char* model_str[] = { "b", "master128", "compact" };
    static const char* audio_str[] = { "i2s", "pwm", "hdmi" };

    int len = snprintf(buf, sizeof(buf),
        "model=%s\naudio=%s\nvolume=%d\nlimit_speed=%d\n",
        model_str[g_micro_settings.model % MICRO_MODEL_COUNT],
        audio_str[g_micro_settings.audio_driver % MICRO_AUDIO_COUNT],
        (int)g_micro_settings.volume,
        (int)g_micro_settings.limit_speed);

    if (g_micro_settings.disk_a[0])
        len += snprintf(buf + len, sizeof(buf) - len, "disk_a=%s\n", g_micro_settings.disk_a);
    if (g_micro_settings.disk_b[0])
        len += snprintf(buf + len, sizeof(buf) - len, "disk_b=%s\n", g_micro_settings.disk_b);

    UINT bw;
    f_write(&f, buf, (UINT)len, &bw);
    f_close(&f);
}
