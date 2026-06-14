#ifndef __INC_TAPE_H
#define __INC_TAPE_H

#include "acia.h"

#ifndef NO_USE_TAPE
extern ALLEGRO_PATH *tape_fn;

void tape_load(ALLEGRO_PATH *fn);
void tape_close(void);
void tape_poll(void);
void tape_receive(ACIA *acia, uint8_t data);

extern bool tape_loaded;
extern int tapelcount,tapellatch;
extern bool fasttape;
#endif

#endif
