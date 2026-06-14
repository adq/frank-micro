#ifndef __INC_VIA_H
#define __INC_VIA_H

#ifdef PICO_BUILD
#include "hw_event_queue.h"
#endif

typedef struct VIA
{
        uint8_t  ora,   orb,   ira,   irb;
        uint8_t  ddra,  ddrb;
        uint8_t  sr;
        uint8_t  t1pb7;
        uint32_t t1l,   t2l;
#ifndef USE_HW_EVENT
        int      t1c,   t2c;
#endif
        uint8_t  acr,   pcr,   ifr,   ier;
        int      t1hit, t2hit;
        int      ca1,   ca2,   cb1,   cb2;
        int      intnum;
        int      sr_count;

        uint8_t  (*read_portA)(void);
        uint8_t  (*read_portB)(void);
        void     (*write_portA)(uint8_t val);
        void     (*write_portB)(uint8_t val);

        // todo graham remove some of these unused
        void     (*set_ca1)(int level);
        void     (*set_ca2)(int level);
        void     (*set_cb1)(int level);
        void     (*set_cb2)(int level);
        void     (*timer_expire1)(void);
#ifdef USE_HW_EVENT
        struct hw_event timer1_event;
        struct hw_event timer2_event;
        struct hw_event sr_event;
        int t2_stopped_at;
#endif
} VIA;

uint8_t via_read(VIA *v, uint16_t addr);
void    via_write(VIA *v, uint16_t addr, uint8_t val);
void    via_reset(VIA *v);
void    via_shift(VIA *v, int cycles);

void via_set_ca1(VIA *v, int level);
void via_set_ca2(VIA *v, int level);
void via_set_cb1(VIA *v, int level);
void via_set_cb2(VIA *v, int level);

void via_savestate(VIA *v, FILE *f);
void via_loadstate(VIA *v, FILE *f);

void via_poll(VIA *v, int cycles);

int via_get_t1c(VIA *v);
int via_get_t2c(VIA *v);
#endif
