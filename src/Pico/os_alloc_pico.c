/*
 * frank-micro — BBC Micro for RP2350
 * os_alloc_pico.c — Memory allocation for Pico.
 *
 * Strategy:
 *  - BBC 6502 address space (64KB flat): SRAM heap via malloc
 *  - Sideways ROMs (16 × 16KB = 256KB): PSRAM via psram_malloc
 *  - Everything else: SRAM heap via malloc
 *
 * The address-space double-mapping trick is disabled in bbc.c for PICO_BUILD
 * (map_size = 64KB, map_offset = 0), so we only need a single 64KB chunk.
 * ROM data is copied from PSRAM into the 6502 address space at bank-switch
 * time — PSRAM access is only needed during bank-switches, not at run time.
 */
#include "os_alloc.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* Simple struct to hold an "allocated" region. */
struct os_alloc_mapping {
    void* p_base;
    size_t size;
};

void* os_alloc_get_aligned(size_t alignment, size_t size) {
    void* p;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (posix_memalign(&p, alignment, size) != 0) return NULL;
    if (p) memset(p, 0, size);
    return p;
}

void os_alloc_free_aligned(void* p) {
    free(p);
}

/* Memory handles */
intptr_t os_alloc_get_memory_handle(size_t size) {
    /* Route large allocations to PSRAM to preserve SRAM for small objects. */
    void* p = NULL;
    if (size >= 16384 && psram_is_available()) {
        p = psram_malloc(size);
    }
    if (!p) {
        p = malloc(size);
    }
    if (!p) return (intptr_t)-1;
    memset(p, 0, size);
    return (intptr_t)p;
}

void os_alloc_free_memory_handle(intptr_t handle) {
    if (handle == (intptr_t)-1) return;
    void* p = (void*)handle;
    uintptr_t a = (uintptr_t)p;
    if (a >= 0x11000000u && a < 0x12000000u)
        psram_free(p);
    else
        free(p);
}

struct os_alloc_mapping* os_alloc_get_mapping_from_handle(intptr_t handle,
                                                           void* p_addr,
                                                           size_t offset,
                                                           size_t size) {
    (void)p_addr;
    struct os_alloc_mapping* m = malloc(sizeof(*m));
    if (!m) return NULL;
    m->p_base = (void*)((uint8_t*)handle + offset);
    m->size   = size;
    return m;
}

struct os_alloc_mapping* os_alloc_get_mapping(void* p_addr, size_t size) {
    struct os_alloc_mapping* m = malloc(sizeof(*m));
    if (!m) return NULL;
    m->p_base = p_addr;
    m->size   = size;
    return m;
}

void* os_alloc_get_mapping_addr(struct os_alloc_mapping* p_mapping) {
    return p_mapping->p_base;
}

void os_alloc_free_mapping(struct os_alloc_mapping* p_mapping) {
    free(p_mapping);
}

/* Permission changes are no-ops on a flat SRAM MCU. */
void os_alloc_make_mapping_read_only(void* p, size_t size)       { (void)p; (void)size; }
void os_alloc_make_mapping_read_write(void* p, size_t size)      { (void)p; (void)size; }
void os_alloc_make_mapping_read_write_exec(void* p, size_t size) { (void)p; (void)size; }
void os_alloc_make_mapping_read_exec(void* p, size_t size)       { (void)p; (void)size; }
void os_alloc_make_mapping_none(void* p, size_t size)            { (void)p; (void)size; }

/*
 * Called from bbc.c (PICO_BUILD) to allocate full-size sideways ROM storage.
 * Uses PSRAM if available, otherwise falls back to SRAM heap.
 */
uint8_t* pico_alloc_sw_roms(size_t size) {
    if (psram_is_available()) {
        uint8_t* p = (uint8_t*)psram_malloc(size);
        if (p) {
            memset(p, 0xFF, size);   /* standard empty ROM value */
            return p;
        }
    }
    return NULL;  /* caller falls back to malloc() */
}
