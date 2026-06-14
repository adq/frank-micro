/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include "b-em.h"
#include <assert.h>
#include "cpu/cpu.h"

#include "6502.h"
#ifdef USE_HW_EVENT
#include "hw_event_queue.h"
#endif

uint8_t __time_critical_func(readmem)(uint16_t addr) {
    return C6502_Read8(addr);
}

void __time_critical_func(writemem)(uint16_t addr, uint8_t val) {
    C6502_Write8(addr, val);
}

void __time_critical_func(nmi_set_mask)(uint mask) {
#ifdef USE_HW_EVENT
    possible_cpu_irq_breakout();
#endif
//    printf("NMI SET MASK %d\n", get_cpu_timestamp());
    g_cpu.nmi |= mask;
}

void __time_critical_func(nmi_clr_mask)(uint mask) {
    g_cpu.nmi &= ~mask;
}

void __time_critical_func(nmi_set)(uint val) {
//    printf("NMI SET %d\n", get_hardware_timestamp());
#ifdef USE_HW_EVENT
    if (val) possible_cpu_irq_breakout();
#endif
    g_cpu.nmi = val;
}

// todo we need to know if these are valid
// may need a force into C mode!
uint8_t get_a() {
    assert_if_asm();
    return g_cpu.a;
}

void set_a(uint8_t _a) {
    assert_if_asm();
    g_cpu.a = _a;
}

uint8_t get_x() {
    assert_if_asm();
    return g_cpu.x;
}

void set_x(uint8_t _x) {
    assert_if_asm();
    g_cpu.x = _x;
}

uint8_t get_y() {
    assert_if_asm();
    return g_cpu.y;
}

void set_y(uint8_t _y) {
    assert_if_asm();
    g_cpu.y = _y;
}

int get_c() {
    assert_if_asm();
    return FLG_ISSET(g_cpu, c);
}

void set_c(int _c) {
    assert_if_asm();
    FLG_SETBOOL(g_cpu, c, _c);
}

int get_z() {
    assert_if_asm();
    assert(false);
    return 0;
}

void set_z(int z) {
    assert_if_asm();
    assert(false);
}

uint16_t get_pc() {
//    assert_if_asm();
    return g_cpu.pc;
}

void set_pc(uint16_t _pc) {
    CPUUpdatePC(_pc);
}

int get_n() {
    assert(false);
    return 0;
}

int get_v() {
    return FLG_ISSET(g_cpu, v);
}

int get_d() {
    return FLG_ISSET(g_cpu, d);
}

int get_i() {
    return FLG_ISSET(g_cpu, i);
}

void m6502_init(void) {
    CPUInit();
    g_cpu.memHandlers = MemMapInit();
}

void m6502_reset(void) {
    CPUReset();
}

void /*__time_critical*/ m6502_exec(void) {
    // run for 1/50 of a second
#ifdef USE_HW_EVENT
    set_cpu_limit(get_cpu_timestamp() + 40000);
#else
    g_cpu.clk -= 40000;
#endif
    CPURun(false);
}

void m65c02_exec(void) {
    // run for 1/50 of a second
#ifdef USE_HW_EVENT
    set_cpu_limit(get_cpu_timestamp() + 40000);
#else
    g_cpu.clk -= 40000;
#endif
    CPURun(true);
}

void os_paste_start(char *str) {
    printf("paste!?");
    assert(false);
}


void /*__time_critical*/ interrupt_set_mask(uint mask) {
    g_cpu.interrupt |= mask;
}

void /*__time_critical*/ interrupt_clr_mask(uint mask) {
    g_cpu.interrupt &= ~mask;
}
