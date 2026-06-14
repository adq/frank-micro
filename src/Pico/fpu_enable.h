/*
 * frank-micro — BBC Micro for RP2350
 * fpu_enable.h — Enable the Cortex-M33 FPU (CP10/CP11) on the calling core.
 *
 * The custom fast-boot path used by this project does not run the Pico SDK's
 * runtime_init_per_core_enable_coprocessors(), so the FPU is left disabled
 * (CPACR.CP10/CP11 == 0). GCC, however, freely emits VFP instructions (e.g.
 * `vpush {d8}` to spill 64-bit values) in ordinary integer code. Executing any
 * such instruction with the FPU disabled raises a NOCP UsageFault that escalates
 * to a HardFault. Enabling the FPU on every core makes those instructions valid
 * everywhere. FPCCR ASPEN/LSPEN (automatic + lazy FP state preservation) are set
 * by the SDK, so FP use in interrupt handlers is also safe.
 */
#ifndef FRANK_FPU_ENABLE_H
#define FRANK_FPU_ENABLE_H

#include <stdint.h>

/* CPACR: grant full access to CP10 and CP11 (the FPU). */
static inline void frank_enable_fpu(void) {
    volatile uint32_t* cpacr = (volatile uint32_t*)0xE000ED88u;
    *cpacr |= (0x3u << 20) | (0x3u << 22);
    __asm volatile("dsb");
    __asm volatile("isb");
}

#endif /* FRANK_FPU_ENABLE_H */
