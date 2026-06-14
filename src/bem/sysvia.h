#ifndef __INC_SYSVIA_H
#define __INC_SYSVIA_H

extern VIA sysvia;

void    sysvia_reset(void);

void    sysvia_savestate(FILE *f);
void    sysvia_loadstate(FILE *f);

extern uint8_t IC32;
extern uint8_t sdbval;

void set_scrsize(int);

static inline void sysvia_write(uint16_t addr, uint8_t val)
{
    via_write(&sysvia, addr, val);
}

static inline uint8_t sysvia_read(uint16_t addr)
{
    return via_read(&sysvia, addr);
}


static inline void sysvia_set_ca1(int level)
{
    via_set_ca1(&sysvia,level);
}
extern void sysvia_set_ca2(int level);

static inline void sysvia_set_cb1(int level)
{
    via_set_cb1(&sysvia,level);
}

static inline void sysvia_set_cb2(int level)
{
    via_set_cb2(&sysvia,level);
}

#endif
