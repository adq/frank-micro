/*
 * frank-micro — BBC Micro for RP2350
 * os_channel_pico.c — Inter-core message channel via shared FIFO.
 *
 * In frank-micro the BBC CPU runs on Core 1 and the client (vsync handler)
 * runs on Core 0.  Messages are passed through a simple spinlock-protected
 * shared buffer.  The RP2350 hardware FIFO is used to signal readiness so
 * each core can block efficiently without spinning the bus.
 */
#include "os_channel.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* We only ever have ONE channel pair: BBC (Core 1) ↔ client (Core 0). */
#define CH_BBC_TO_CLIENT  0   /* BBC writes, client reads */
#define CH_CLIENT_TO_BBC  1   /* client writes, BBC reads */

#define MSG_SIZE 32   /* sizeof(struct bbc_message) is 5×uint32_t = 20 bytes, pad to 32 */

static uint8_t  g_msg_buf[2][MSG_SIZE];
static spin_lock_t* g_lock[2];
static volatile bool g_pending[2];

/* Handles handed out by os_channel_get_handles:
 *   read1  = BBC reads  (CH_CLIENT_TO_BBC)
 *   write1 = BBC writes (CH_BBC_TO_CLIENT)
 *   read2  = client reads  (CH_BBC_TO_CLIENT)
 *   write2 = client writes (CH_CLIENT_TO_BBC)
 */
void os_channel_get_handles(intptr_t* p_read1,
                             intptr_t* p_write1,
                             intptr_t* p_read2,
                             intptr_t* p_write2) {
    /* Allocate spin locks once. */
    static bool inited = false;
    if (!inited) {
        inited = true;
        uint lock0 = spin_lock_claim_unused(true);
        uint lock1 = spin_lock_claim_unused(true);
        g_lock[0] = spin_lock_instance(lock0);
        g_lock[1] = spin_lock_instance(lock1);
        g_pending[0] = false;
        g_pending[1] = false;
    }
    *p_read1  = (intptr_t)CH_CLIENT_TO_BBC;   /* BBC reads  */
    *p_write1 = (intptr_t)CH_BBC_TO_CLIENT;   /* BBC writes */
    *p_read2  = (intptr_t)CH_BBC_TO_CLIENT;   /* client reads */
    *p_write2 = (intptr_t)CH_CLIENT_TO_BBC;   /* client writes */
}

void os_channel_free_handles(intptr_t r1, intptr_t w1, intptr_t r2, intptr_t w2) {
    (void)r1; (void)w1; (void)r2; (void)w2;
}

void os_channel_write(intptr_t handle, const void* p_message, uint32_t length) {
    int ch = (int)handle;
    assert(ch == 0 || ch == 1);
    assert(length <= MSG_SIZE);

    uint32_t irq = spin_lock_blocking(g_lock[ch]);
    memcpy(g_msg_buf[ch], p_message, length);
    g_pending[ch] = true;
    spin_unlock(g_lock[ch], irq);

    /* Signal the other core via hardware FIFO. */
    multicore_fifo_push_blocking((uint32_t)ch | 0x80000000u);
}

void os_channel_read(intptr_t handle, void* p_message, uint32_t length) {
    int ch = (int)handle;
    assert(ch == 0 || ch == 1);
    assert(length <= MSG_SIZE);

    /* Wait until a message is available for this channel. */
    while (true) {
        uint32_t irq = spin_lock_blocking(g_lock[ch]);
        bool avail = g_pending[ch];
        if (avail) {
            memcpy(p_message, g_msg_buf[ch], length);
            g_pending[ch] = false;
            spin_unlock(g_lock[ch], irq);
            return;
        }
        spin_unlock(g_lock[ch], irq);

        /* Drain FIFO entries to wake up. */
        if (multicore_fifo_rvalid()) {
            (void)multicore_fifo_pop_blocking();
        } else {
            __wfe();
        }
    }
}
