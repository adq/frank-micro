/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_XIP_STREAM_H
#define B_EM_PICO_XIP_STREAM_H

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xip_stream_dma {
    struct xip_stream_dma *next;
    const uint32_t *src;
    uint32_t *dest;
    uint32_t transfer_size;
    uint32_t completed;
    enum {
        NONE = 0,
        WAITING = 1,
        STARTED = 2,
        COMPLETE = 3,
    } state;
};

void xip_stream_init();

void xip_stream_dma_start(struct xip_stream_dma *dma);
void xip_stream_dma_cancel(struct xip_stream_dma *dma);

// todo revisit this if we allow IRQ behavior - this is still necessary internally
void xip_stream_dma_poll();

static inline uint xip_stream_dma_available_words(struct xip_stream_dma *dma) {
    extern struct xip_stream_dma *xip_stream_head;
    assert(dma->state);
    if (xip_stream_head) xip_stream_dma_poll();
    return dma->completed;
}

#ifdef __cplusplus
}
#endif
#endif
