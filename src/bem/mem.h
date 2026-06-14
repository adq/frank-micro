#ifndef __INC_MEM_H
#define __INC_MEM_H

#include "savestate.h"

#define RAM_SIZE  (64*1024)
#define ROM_SIZE  (16*1024)
#define ROM_NSLOT 16

typedef struct {
    uint8_t swram;    // this slot behaves as sideways RAM.
    uint8_t locked;   // slot is essential to machine config.
    uint8_t use_name; // use a short name writing to config file.
    uint8_t alloc;    // name/path are from malloc(3)
    char *name;       // short name for the loaded ROM.
    char *path;       // full filestystem path for the loaded ROM.
#ifdef NO_USE_RAM_ROMS
    const uint8_t *data;
#endif
} rom_slot_t;

extern void mem_romsetup_os01(void);
extern void mem_romsetup_std(void);
extern void mem_romsetup_swram(void);
extern void mem_romsetup_bp128(void);
extern void mem_romsetup_master(void);
extern void mem_romsetup_compact(void);
extern void mem_fillswram(void);
extern int mem_findswram(int n);
extern void mem_clearroms(void);

void mem_clearrom(int slot);
void mem_loadrom(int slot, const char *name, const char *path, uint8_t rel);
const uint8_t *mem_romdetail(int slot);
void mem_save_romcfg(const char *sect);

void mem_init(void);
void mem_reset(void);
void mem_close(void);

#ifndef NO_USE_SAVE_STATE
void mem_savezlib(ZFILE *zfp);
void mem_loadzlib(ZFILE *zfp);
void mem_loadstate(FILE *f);
#endif

void mem_dump(void);

extern uint8_t ram_fe30, ram_fe34;
extern uint8_t *ram;
extern rom_slot_t rom_slots[ROM_NSLOT];

#ifndef NO_USE_RAM_ROMS
extern uint8_t *os;
static inline uint8_t* rom_slot_ptr(int slot) {
    extern uint8_t *rom;
    return rom + slot * ROM_SIZE;
}
#else
extern const uint8_t *os;
// read and write for non device so code can't mutate value and read it back
extern uint8_t *g_garbage_read;
extern uint8_t *g_garbage_write;
static inline const uint8_t* rom_slot_ptr(int slot) {
    const uint8_t *rc = rom_slots[slot].data;
    if (!rc) {
        rc = g_garbage_read;
//        printf("Garbage for slot %d\n", slot);
    }
    return rc;
}
#endif
#endif
