/*
 * frank-micro — BBC Micro for RP2350
 * micro_settings.h — Runtime settings.
 */
#ifndef MICRO_SETTINGS_H
#define MICRO_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#define MICRO_INI_PATH "/micro/micro.ini"

typedef enum {
    MICRO_MODEL_B = 0,
    MICRO_MODEL_MASTER_128,
    MICRO_MODEL_MASTER_COMPACT,
    MICRO_MODEL_COUNT
} micro_model_t;

typedef enum {
    MICRO_AUDIO_I2S = 0,
    MICRO_AUDIO_PWM,
    MICRO_AUDIO_HDMI,
    MICRO_AUDIO_COUNT
} micro_audio_driver_t;

typedef struct {
    uint8_t  model;         /* micro_model_t */
    uint8_t  audio_driver;  /* micro_audio_driver_t */
    uint8_t  volume;        /* 0-100 */
    uint8_t  limit_speed;   /* 0=fast, 1=50Hz */
    char     disk_a[128];   /* auto-mount path for drive A */
    char     disk_b[128];   /* auto-mount path for drive B */
} micro_settings_t;

extern micro_settings_t g_micro_settings;

void micro_settings_load(void);
void micro_settings_save(void);
void micro_settings_defaults(void);

#endif /* MICRO_SETTINGS_H */
