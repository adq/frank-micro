/*
 * frank-micro — BBC Micro for RP2350
 * state_pico.c — Minimal save-state stubs for Pico build.
 */
#include "state.h"
#include "bbc.h"
#include <stdint.h>
#include <stdio.h>

void state_save(struct bbc_struct* p_bbc, const char* p_filename) {
    (void)p_bbc; (void)p_filename;
    printf("state_save: not implemented on Pico\n");
}

void state_load(struct bbc_struct* p_bbc, const char* p_filename) {
    (void)p_bbc; (void)p_filename;
    printf("state_load: not implemented on Pico\n");
}
