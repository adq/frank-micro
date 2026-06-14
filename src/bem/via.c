/*B-em v2.2 by Tom Walker
  * Pico version (C) 2021 Graham Sanderson
  *
  * VIA  emulation*/
#include "b-em.h"
#include "6502.h"
#include "via.h"

#define INT_CA1    0x02
#define INT_CA2    0x01
#define INT_CB1    0x10
#define INT_CB2    0x08
#define INT_TIMER1 0x40
#define INT_TIMER2 0x20

#define     ORB     0x00
#define     ORA 0x01
#define     DDRB    0x02
#define     DDRA    0x03
#define     T1CL    0x04
#define     T1CH    0x05
#define     T1LL    0x06
#define     T1LH    0x07
#define     T2CL    0x08
#define     T2CH    0x09
#define     SR  0x0a
#define     ACR 0x0b
#define     PCR 0x0c
#define     IFR 0x0d
#define     IER 0x0e
#define     ORAnh   0x0f

#define TLIMIT -3

static void __time_critical_func(via_updateIFR)(VIA *v)
{
    // REMOVE DEBUGGING
//
//    extern int hc, vc;
//    printf("IFR update %02x %d:%d %d\n", v->ifr, hc, vc, get_cpu_timestamp());

    if ((v->ifr & 0x7F) & (v->ier & 0x7F))
        {
                v->ifr |= 0x80;
                interrupt_set_mask(v->intnum);
        }
        else
        {
                v->ifr &= ~0x80;
                interrupt_clr_mask(v->intnum);
        }
}

static void __time_critical_func(via_set_sr_count)(VIA *v, int count) {
    v->sr_count = count;
#ifdef USE_HW_EVENT
    // todo we only support system clock rate writes, as this is all that is checked
    //  in via_clock- easily fixed
    if (count > 0 && (v->acr & 0x1c) == 0x18) {
        v->sr_event.user_time = get_hardware_timestamp();
        v->sr_event.target = v->sr_event.user_time + count;
        upsert_hw_event(&v->sr_event);
    } else {
        remove_hw_event(&v->sr_event);
    }
#endif
}


static void __time_critical_func(via_set_t1c)(VIA *v, int cycles) {
#ifndef USE_HW_EVENT
    v->t1c = cycles;
#else
    v->timer1_event.target = get_hardware_timestamp() + cycles - TLIMIT + 1;
    upsert_hw_event(&v->timer1_event);
#endif
}

static void __time_critical_func(via_set_t2c)(VIA *v, int cycles) {
#ifndef USE_HW_EVENT
    v->t2c = cycles;
#else
    v->timer2_event.target = get_hardware_timestamp() + cycles - TLIMIT + 1;
    upsert_hw_event(&v->timer2_event);
#endif
}

int __time_critical_func(via_get_t1c)(VIA *v) {
#ifndef USE_HW_EVENT
    return v->t1c;
#else
    int t = (int)(v->timer1_event.target - get_hardware_timestamp() + TLIMIT - 1);
    return t;
#endif
}

int __time_critical_func(via_get_t2c)(VIA *v) {
#ifndef USE_HW_EVENT
    return v->t2c;
#else
    int t;
    if (v->t2_stopped_at >= 0) {
        t = v->t2_stopped_at;
    } else {
        t = (int) (v->timer2_event.target - get_hardware_timestamp() + TLIMIT - 1);
    }
    return t;
#endif
}

static void __time_critical_func(t1_reached)(VIA *v) {
    int t1c = via_get_t1c(v);
//    printf("T1 REACHED AT %d\n", t1c);
    assert (t1c < TLIMIT);
    while (t1c < TLIMIT) {
        t1c += v->t1l + 4;
        via_set_t1c(v, t1c);
    }
    if (!v->t1hit) {
        v->ifr |= INT_TIMER1;
        via_updateIFR(v);
        if (v->timer_expire1)
            v->timer_expire1();
        if (v->acr & 0x80) /*Output to PB7*/
            v->t1pb7 ^= 0x80;
    }
    if (!(v->acr & 0x40))
        v->t1hit = 1;
}

static void __time_critical_func(t2_reached)(VIA *v) {
    if (!v->t2hit) {
        v->ifr |= INT_TIMER2;
        via_updateIFR(v);
        v->t2hit=1;
    }
}

#ifdef USE_HW_EVENT
static bool __time_critical_func(t1_invoke)(struct hw_event *event) {
    VIA *v = (VIA *)event->user_data;
    t1_reached(v);
    return false;
}

static bool __time_critical_func(t2_invoke)(struct hw_event *event) {
    VIA *v = (VIA *)event->user_data;
    t2_reached(v);
    return false;
}

static bool __time_critical_func(sr_invoke)(struct hw_event *event) {
    // todo untested
    VIA *v = (VIA *)event->user_data;
    via_shift(v, (int)(get_hardware_timestamp() - event->user_time));
    return false;
}

#else
void via_poll(VIA *v, int cycles)
{
    v->t1c -= cycles;
    if (v->t1c < TLIMIT) {
        t1_reached(v);
    }
    if (!(v->acr & 0x20)) {
        v->t2c -= cycles;
        if (v->t2c < TLIMIT) {
            t2_reached(v);
        }
    }
    if (v->acr & 0x1c)
        via_shift(v, cycles);
}
#endif

void __time_critical_func(via_write)(VIA *v, uint16_t addr, uint8_t val)
{
        switch (addr&0xF)
        {
            case ORA:
                v->ifr &= ~INT_CA1;
                if ((v->pcr & 0xA) != 0x2) /*Not independent interrupt for CA2*/
                   v->ifr &= ~INT_CA2;
                via_updateIFR(v);

                if ((v->pcr & 0x0E) == 0x08) /*Handshake mode*/
                {
                        v->set_ca2(0);
                        v->ca2 = 0;
                }
                if ((v->pcr & 0x0E) == 0x0A) /*Pulse mode*/
                {
                        v->set_ca2(0);
                        v->set_ca2(1);
                        v->ca2 = 1;
                }
                // FALLTHROUGH

            case ORAnh:
                v->write_portA((val & v->ddra) | ~v->ddra);
                v->ora=val;
                break;

            case ORB:
                v->ifr &= ~INT_CB1;
                if ((v->pcr & 0xA0) != 0x20) /*Not independent interrupt for CB2*/
                   v->ifr &= ~INT_CB2;
                via_updateIFR(v);

                v->orb=val;
                val = (val & v->ddrb) | ~v->ddrb;
                if (v->acr & 0x80)
                    val = (val & 0x8f) | v->t1pb7;
                v->write_portB(val);

                if ((v->pcr & 0xE0) == 0x80) /*Handshake mode*/
                {
                        v->set_cb2(0);
                        v->cb2 = 0;
                }
                if ((v->pcr & 0xE0) == 0xA0) /*Pulse mode*/
                {
                        v->set_cb2(0);
                        v->set_cb2(1);
                        v->cb2 = 1;
                }
                break;

            case DDRA:
                v->ddra = val;
                v->write_portA((v->ora & v->ddra) | ~v->ddra);
                break;
            case DDRB:
                v->ddrb = val;
                val = (v->orb & val) | ~val; // val is now output data.
                if (v->acr & 0x80)
                    val = (val & 0x8f) | v->t1pb7;
                v->write_portB(val);
                break;
            case ACR:
                v->acr = val;
#ifdef USE_HW_EVENT
                if (0x20u & (v->acr & val)) {
                    int t2c = via_get_t2c(v);
                    if (val & 0x20) {
                        v->t2_stopped_at = t2c;
                        remove_hw_event(&v->timer2_event);
                    } else {
                        v->t2_stopped_at = -1;
                        via_set_t2c(v, t2c);
                        upsert_hw_event(&v->timer2_event);
                    }
                }
                if (0x1cu & (v->acr & val)) {
                    via_set_sr_count(v, v->sr_count);
                }
#endif
                break;
            case PCR:
                v->pcr  = val;
                log_debug("via: PCR write %04X %02X", addr, val);

                if ((val & 0xE) == 0xC)
                {
                        v->set_ca2(0);
                        v->ca2 = 0;
                }
                else if (val & 0x8)
                {
                        v->set_ca2(1);
                        v->ca2 = 1;
                }

                if ((val & 0xE0) == 0xC0)
                {
                        v->set_cb2(0);
                        v->cb2 = 0;
                }
                else if (val & 0x80)
                {
                        v->set_cb2(1);
                        v->cb2 = 1;
                }
                break;
            case SR:
                v->sr   = val;
                via_set_sr_count(v, 16);
                v->ifr &= ~0x04;
                break;
            case T1LL:
            case T1CL:
                v->t1l &= 0x1FE00;
                v->t1l |= (val<<1);
                break;
            case T1LH:
                v->t1l &= 0x1FE;
                v->t1l |= (val<<9);
                v->ifr &= ~INT_TIMER1;
                via_updateIFR(v);
                break;
            case T1CH:
                if ((v->acr & 0xC0) == 0x80)
                    v->t1pb7 = 0; /*Lower PB7 for one-shot timer*/
                v->t1l &= 0x1FE;
                v->t1l |= (val<<9);
                via_set_t1c(v, v->t1l + 1);
                v->t1hit = 0;
                v->ifr &= ~INT_TIMER1;
                via_updateIFR(v);
                break;
            case T2CL:
                v->t2l &= 0x1FE00;
                v->t2l |= (val << 1);
                break;
            case T2CH:
                /*Fix for Kevin Edwards protection - if interrupt triggers in cycle before write then let it run*/
                if ((via_get_t2c(v) == TLIMIT && (v->ier & INT_TIMER2)) ||
                    (v->ifr & v->ier & INT_TIMER2))
                {
                        interrupt_set_mask(128);
                }
                v->t2l &= 0x1FE;
                v->t2l |= (val << 9);
                via_set_t2c(v, v->t2l + 1);
                v->ifr &= ~INT_TIMER2;
                via_updateIFR(v);
                v->t2hit=0;
                break;
            case IER:
                if (val & 0x80)
                   v->ier |=  (val&0x7F);
                else
                   v->ier &= ~(val&0x7F);
                via_updateIFR(v);
                break;
            case IFR:
                v->ifr &= ~(val & 0x7F);
                via_updateIFR(v);
                break;
        }
}

uint8_t __time_critical_func(via_read)(VIA *v, uint16_t addr)
{
        uint8_t temp;
        switch (addr&0xF)
        {
            case ORA:
                v->ifr &= ~INT_CA1;
                if ((v->pcr & 0xA) != 0x2) /*Not independent interrupt for CA2*/
                   v->ifr &= ~INT_CA2;
                via_updateIFR(v);
            case ORAnh:
                temp=v->ora & v->ddra;
                if (v->acr & 1)
                   temp|=(v->ira          & ~v->ddra); /*Read latch*/
                else
                   temp|=(v->read_portA() & ~v->ddra); /*Read current port values*/
                return temp;

            case ORB:
                v->ifr &= ~INT_CB1;
                if ((v->pcr & 0xA0) != 0x20) /*Not independent interrupt for CB2*/
                   v->ifr &= ~INT_CB2;
                via_updateIFR(v);

                temp=v->orb & v->ddrb;
                if (v->acr & 2)
                   temp|=(v->irb          & ~v->ddrb); /*Read latch*/
                else { /*Read current port values*/
                    temp |= v->read_portB() & ~v->ddrb;
                    if (v->acr & 0x80)
                        temp = (temp & 0x7f) | v->t1pb7;
                }
                log_debug("via: addr=%04X, returning %02X for ORB", addr, temp);
                return temp;

            case DDRA:
                return v->ddra;

            case DDRB:
                return v->ddrb;

            case T1LL:
                return (v->t1l & 0x1FE) >> 1;

            case T1LH:
                return v->t1l >> 9;

            case T1CL: {
                v->ifr &= ~INT_TIMER1;
                via_updateIFR(v);
                int t1c = via_get_t1c(v);
                if (t1c < -1) return 0xFF; /*Return 0xFF during reload*/
                return ((t1c + 1) >> 1) & 0xFF;
            }
            case T1CH: {
                int t1c = via_get_t1c(v);
                if (t1c < -1) return 0xFF;   /*Return 0xFF during reload*/
                return (t1c + 1) >> 9;
            }
            case T2CL:
                v->ifr &= ~INT_TIMER2;
                via_updateIFR(v);
                return ((via_get_t2c(v) + 1) >> 1) & 0xFF;

            case T2CH:
                return (via_get_t2c(v)+1)>>9;

            case SR:
                return v->sr;

            case ACR:
                return v->acr;

            case PCR:
                return v->pcr;

            case IER:
                return v->ier | 0x80;

            case IFR:
                return v->ifr;
        }
        return 0xFE;
}

void __time_critical_func(via_set_ca1)(VIA *v, int level)
{
    if (level == v->ca1) return;
        if (((v->pcr & 0x01) && level) || (!(v->pcr & 0x01) && !level))
        {
                if (v->acr & 0x01) v->ira = v->read_portA(); /*Latch port A*/
                v->ifr |= INT_CA1;
                via_updateIFR(v);
                if ((v->pcr & 0x0C) == 0x08) /*Handshaking mode*/
                {
                        v->ca2 = 1;
                        v->set_ca2(1);
                }
        }
        v->ca1 = level;
}

void __time_critical_func(via_set_ca2)(VIA *v, int level)
{
        if (level == v->ca2) return;
        if (v->pcr & 0x08) return; /*Output mode*/
        if (((v->pcr & 0x04) && level) || (!(v->pcr & 0x04) && !level))
        {
                v->ifr |= INT_CA2;
                via_updateIFR(v);
        }
        v->ca2 = level;
}

void __time_critical_func(via_set_cb1)(VIA *v, int level)
{
        if (level == v->cb1) return;
        if (((v->pcr & 0x10) && level) || (!(v->pcr & 0x10) && !level))
        {
                if (v->acr & 0x02) { /*Latch port B*/
                    uint8_t data = v->read_portB() & ~v->ddrb;
                    if (v->acr & 0x80)
                        data = (data & 0x7f) | v->t1pb7;
                    v->irb = data;
                }
                v->ifr |= INT_CB1;
                via_updateIFR(v);
                if ((v->pcr & 0xC0) == 0x80) /*Handshaking mode*/
                {
                        v->cb2 = 1;
                        v->set_cb2(1);
                }
        }
        v->cb1 = level;
}

void __time_critical_func(via_set_cb2)(VIA *v, int level)
{
        if (level == v->cb2) return;
        if (v->pcr & 0x80) return; /*Output mode*/
        if (((v->pcr & 0x40) && level) || (!(v->pcr & 0x40) && !level))
        {
                v->ifr |= INT_CB2;
                via_updateIFR(v);
        }
        v->cb2 = level;
}

void __time_critical_func(via_shift)(VIA *v, int cycles) {
    int cb1;

    // todo if support is added for more modes, then via_set_sr_count must be updated
    if ((v->acr & 0x1c) == 0x18) {
        while (cycles--) {
            if (v->sr_count > 0) {
                via_set_sr_count(v, v->sr_count - 1);
                cb1 = !(v->sr_count & 0x01);
                if (cb1) {
                    v->set_cb2(v->sr >> 7);
                    v->sr = (v->sr << 1);
                }
                v->set_cb1(cb1);
            }
            if (v->sr_count <= 0) {
                v->ifr |= 0x04;
                via_updateIFR(v);
            }
        }
    }
}

static uint8_t __time_critical_func(via_read_null)()
{
        return 0xFF;
}

static void __time_critical_func(via_write_null)(uint8_t val)
{
}

static void __time_critical_func(via_set_null)(int level)
{
}

void via_reset(VIA *v)
{
        v->ora   = v->orb   = 0;
        v->ddra  = v->ddrb  = 0;
        v->ifr   = v->ier   = 0;
        v->t1pb7            = 0;
        v->t1l   = 0x1FFFE;
        v->t2l   = 0x1FFFE;
#ifdef USE_HW_EVENT
        v->timer1_event.invoke = t1_invoke;
        v->timer2_event.invoke = t2_invoke;
        v->sr_event.invoke = sr_invoke;
        v->timer1_event.user_data = v;
        v->timer2_event.user_data = v;
        v->sr_event.user_data = v;
        v->t2_stopped_at = -1; // starts running
#endif
        via_set_t1c(v, v->t1l);
        via_set_t2c(v, v->t2l);
        v->t1hit = v->t2hit = 1;
        v->acr   = v->pcr   = 0;

        v->read_portA  = v->read_portB  = via_read_null;
        v->write_portA = v->write_portB = via_write_null;

        v->set_ca1 = v->set_ca2 = v->set_cb1 = v->set_cb2 = via_set_null;
}

#ifndef NO_USE_SAVE_STATE
void via_savestate(VIA *v, FILE *f)
{
        putc(v->ora,f);
        putc(v->orb,f);
        putc(v->ira,f);
        putc(v->irb,f);
        putc(v->read_portA(),f);
        putc(v->read_portA(),f);
        putc(v->ddra,f);
        putc(v->ddrb,f);
        putc(v->sr,f);
        putc(v->acr,f);
        putc(v->pcr,f);
        putc(v->ifr,f);
        putc(v->ier,f);
        putc(v->t1l,f); putc(v->t1l>>8,f); putc(v->t1l>>16,f); putc(v->t1l>>24,f);
        putc(v->t2l,f); putc(v->t2l>>8,f); putc(v->t2l>>16,f); putc(v->t2l>>24,f);
        putc(v->t1c,f); putc(v->t1c>>8,f); putc(v->t1c>>16,f); putc(v->t1c>>24,f);
        putc(v->t2c,f); putc(v->t2c>>8,f); putc(v->t2c>>16,f); putc(v->t2c>>24,f);
        putc(v->t1hit,f);
        putc(v->t2hit,f);
        putc(v->ca1,f);
        putc(v->ca2,f);
}

void via_loadstate(VIA *v, FILE *f)
{
        v->ora=getc(f);
        v->orb=getc(f);
        v->ira=getc(f);
        v->irb=getc(f);
        /*v->porta=*/getc(f);
        /*v->portb=*/getc(f);
        v->ddra=getc(f);
        v->ddrb=getc(f);
        v->sr=getc(f);
        v->acr=getc(f);
        v->pcr=getc(f);
        v->ifr=getc(f);
        v->ier=getc(f);
        v->t1l=getc(f); v->t1l|=getc(f)<<8; v->t1l|=getc(f)<<16; v->t1l|=getc(f)<<24;
        v->t2l=getc(f); v->t2l|=getc(f)<<8; v->t2l|=getc(f)<<16; v->t2l|=getc(f)<<24;
        v->t1c=getc(f); v->t1c|=getc(f)<<8; v->t1c|=getc(f)<<16; v->t1c|=getc(f)<<24;
        v->t2c=getc(f); v->t2c|=getc(f)<<8; v->t2c|=getc(f)<<16; v->t2c|=getc(f)<<24;
        v->t1hit=getc(f);
        v->t2hit=getc(f);
        v->ca1=getc(f);
        v->ca2=getc(f);
}
#endif
