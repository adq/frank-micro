/*
 * frank-micro — BBC Micro for RP2350
 * os_sound_pico.c — BBC sound output via I2S ring buffer.
 *
 * beebjit's sound.c generates SN76489 audio at ~44100 Hz and calls
 * os_sound_write() with a buffer of int16_t samples.  We push those
 * into the shared I2S ring buffer that Core 1 drains to the DAC.
 */
#include "os_sound.h"
#include <stdlib.h>
#include <stdint.h>

/* Ring buffer - filled here, drained by Core 1 in main.c */
extern unsigned i2s_ring_push(const int16_t* samples, unsigned count);
extern unsigned i2s_ring_free(void);

#define SOUND_SAMPLE_RATE    44100
#define SOUND_BUFFER_SIZE    882    /* 44100 / 50 Hz */
#define SOUND_NUM_PERIODS    4

struct os_sound_struct {
    uint32_t sample_rate;
    uint32_t buffer_size;
    uint32_t period_size;
};

uint32_t os_sound_get_default_sample_rate(void)  { return SOUND_SAMPLE_RATE; }
uint32_t os_sound_get_default_buffer_size(void)  { return SOUND_BUFFER_SIZE * SOUND_NUM_PERIODS; }
uint32_t os_sound_get_default_num_periods(void)  { return SOUND_NUM_PERIODS; }

struct os_sound_struct* os_sound_create(char* p_device_name,
                                        uint32_t sample_rate,
                                        uint32_t buffer_size,
                                        uint32_t num_periods) {
    (void)p_device_name;
    struct os_sound_struct* p = malloc(sizeof(*p));
    if (!p) return NULL;
    p->sample_rate = sample_rate  ? sample_rate  : SOUND_SAMPLE_RATE;
    p->buffer_size = buffer_size  ? buffer_size  : (SOUND_BUFFER_SIZE * SOUND_NUM_PERIODS);
    p->period_size = num_periods  ? (buffer_size / num_periods) : SOUND_BUFFER_SIZE;
    return p;
}

void os_sound_destroy(struct os_sound_struct* p_driver) {
    free(p_driver);
}

int os_sound_init(struct os_sound_struct* p_driver) {
    (void)p_driver;
    return 0; /* Always succeeds */
}

uint32_t os_sound_get_sample_rate(struct os_sound_struct* p_driver) {
    return p_driver->sample_rate;
}

uint32_t os_sound_get_buffer_size(struct os_sound_struct* p_driver) {
    return p_driver->buffer_size;
}

uint32_t os_sound_get_period_size(struct os_sound_struct* p_driver) {
    return p_driver->period_size;
}

void os_sound_write(struct os_sound_struct* p_driver,
                    int16_t* p_frames,
                    uint32_t num_frames) {
    (void)p_driver;
    /* Push mono samples to ring; ring_push handles overflow. */
    i2s_ring_push(p_frames, num_frames);
}
