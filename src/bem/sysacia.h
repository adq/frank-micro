#ifndef __INC_SYSACIA_H
#define __INC_SYSACIA_H

#ifndef NO_USE_ACIA
#include "acia.h"

extern ACIA sysacia;
extern int sysacia_tapespeed;

void sysacia_poll(void);
#endif
#endif
