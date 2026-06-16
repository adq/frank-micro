/*
 * frank-micro — BBC Micro for RP2350 (b-em port)
 * frank_audio.c — bridges b-em's audio buffer pool (sn76489 output) to the
 *                 frank HDMI audio ring.
 *
 * (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * b-em fills a mono int16 buffer (sound_poll_n) and hands it back via
 * give_audio_buffer().  We up-mix to stereo and push it into the HDMI audio
 * ring; the encoder consumes it on core1.  Never blocks the producer.
 */
#include <stdint.h>
#include <string.h>
#include "pico.h"
#include "pico/util/buffer.h"
#include "x_gui.h"

#if defined(HDMI_PIO_AUDIO)
#include "frank_hdmi.h"
#endif

#include <stdbool.h>

/* Live volume (0..100) and mute, driven by the settings UI (frank_settings.c). */
extern volatile int  g_frank_volume;
extern volatile bool g_frank_sound_on;

bool x_gui_audio_init_failed;

/* A small mono sample buffer reused each fill (b-em uses one in flight). */
#define FRANK_AUDIO_SAMPLES 882   /* 1 frame @ ~44.1kHz / 50Hz */
static int16_t s_mono[FRANK_AUDIO_SAMPLES];
static mem_buffer_t s_mem = {
    .size = sizeof(s_mono),
    .bytes = (uint8_t *)s_mono,
    .flags = 0,
};
static struct audio_format s_fmt;
static struct audio_buffer_format s_bfmt = { .format = &s_fmt, .sample_stride = 2 };
static struct audio_buffer s_buf = {
    .buffer = &s_mem,
    .format = &s_bfmt,
    .max_sample_count = FRANK_AUDIO_SAMPLES,
};
static struct audio_buffer_pool s_pool;

struct audio_buffer_pool *x_gui_audio_init(uint freq) {
    s_fmt.sample_freq = freq;
    s_fmt.format = AUDIO_BUFFER_FORMAT_PCM_S16;
    s_fmt.channel_count = 1;
    x_gui_audio_init_failed = false;
    return &s_pool;
}

struct audio_buffer *take_audio_buffer(struct audio_buffer_pool *ac, bool block) {
    s_buf.sample_count = 0;
    return &s_buf;
}

/*
 * Push b-em's audio into the HDMI ring, resampled from the BBC's native
 * 31250 Hz (the sn76489 output rate, FREQ_SO) to the HDMI ring's standard
 * 32000 Hz, and up-mixed mono -> stereo.
 *
 * The HDMI audio data-island stream advertises a STANDARD CEA-861 rate
 * (32000 Hz) so real HDMI sinks lock their audio clock to our Clock-
 * Regeneration packet.  A non-standard rate (31250 Hz) is mishandled by
 * many sinks, which then drop a sample every few seconds.
 *
 * The ratio 31250/32000 = 0.9765625 = 64000/65536 exactly, so the linear
 * resampler below is exact fixed-point: one output sample advances the input
 * phase by 64000 in 16.16, emitting 1.024 output samples per input sample.
 */
#define RESAMP_STEP   64000u   /* (31250/32000) << 16, exact */
#define RESAMP_ONE    0x10000u

void give_audio_buffer(struct audio_buffer_pool *ac, struct audio_buffer *buffer) {
#if defined(HDMI_PIO_AUDIO)
    const int16_t *src = (const int16_t *)buffer->buffer->bytes;
    uint32_t n = buffer->sample_count;

    static uint32_t mu   = 0;   /* 16.16 phase between prev and cur input  */
    static int16_t  prev = 0;   /* previous input sample (persists)        */
    static int16_t  stereo[256];
    uint32_t sc = 0;            /* stereo frames buffered for flush         */

    int vol = g_frank_sound_on ? g_frank_volume : 0;
    if (vol > 100) vol = 100;

    for (uint32_t i = 0; i < n; i++) {
        int16_t cur = (int16_t)(((int32_t)src[i] * vol) / 100);
        while (mu < RESAMP_ONE) {
            int32_t out = prev + (((int32_t)(cur - prev) * (int32_t)mu) >> 16);
            stereo[sc * 2]     = (int16_t)out;
            stereo[sc * 2 + 1] = (int16_t)out;
            if (++sc == 128) { frank_hdmi_audio_write(stereo, 128); sc = 0; }
            mu += RESAMP_STEP;
        }
        mu -= RESAMP_ONE;
        prev = cur;
    }
    if (sc) frank_hdmi_audio_write(stereo, sc);
#else
    (void)buffer;
#endif
}

/* ── xip_stream stub ──────────────────────────────────────────────────────
 * b-em's audio.c streams flash-resident PCM samples (disc/ddnoise) via the
 * sector_read xip_stream DMA helper.  The BBC's BASIC beep uses sn76489 which
 * does NOT need this, so we stub the streamer for milestone 1.
 */
#include "xip_stream.h"
struct xip_stream_dma *xip_stream_head;
void xip_stream_init(void) {}
void xip_stream_dma_start(struct xip_stream_dma *dma) { if (dma) dma->state = COMPLETE; }
void xip_stream_dma_cancel(struct xip_stream_dma *dma) { if (dma) dma->state = NONE; }
void xip_stream_dma_poll(void) {}
