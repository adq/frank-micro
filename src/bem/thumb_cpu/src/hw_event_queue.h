/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifdef USE_HW_EVENT
#ifndef B_EM_PICO_HW_EVENT_QUEUE_H
#define B_EM_PICO_HW_EVENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include "list.h"

typedef int32_t cycle_timestamp_t;

struct hw_event {
    struct list_element e;
    cycle_timestamp_t target; // global time for the event
    // return true to requeue the event
    bool (*invoke)(struct hw_event *event);
    cycle_timestamp_t user_time;
    void *user_data;
};


void upsert_hw_event(struct hw_event *event);
void remove_hw_event(struct hw_event *event);
void set_simple_hw_event_counter(struct hw_event *event, int cycles);

// set when the CPU should return from execution
void set_cpu_limit(cycle_timestamp_t limit);

// cycles as counted by the CPU
cycle_timestamp_t get_cpu_timestamp();
// current cycles as seen by the hardware (i.e. events before that will have been delivered)
cycle_timestamp_t get_hardware_timestamp();

// adjust g_cpu.clk so that the cpu times out at the right point
void set_next_cpu_clk();
void advance_hardware(cycle_timestamp_t to);
// will set g_cpu.clk to 0, to cause loop breakout
int32_t possible_cpu_irq_breakout();
extern cycle_timestamp_t hw_event_motor_base;
#endif
#endif
