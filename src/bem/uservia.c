/*B-em v2.1 by Tom Walker
  User VIA + Master 512 mouse emulation*/

#include <stdio.h>
#include "b-em.h"
#include "via.h"
#include "uservia.h"
#include "model.h"
#include "compact_joystick.h"
#include "mouse.h"
#include "music4000.h"
#include "sound.h"

VIA uservia;

uint8_t lpt_dac;
ALLEGRO_USTR *prt_clip_str;
FILE *prt_fp;

void uservia_write_portA(uint8_t val)
{
    if (prt_clip_str || prt_fp) {
        // Printer output.
        if (val == 0x60)
            val = 0xa3; // pound sign.
        if (prt_clip_str)
            al_ustr_append_chr(prt_clip_str, val);
        if (prt_fp)
            putc(val, prt_fp);
        via_set_ca1(&uservia, 1);
        log_debug("uservia: set CA1 low for printer");
    }
    else
        lpt_dac = val; /*Printer port - no printer, just 8-bit DAC*/
}

void printer_set_ca2(int level)
{
    if (level && (prt_clip_str || prt_fp)) {
        via_set_ca1(&uservia, 0);
        log_debug("uservia: set CA1 high for printer");
    }
}

void uservia_write_portB(uint8_t val)
{
    /*User port - nothing emulated*/
    log_debug("uservia_write_portB: %02X", val);
}

uint8_t uservia_read_portA()
{
        return 0xff; /*Printer port - read only*/
}

uint8_t uservia_read_portB()
{
#ifndef NO_USE_MOUSE
    if (curtube == 3 || mouse_amx)
        return mouse_portb;
#else
    if (curtube == 3) return 0xff;
#endif
#ifndef NO_USE_JOYSTICK
    if (compactcmos)
        return compact_joystick_read();
#endif
#ifndef NO_USE_MUSIC5000
    if (sound_music5000)
        return music4000_read();
#endif
    return 0xff; /*User port - nothing emulated*/
}

void uservia_reset()
{
        via_reset(&uservia);

        uservia.read_portA = uservia_read_portA;
        uservia.read_portB = uservia_read_portB;

        uservia.write_portA = uservia_write_portA;
        uservia.write_portB = uservia_write_portB;

        uservia.set_ca2 = printer_set_ca2;
        uservia.set_cb2 = music4000_shift;

        uservia.intnum = 2;
}

void dumpuservia()
{
        log_debug("T1 = %04X %04X T2 = %04X %04X\n",via_get_t1c(&uservia),(int)uservia.t1l,via_get_t2c(&uservia),(int)uservia.t2l);
        log_debug("%02X %02X  %02X %02X\n",uservia.ifr,uservia.ier,uservia.pcr,uservia.acr);
}

#ifndef NO_USE_SAVE_STATE
void uservia_savestate(FILE *f)
{
        via_savestate(&uservia, f);
}

void uservia_loadstate(FILE *f)
{
        via_loadstate(&uservia, f);
}
#endif
