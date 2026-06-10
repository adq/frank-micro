/*
 * frank-micro — BBC Micro for RP2350
 * os_lock_pico.c — Mutex via pico-sdk spinlocks.
 */
#include "os_lock.h"
#include "hardware/sync.h"
#include <stdlib.h>

struct os_lock_struct {
    spin_lock_t* p_spin;
    uint32_t     saved_irq;
};

struct os_lock_struct* os_lock_create(void) {
    struct os_lock_struct* p = malloc(sizeof(*p));
    if (!p) return NULL;
    uint idx = spin_lock_claim_unused(true);
    p->p_spin = spin_lock_instance(idx);
    p->saved_irq = 0;
    return p;
}

void os_lock_destroy(struct os_lock_struct* p_lock) {
    free(p_lock);
}

void os_lock_lock(struct os_lock_struct* p_lock) {
    p_lock->saved_irq = spin_lock_blocking(p_lock->p_spin);
}

void os_lock_unlock(struct os_lock_struct* p_lock) {
    spin_unlock(p_lock->p_spin, p_lock->saved_irq);
}
