/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include <string.h>
#include <stdio.h>
#include "pico.h"
#include "xip_stream.h"

#if PICO_ON_DEVICE
#include "hardware/dma.h"
#include "hardware/structs/xip_ctrl.h"
#endif

//#define xip_stream_assert(x) hard_assert(x)
#if 0
#define xip_stream_assert(x) ({if (!(x)) panic("%d", __LINE__);})
#else
#define xip_stream_assert(x) assert(x)
#endif

struct xip_stream_dma *xip_stream_head;
static struct xip_stream_dma *_tail;

#define XIP_STREAM_DMA_CHANNEL 7

void xip_stream_init() {
#if PICO_ON_DEVICE
    xip_ctrl_hw->stream_ctr = 0;
    //mm_xip_ctrl->stream_addr = (uintptr_t)flash_ptr;
    dma_channel_config c = dma_channel_get_default_config(XIP_STREAM_DMA_CHANNEL);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_XIP_STREAM);
    dma_channel_set_read_addr(XIP_STREAM_DMA_CHANNEL, (void *)XIP_AUX_BASE, false);
    dma_channel_set_config(XIP_STREAM_DMA_CHANNEL, &c, false);
#endif
}

void xip_stream_dma_start(struct xip_stream_dma *dma) {
    xip_stream_assert(dma->state == NONE);
//    printf("xip stream start %p\n", dma);
    dma->state = WAITING;
    dma->completed = 0;
    xip_stream_assert(dma != xip_stream_head);
    xip_stream_assert(dma != _tail);
    if (xip_stream_head) {
        _tail->next = dma;
    } else {
        xip_stream_head = dma;
    }
    _tail = dma;
    xip_stream_assert(dma->next == NULL);
    xip_stream_dma_poll();
}

void xip_stream_dma_cancel(struct xip_stream_dma *dma) {
//    printf("xip stream cancel %p\n", dma);
    if (dma->state) {
        if (dma == xip_stream_head) {
            xip_stream_head = dma->next;
            if (dma == _tail) _tail = NULL;
#if PICO_ON_DEVICE
            dma_channel_abort(XIP_STREAM_DMA_CHANNEL);
            while (dma_channel_is_busy(XIP_STREAM_DMA_CHANNEL));
            xip_ctrl_hw->stream_addr = 0;
//            dma_abort(XIP_STREAM_DMA_CHANNEL);
            while (!(xip_ctrl_hw->stat & XIP_STAT_FIFO_EMPTY)) {
                xip_ctrl_hw->stream_fifo;
            }
#endif
        } else {
            for (struct xip_stream_dma *prev = xip_stream_head; prev; prev = prev->next) {
                if (prev->next == dma) {
                    prev->next = dma->next;
                    if (dma == _tail) {
                        _tail = prev;
                    }
                    break;
                }
            }
        }
        dma->next = NULL;
        dma->state = NONE;
    } else {
        xip_stream_assert(!dma->next);
    }
}

void xip_stream_dma_poll() {
    bool done = false;

    do {
        if (xip_stream_head) {
//            printf("xip stream poll %p %d\n", xip_stream_head, xip_stream_head->state);
            switch (xip_stream_head->state) {
                case WAITING:
#if PICO_ON_DEVICE
                    xip_ctrl_hw->stream_addr = (uintptr_t)xip_stream_head->src;
                    xip_ctrl_hw->stream_ctr = xip_stream_head->transfer_size;
//                    printf("%p XFER start %p<-%p %d\n", xip_stream_head, xip_stream_head->dest, xip_stream_head->src, (int)xip_stream_head->transfer_size);
                    dma_channel_transfer_to_buffer_now(XIP_STREAM_DMA_CHANNEL, xip_stream_head->dest, xip_stream_head->transfer_size);
#else
                    memcpy(xip_stream_head->dest, xip_stream_head->src, xip_stream_head->transfer_size * 4);
#endif
                    xip_stream_head->state = STARTED;
                    break;
                case STARTED: {
#if PICO_ON_DEVICE
                    bool __unused busy = dma_channel_is_busy(XIP_STREAM_DMA_CHANNEL);
                    uint words = ((uintptr_t)dma_hw->ch[XIP_STREAM_DMA_CHANNEL].al1_write_addr - (uintptr_t)xip_stream_head->dest) / 4;
//                    printf("%p XFER completed %d/%d busy %d\n", xip_stream_head, words, (int)xip_stream_head->transfer_size, busy);
                    xip_stream_assert(busy || words == xip_stream_head->transfer_size); // could have errored out.
#else
                    uint words = xip_stream_head->transfer_size;
#endif
                    xip_stream_head->completed = words;
                    if (xip_stream_head->completed == xip_stream_head->transfer_size) {
                        xip_stream_head->state = COMPLETE;
                        struct xip_stream_dma *next = xip_stream_head->next;
                        xip_stream_head->next = 0;
                        if (!next) {
                            xip_stream_assert(_tail == xip_stream_head);
                            _tail = NULL;
                        }
                        xip_stream_head = next;
                    } else {
                        done = true;
                    }
                    break;
                }
                default:
                    panic("%p state %d\n", xip_stream_head, xip_stream_head->state);
                            hard_assert(false);
            }
        } else {
            done = true;
        }
    } while (!done);

}
