/*
 * Vendored into frank-micro from pico_shared, drivers/pico_hdmi.
 *
 *   https://github.com/PicoPlus-devel/pico_shared
 *   (formerly fhoedemakers/pico_shared, maintained by github.com/fhoedemakers)
 *
 * Taken by way of the vendored copy inside fhoedemakers/fruitjam-doom, which
 * is where it was found running on Fruit Jam hardware.
 *
 * Upstream is licensed GPL-3.0. Its LICENSE is the plain GPLv3 text with no
 * copyright holder filled in, and no upstream source file carries a per-file
 * notice, so none is reproduced here and nothing below claims or reassigns
 * copyright in the vendored code.
 *
 * Modified for frank-micro. The changes are listed in video_output.h.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef HSTX_DATA_ISLAND_QUEUE_H
#define HSTX_DATA_ISLAND_QUEUE_H

#include "hstx_packet.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Initialize the Data Island queue and scheduler.
 */
void hstx_di_queue_init(void);

/**
 * Set the audio sample rate for packet timing.
 * @param sample_rate Audio sample rate in Hz (e.g. 44100, 48000)
 */
void hstx_di_queue_set_sample_rate(uint32_t sample_rate);

/**
 * Push a pre-encoded Data Island into the queue.
 * Returns true if successful, false if the queue is full.
 */
bool hstx_di_queue_push(const hstx_data_island_t *island);

/**
 * Get the current number of items in the queue.
 */
uint32_t hstx_di_queue_get_level(void);

/**
 * Advance the Data Island scheduler by one scanline.
 * Must be called exactly once per scanline in the DMA ISR.
 */
void hstx_di_queue_tick(void);

/**
 * Get the next audio Data Island packet if the scheduler determines it's time.
 *
 * @return Pointer to 36-word HSTX data island, or NULL if no packet is due.
 */
const uint32_t *hstx_di_queue_get_audio_packet(void);

/**
 * Number of silence-packet fallbacks since boot (a packet was due but the
 * queue was empty). Monotonic; diff between reads to detect underruns.
 */
uint32_t hstx_di_queue_get_underrun_count(void);

#endif // HSTX_DATA_ISLAND_QUEUE_H
