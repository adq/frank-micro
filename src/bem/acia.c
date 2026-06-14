/*B-em v2.2 by Tom Walker
 * Pico version (C) 2021 Graham Sanderson
 *
 * 6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"

// NOTE: for USE_HW_EVENT I haven't bothered to encapsulate, however we need an event periodically (we continue to use every 128
//       when TXD_REG_EMP is clear in acia->status_reg, so we need to check everywhere that

#ifndef NO_USE_ACIA
/* Status register flags */

#define RXD_REG_FUL 0x01
#define TXD_REG_EMP 0x02
#define DCD         0x04
#define CTS         0x08
#define FRAME_ERR   0x10
#define RX_OVERRUN  0x20
#define PARITY_ERR  0x40
#define INTERUPT    0x80

#ifdef USE_HW_EVENT
bool __time_critical_func(invoke_acia)(struct hw_event *event) {
    acia_poll((ACIA *)event);
    return false;
}

static struct hw_event acia_event = {
    .invoke = invoke_acia
};

void updated_TXD_REG_EMP(ACIA *acia) {
    if (acia->status_reg & TXD_REG_EMP) {
        remove_hw_event(&acia_event);
    } else {
        acia_event.target = get_hardware_timestamp() + 128; // what it used to do in otherpoll
        upsert_hw_event(&acia_event);
    }
}
#else
void updated_TXD_REG_EMP(ACIA *acia) {}
#endif
static inline int rx_int(ACIA *acia) {
    return (acia->status_reg & INTERUPT) && (acia->control_reg & INTERUPT);
}

static inline int tx_int(ACIA *acia) {
    return (acia->status_reg & TXD_REG_EMP) && ((acia->control_reg & 0x60) == 0x20);
}    

static void __time_critical_func(acia_updateint)(ACIA *acia) {
    if (rx_int(acia) || tx_int(acia))
        interrupt_set_mask(4);
    else
        interrupt_clr_mask(4);
}

void acia_reset(ACIA *acia) {
#ifdef USE_HW_EVENT
    // todo yuk.. i guess there is only one ACIA - it was after all polled specifically for sysvia
    assert(!acia_event.user_data || acia_event.user_data == acia);
    acia_event.user_data = acia;
#endif
    acia->status_reg = (acia->status_reg & (CTS|DCD)) | TXD_REG_EMP;
    updated_TXD_REG_EMP(acia);
    acia_updateint(acia);
}

uint8_t acia_read(ACIA *acia, uint16_t addr) {
    uint8_t temp;

    if (addr & 1) {
        temp = acia->rx_data_reg;
        acia->status_reg &= ~(INTERUPT | RXD_REG_FUL);
        acia_updateint(acia);
        return temp;
    }
    else {
        temp = acia->status_reg & ~INTERUPT;
        if (rx_int(acia) || tx_int(acia))
            temp |= INTERUPT;
        return temp;
    }
}

void acia_write(ACIA *acia, uint16_t addr, uint8_t val) {
    if (addr & 1) {
        acia->tx_data_reg = val;
        if (acia->tx_hook)
            acia->tx_hook(acia, val);
        acia->status_reg &= ~TXD_REG_EMP;
        updated_TXD_REG_EMP(acia);
    }
    else if (val != acia->control_reg) {
        if ((val & 0x60) != 0x20) // interupt being turned off
            if (acia->tx_end)
                acia->tx_end(acia);
        acia->control_reg = val;
        if (val == 3)
            acia_reset(acia);
        if (acia->set_params)
            acia->set_params(acia, val);
        acia_updateint(acia);
    }
}

void acia_dcdhigh(ACIA *acia) {
    if (acia->status_reg & DCD)
        return;
    acia->status_reg |= DCD | INTERUPT;
    acia_updateint(acia);
}

void acia_dcdlow(ACIA *acia) {
    acia->status_reg &= ~DCD;
    acia_updateint(acia);
}

void __time_critical_func(acia_poll)(ACIA *acia) {
    if (!(acia->status_reg & TXD_REG_EMP)) {
        acia->status_reg |= TXD_REG_EMP;
        acia_updateint(acia);
        acia_updateint(acia);
    }
}

void acia_receive(ACIA *acia, uint8_t val) { /*Called when the acia recives some data*/
    acia->rx_data_reg = val;
    acia->status_reg |= RXD_REG_FUL | INTERUPT;
    if (acia->rx_hook)
        acia->rx_hook(acia, val);
    acia_updateint(acia);
}

void acia_savestate(ACIA *acia, FILE *f) {
    putc(acia->control_reg, f);
    putc(acia->status_reg, f);
}

void acia_loadstate(ACIA *acia, FILE *f) {
    acia->control_reg = getc(f);
    acia->status_reg = getc(f);
}
#endif
