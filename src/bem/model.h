#ifndef __INC_MODEL_H
#define __INC_MODEL_H

#include "cpu_debug.h"
#include "savestate.h"

typedef enum
{
    FDC_NONE,
    FDC_I8271,
    FDC_ACORN,
    FDC_MASTER,
    FDC_OPUS,
    FDC_STL,
    FDC_WATFORD,
    FDC_MAX
} fdc_type_t;

typedef struct
{
    char name[8];
    void (*func)(void);
} rom_setup_t;

typedef struct
{
    const char *cfgsect;
    const char *name;
    const char *os;
    const char *cmos;
    rom_setup_t *romsetup;
    fdc_type_t fdc_type;
    uint8_t x65c02:1;
    uint8_t bplus:1;
    uint8_t master:1;
    uint8_t modela:1;
    uint8_t os01:1;
    uint8_t compact:1;
    int tube;
} MODEL;

extern MODEL *models;
extern int8_t model_count;

typedef struct
{
    char name[32];
    bool (*init)(void *rom);
    void (*reset)(void);
#ifndef NO_USE_DEBUGGER
    cpu_debug_t *debug;
#endif
    int  rom_size;
    char bootrom[16];
    int  speed_multiplier;
} TUBE;

#ifndef NO_USE_TUBE
#define NUM_TUBES 8
extern TUBE tubes[NUM_TUBES];
#endif

extern int8_t curmodel;
extern int8_t oldmodel;
#ifndef NO_USE_TUBE
extern int8_t curtube;
extern int8_t selecttube;
#else
#define curtube 0
#define selecttube -1
#endif
extern fdc_type_t fdc_type;
extern bool BPLUS, x65c02, MASTER, MODELA, OS01;
#ifndef NO_USE_COMPACT
extern bool compactcmos;
#else
#define compactcmos false
#endif

void model_loadcfg(void);
void model_check(void);
void model_init(void);
void model_savestate(FILE *f);
void model_loadstate(FILE *f);
void model_savecfg(void);

#endif
