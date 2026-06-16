/*
 * frank-cpc — Amstrad CPC for RP2350
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-cpc
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * frank-cpc - PWM Audio Driver
 * DMA-paced double-buffered PWM playback via DREQ from the audio PWM slice.
 * Ported from frank-msx.
 * SPDX-License-Identifier: MIT
 */

#ifndef PWM_AUDIO_H
#define PWM_AUDIO_H

#include <stdint.h>
#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the PWM audio path.
 *   pin_l, pin_r: GPIOs wired to the audio PWM slice (may be the same slice
 *                 or two different slices; if they share a slice the single
 *                 DMA stream drives both, otherwise pin_r is parked at a
 *                 static mid-level PWM).
 *   sample_rate:  output sample rate in Hz (e.g. 22050).
 * Safe to call multiple times — subsequent calls are no-ops. */
void pwm_audio_init(uint pin_l, uint pin_r, uint32_t sample_rate);

/* Push signed 16-bit mono samples. Non-blocking as long as a DMA buffer is
 * free; drops samples if both buffers are still playing. */
void pwm_audio_push_samples(const int16_t *buf, int count);

/* Like pwm_audio_push_samples() but waits for a free DMA buffer instead of
 * dropping samples. This self-paces the producer to the PWM output clock
 * (mirroring the I2S driver), eliminating the dropouts/crackle that the
 * non-blocking path produces under frame-timing jitter. For glitch-free
 * output, push exactly pwm_audio chunk-size (see pwm_audio_set_chunk_frames)
 * samples per call so each buffer is filled completely with no silence pad. */
void pwm_audio_push_samples_blocking(const int16_t *buf, int count);

/* Set the exact per-buffer DMA transfer size (in mono frames). Use this when
 * the producer delivers a fixed block size that does not divide evenly by the
 * frame rate, so every committed buffer is full and never silence-padded.
 * Clamped to the internal DMA buffer capacity. */
void pwm_audio_set_chunk_frames(uint32_t frames);

/* Drop `count` samples' worth of silence at the configured sample rate. */
void pwm_audio_fill_silence(int count);

/* Resize the per-DMA-chunk transfer count to match the emulation frame rate.
 * frame_rate = 60 for NTSC, 50 for PAL. */
void pwm_audio_set_frame_rate(int frame_rate);

#ifdef __cplusplus
}
#endif

#endif /* PWM_AUDIO_H */
