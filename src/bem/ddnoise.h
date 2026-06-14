#ifndef __INC_DDNOISE_H
#define __INC_DDNOISE_H

#include <allegro5/allegro_audio.h>
extern ALLEGRO_SAMPLE *find_load_wav(ALLEGRO_PATH *dir, const char *name);
#ifndef NO_USE_DD_NOISE
void ddnoise_init(void);
void ddnoise_close(void);
void ddnoise_seek(int len);
void ddnoise_spinup(void);
void ddnoise_headdown(void);
void ddnoise_spindown(void);
extern int8_t ddnoise_vol;
extern int8_t ddnoise_type;
extern int ddnoise_ticks;
#else
#include "disc.h"
static inline void ddnoise_init(void) {}
static inline void ddnoise_close(void) {}
static inline void ddnoise_seek(int len) {set_fdc_time(200);}
static inline void ddnoise_spinup(void) {}
static inline void ddnoise_headdown(void) {}
static inline void ddnoise_spindown(void) {}
#endif
#endif
