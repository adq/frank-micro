#ifndef BEEBJIT_INTERP_H
#define BEEBJIT_INTERP_H

#include <stdint.h>

struct cpu_driver;
struct cpu_driver_funcs;
struct interp_struct;

struct cpu_driver* interp_create(struct cpu_driver_funcs* p_funcs,
                                 int is_65c12);

void interp_set_instruction_callback(
    struct interp_struct* p_interp,
    int (*instruction_callback)(void* p,
                                uint16_t next_pc,
                                uint8_t done_opcode,
                                uint16_t done_addr,
                                int next_is_irq,
                                int irq_pending),
    void* p_callback_context);

int64_t interp_enter_with_countdown(struct interp_struct* p_interp,
                                    int64_t countdown);
int interp_has_memory_written_callback(struct interp_struct* p_interp);

void interp_testing_unexit(struct interp_struct* p_interp);

void interp_pcring_arm(uint16_t trigger);
void interp_watch_arm(uint16_t addr);
void interp_watch_arm_val(uint16_t addr, int val);
int interp_pcring_dump(uint16_t* p_out, int max);
int interp_pcring_trapped(void);
int interp_trap_regs(uint8_t* p_out8);
void interp_trap_stack(uint8_t* p_out64);
void interp_trap_mem(uint8_t* p_out64);
void interp_trap_mem_set_addr(uint16_t addr);

#endif /* BEEBJIT_INTERP_H */
