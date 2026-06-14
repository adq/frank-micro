#ifndef __INC_USERVIA_H
#define __INC_USERVIA_H

#include "via.h"

extern VIA uservia;
extern ALLEGRO_USTR *prt_clip_str;
extern FILE *prt_fp;

void    uservia_reset(void);

void    uservia_savestate(FILE *f);
void    uservia_loadstate(FILE *f);

extern uint8_t lpt_dac;

static inline void uservia_write(uint16_t addr, uint8_t val)
{
    via_write(&uservia, addr, val);
}

static inline uint8_t uservia_read(uint16_t addr)
{
    return via_read(&uservia, addr);
}

static inline void uservia_set_ca1(int level)
{
    via_set_ca1(&uservia, level);
}
static inline void uservia_set_ca2(int level)
{
    via_set_ca2(&uservia, level);
}
static inline void uservia_set_cb1(int level)
{
    via_set_cb1(&uservia, level);
}
static inline void uservia_set_cb2(int level)
{
    via_set_cb2(&uservia, level);
}


#endif
