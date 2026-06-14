#ifndef __INC_6502_H
#define __INC_6502_H

#include "6502debug.h"

//extern uint8_t a,x,y,s;
#ifndef NO_USE_DEBUGGER
extern uint16_t oldpc, oldoldpc, pc3;
#endif

uint8_t get_a();
void set_a(uint8_t _a);
uint8_t get_x();
void set_x(uint8_t _x);
uint8_t get_y();
void set_y(uint8_t _y);
int get_c();
void set_c(int c);
int get_z();
void set_z(int z);
uint16_t get_pc();
void set_pc(uint16_t _pc);
int get_n();
int get_v();
int get_d();
int get_i();

#ifndef NO_USE_DEBUGGER
extern cpu_debug_t core6502_cpu_debug;
#endif

extern int output;
//extern int interrupt;
void interrupt_set_mask(uint mask);
void interrupt_clr_mask(uint mask);
void nmi_set_mask(uint mask);
void nmi_clr_mask(uint mask);
void nmi_set(uint value);
static inline void nmi_clr_all() {
    nmi_set(0);
}

//extern uint8_t opcode;

void m6502_init(void);
void m6502_reset(void);
void m6502_exec(void);
void m65c02_exec(void);
void dumpregs(void);

uint8_t readmem(uint16_t addr);
void writemem(uint16_t addr, uint8_t val);

#ifndef NO_USE_SAVE_STATE
void m6502_savestate(FILE *f);
void m6502_loadstate(FILE *f);
#endif

void os_paste_start(char *str);

#endif
