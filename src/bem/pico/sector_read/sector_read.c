/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sector_read.h"

#ifdef ENABLE_COMPRESSED_SECTOR_READ
#include "miniz_tinfl.h"
#endif

#include "hardware/gpio.h"
#include "xip_stream.h"

CU_REGISTER_DEBUG_PINS(xip_sector_read_dma)

#if 0
#define sr_assert hard_assert
#else
#define sr_assert assert
#endif

// ---- select at most one ---
//CU_SELECT_DEBUG_PINS(xip_sector_read_dma)

struct memory_sector_read {
    struct sector_read sector_read;
    const uint8_t *data;
    bool data_owned;
};

static inline struct memory_sector_read *to_ms(struct sector_read *s) {
            sr_assert(s);
            sr_assert(s->funcs->type == memory);
    return (struct memory_sector_read *) s;
}

#define SINGLE_BUFFER

#ifdef SINGLE_BUFFER
static struct sector_buffer _single_buffer;
#endif

// todo pool
static struct sector_buffer *sector_buffer_alloc() {
#ifdef SINGLE_BUFFER
            sr_assert(!_single_buffer.in_use);
    _single_buffer.in_use = true;
    return &_single_buffer;
#else
    struct sector_buffer *b = calloc(1, sizeof(struct sector_buffer));;
    b->in_use = 1;
    return b;
#endif
}

static void sector_buffer_release(struct sector_buffer *buffer) {
            sr_assert(buffer->in_use);
#ifdef SINGLE_BUFFER
            sr_assert(buffer == &_single_buffer);
    buffer->in_use--;
#else
    if (!--buffer->in_use)
        free(buffer);
#endif
}

struct sector_buffer *memory_sector_read_acquire_buffer(struct sector_read *sr, uint32_t sector) {
    if (sector >= sr->sector_count) {
        return NULL;
    }
    struct sector_buffer *b = sector_buffer_alloc();
    b->buffer.size = sr->sector_size;
    struct memory_sector_read *ms = to_ms(sr);
//    printf("acquire sector %d\n", sector);
    b->buffer.bytes = (uint8_t *) (ms->data + sector * sr->sector_size); // annoying cast
    return b;
}

uint memory_sector_read_check_available(struct sector_read *sr, struct sector_buffer *buffer, uint wanted,
                                        uint32_t timeout) {
    return buffer->buffer.size;
}

void memory_sector_read_release_buffer(struct sector_read *sr, struct sector_buffer *buffer) {
    sector_buffer_release(buffer);
}

void memory_sector_read_close(struct sector_read *s) {
    struct memory_sector_read *ms = to_ms(s);
    if (ms->data_owned) free((void *) ms->data);
    free(ms);
}

const struct sector_read_funcs memory_sector_read_funcs = {
        .acquire_buffer = memory_sector_read_acquire_buffer,
        .release_buffer = memory_sector_read_release_buffer,
        .check_available = memory_sector_read_check_available,
        .close = memory_sector_read_close,
#ifndef NDEBUG
        .type = memory
#endif
};

struct sector_read *memory_sector_read_open(const uint8_t *buffer, size_t sector_size, size_t sector_count,
                                            bool own_buffer) {
//    printf("Open %p %dx%d\n", buffer, (int)sector_size, (int)sector_count);
    struct memory_sector_read *ms = (struct memory_sector_read *) calloc(1, sizeof(struct memory_sector_read));
    ms->sector_read.funcs = &memory_sector_read_funcs;
    ms->sector_read.sector_size = sector_size;
    ms->sector_read.sector_count = sector_count;
    ms->data = buffer;
    ms->data_owned = own_buffer;
    return &ms->sector_read;
}

struct xip_sector_read {
    struct sector_read sector_read;
    struct xip_stream_dma dma;
    const uint8_t *flash_ptr; // word alignewd
    uint8_t *buffer; // word alignewd
};

static inline struct xip_sector_read *to_xs(struct sector_read *s) {
            sr_assert(s);
            sr_assert(s->funcs->type == xip);
    return (struct xip_sector_read *) s;
}

static void xip_sector_read_start_dma(struct xip_sector_read *xs, const uint32_t *src, uint32_t *dest, uint32_t words) {
            sr_assert(!xs->dma.state);
    xs->dma.src = src;
    xs->dma.dest = dest;
    xs->dma.transfer_size = words;
    xip_stream_dma_start(&xs->dma);
}

static void xip_sector_read_cancel_dma(struct xip_sector_read *xs) {
    xip_stream_dma_cancel(&xs->dma);
}

void xip_sector_read_close(struct sector_read *s) {
    struct xip_sector_read *xs = to_xs(s);
    xip_sector_read_cancel_dma(xs);
    free(xs->buffer);
    free(xs);
}

struct sector_buffer *xip_sector_read_acquire_buffer(struct sector_read *sr, uint32_t sector) {
    if (sector >= sr->sector_count) {
        return NULL;
    }
    struct sector_buffer *b = sector_buffer_alloc();
    b->buffer.size = sr->sector_size;
    struct xip_sector_read *xs = to_xs(sr);
    const uint8_t *flash_base = xs->flash_ptr + sector * xs->sector_read.sector_size;
    if (xs->dma.state == NONE) {
                sr_assert(!(3u & xs->sector_read.sector_size));
        xip_sector_read_start_dma(xs, (uint32_t *) flash_base, (uint32_t *) xs->buffer,
                                  xs->sector_read.sector_size / 4);
    }
    b->buffer.bytes = xs->buffer;
    return b;
}

void xip_sector_read_release_buffer(struct sector_read *sr, struct sector_buffer *buffer) {
    struct xip_sector_read *xs = to_xs(sr);
    xip_sector_read_cancel_dma(xs);
    sector_buffer_release(buffer);
}

uint xip_sector_read_check_available(struct sector_read *sr, struct sector_buffer *buffer, uint wanted,
                                     uint32_t timeout) {
#ifdef SINGLE_BUFFER
            sr_assert(buffer == &_single_buffer);
#else
#error todo
#endif
    struct xip_sector_read *xs = to_xs(sr);
            sr_assert(xs->dma.state);
    uint bytes = xip_stream_dma_available_words(&xs->dma) * 4;
            sr_assert(bytes <= xs->sector_read.sector_size);
    return bytes;
}

const struct sector_read_funcs xip_sector_read_funcs = {
        .acquire_buffer = xip_sector_read_acquire_buffer,
        .release_buffer = xip_sector_read_release_buffer,
        .check_available = xip_sector_read_check_available,
        .close = xip_sector_read_close,
#ifndef NDEBUG
        .type = xip
#endif
};

struct sector_read *xip_sector_read_open(const uint8_t *flash_ptr, size_t sector_size, size_t sector_count) {
    struct xip_sector_read *xs = (struct xip_sector_read *) calloc(1, sizeof(struct xip_sector_read));
#if PICO_ON_DEVICE
    assert(0x1u == (((uintptr_t)flash_ptr)>>28u));
#endif
    assert(!(0x3u & (uintptr_t) flash_ptr));
    xs->sector_read.funcs = &xip_sector_read_funcs;
//    printf("Open %p %dx%d\n", flash_ptr, (int)sector_size, (int)sector_count);
    xs->sector_read.funcs = &xip_sector_read_funcs;
    xs->sector_read.sector_size = sector_size;
    xs->sector_read.sector_count = sector_count;
    xs->flash_ptr = flash_ptr;
    xs->buffer = malloc(sector_size);
    return &xs->sector_read;
}

