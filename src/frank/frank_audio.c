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

void give_audio_buffer(struct audio_buffer_pool *ac, struct audio_buffer *buffer) {
#if defined(HDMI_PIO_AUDIO)
    const int16_t *src = (const int16_t *)buffer->buffer->bytes;
    uint32_t n = buffer->sample_count;
    /* Up-mix mono -> interleaved stereo in small chunks. */
    static int16_t stereo[256];
    uint32_t i = 0;
    while (i < n) {
        uint32_t chunk = n - i;
        if (chunk > 128) chunk = 128;
        for (uint32_t j = 0; j < chunk; j++) {
            int16_t s = src[i + j];
            stereo[j * 2]     = s;
            stereo[j * 2 + 1] = s;
        }
        frank_hdmi_audio_write(stereo, chunk);
        i += chunk;
    }
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
