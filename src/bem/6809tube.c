#include "b-em.h"
#include "tube.h"
#include "model.h"
#include "6809tube.h"
#include "mc6809nc/mc6809_debug.h"
#include "mc6809nc/mc6809.h"

#define MC6809_RAM_SIZE 0x10000

static int overlay_rom = 1;
static uint8_t *copro_mc6809_ram = NULL;
static uint8_t *copro_mc6809_rom;

void tube_6809_int(int new_irq)
{
    if ((new_irq & 1) && !(tube_irq & 1)) {
        log_debug("6809tube: requesting FIRQ");
        mc6809nc_request_firq(1);
    }
    else if (!(new_irq & 1) && (tube_irq & 1)) {
        log_debug("6809tube: releasing FIRQ");
        mc6809nc_release_firq(1);
    }
    if ((new_irq & 2) && !(tube_irq & 2)) {
        log_debug("6809tube: requesting IRQ");
        mc6809nc_request_irq(1);
        tubecycles = 3;
    }
    else if (!(new_irq & 2) && (tube_irq & 2)) {
        log_debug("6809tube: releasing IRQ");
        mc6809nc_release_irq(1);
    }
}

static uint8_t readmem(uint32_t addr)
{
    if ((addr & ~7) == 0xfee0) {
        uint8_t val = tube_parasite_read(addr & 7);
        overlay_rom = 0;
        return val;
    }
    if ((addr & ~0x7FF) == 0xF800 && overlay_rom)
        return copro_mc6809_rom[addr & 0x7FF];
    return copro_mc6809_ram[addr & 0xffff];
}

uint8_t copro_mc6809nc_read(uint16_t addr)
{
    uint8_t data = readmem(addr);
#ifndef NO_USE_DEBUGGER
    if (mc6809nc_debug_enabled)
        debug_memread(&mc6809nc_cpu_debug, addr, data, 1);
#endif
    return data;
}

static void writemem(uint32_t addr, uint8_t data)
{
    if ((addr & ~7) == 0xfee0) {
        overlay_rom = 0;
        tube_parasite_write(addr & 7, data);
    }
    else
        copro_mc6809_ram[addr & 0xffff] = data;
}

void copro_mc6809nc_write(uint16_t addr, uint8_t data)
{
#ifndef NO_USE_DEBUGGER
    if (mc6809nc_debug_enabled)
        debug_memwrite(&mc6809nc_cpu_debug, addr, data, 1);
#endif
    writemem(addr, data);
}

#ifndef NO_USE_SAVE_STATE
static void mc6809nc_savestate(ZFILE *zfp)
{
    uint16_t reg;
    uint8_t bytes[14];

    bytes[0] = get_6809_a();
    bytes[1] = get_6809_b();
    bytes[2] = get_6809_dp();
    bytes[3] = get_6809_cc();
    reg = get_6809_x();
    bytes[4] = (reg >> 8);
    bytes[5] = reg;
    reg = get_6809_y();
    bytes[6] = (reg >> 8);
    bytes[7] = reg;
    reg = get_6809_s();
    bytes[8] = (reg >> 8);
    bytes[9] = reg;
    reg = get_6809_u();
    bytes[10] = (reg >> 8);
    bytes[11] = reg;
    reg = get_6809_pc();
    bytes[12] = (reg >> 8);
    bytes[13] = reg;

    savestate_zwrite(zfp, bytes, sizeof bytes);
    savestate_zwrite(zfp, copro_mc6809_ram, MC6809_RAM_SIZE);
    savestate_zwrite(zfp, copro_mc6809_rom, tubes[curtube].rom_size);
}

static void mc6809nc_loadstate(ZFILE *zfp)
{
    uint8_t bytes[14];

    savestate_zread(zfp, bytes, sizeof bytes);
    set_6809_a(bytes[0]);
    set_6809_b(bytes[1]);
    set_6809_dp(bytes[2]);
    set_6809_cc(bytes[3]);
    set_6809_x((bytes[4] << 8) | bytes[5]);
    set_6809_y((bytes[6] << 8) | bytes[7]);
    set_6809_s((bytes[8] << 8) | bytes[9]);
    set_6809_u((bytes[10] << 8) | bytes[11]);
    set_6809_pc((bytes[12] << 8) | bytes[13]);

    savestate_zread(zfp, copro_mc6809_ram, MC6809_RAM_SIZE);
    savestate_zread(zfp, copro_mc6809_rom, tubes[curtube].rom_size);;
}
#endif

bool tube_6809_init(void *rom)
{
    log_debug("mc6809nc: init");
    if (!copro_mc6809_ram) {
        copro_mc6809_ram = malloc(MC6809_RAM_SIZE);
        if (!copro_mc6809_ram) {
            log_error("mc6809: unable to allocate RAM: %s", strerror(errno));
            return false;
        }
    }
    copro_mc6809_rom = rom;
    tube_type = TUBE6809;
    tube_readmem = readmem;
    tube_writemem = writemem;
    tube_exec  = mc6809nc_execute;
#ifndef NO_USE_SAVE_STATE
    tube_proc_savestate = mc6809nc_savestate;
    tube_proc_loadstate = mc6809nc_loadstate;
#endif
    mc6809nc_reset();
    return true;
}

void mc6809nc_close(void)
{
    if (copro_mc6809_ram) {
        free(copro_mc6809_ram);
        copro_mc6809_ram = NULL;
    }
}
