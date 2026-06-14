/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#ifndef B_EM_PICO_CPU_H_INCLUDED
#define B_EM_PICO_CPU_H_INCLUDED

#if THUMB_CPU_USE_ASM
#if PICO_ON_DEVICE
#include "hardware/interp.h"
#endif
#define assert_if_asm() assert(false);
#else
#define assert_if_asm() ((void)0)
#endif

#ifdef PICO_BUILD
#include "pico.h"
#else
#include "allegro5/allegro.h"
#include <assert.h>
#define __time_critical
#endif

//#include "../common.h"
#include "cpu_mem.h"

#define CPU_MEM_SIZE                (0x10000)           // 64K
#define CPU_MEM_BLOCKSIZE           (0x400)             // 1K

typedef struct C6502
{
    int32_t                 clk;
    uint16_t                pc;
    uint8_t                 a;
    uint8_t                 x;
    uint8_t                 y;
    uint8_t                 sp;
    uint8_t                 status;
    uint8_t                 pad[1];
    uint16_t                znSource;
    uint8_t                 interrupt; // note interrupt/nmi must be adjacent as they are reads as a 16 bit value by asm
    uint8_t                 nmi;
    uint8_t                 nvczFlags[ 16 ];
    uint8_t                 znFlags[ 257 ];
    uint8_t                 pad3[3];
    MemHandler              *memHandlers;
    uint32_t                clkAdjust;
    bool                    cli_breakout;
} C6502;


#define FLG_BIT_c           (0)
#define FLG_BIT_z           (1)
#define FLG_BIT_i           (2)
#define FLG_BIT_d           (3)
#define FLG_BIT_b           (4)
#define FLG_BIT_x           (5)
#define FLG_BIT_v           (6)
#define FLG_BIT_n           (7)
#define FLG_VAL( x ) ( 1 << FLG_BIT_ ## x )
#define FLG_SET(c6502,flg)  c6502.status |= ( 1 << FLG_BIT_ ## flg )
#define FLG_CLR(c6502,flg)  c6502.status &= ~( 1 << FLG_BIT_ ## flg )
#define FLG_ISSET(c6502,flg)      ( ( c6502.status >> ( FLG_BIT_ ## flg ) ) & 1 )
#define FLG_SETBOOL(c6502,flg,val) if(val) { FLG_SET(c6502,flg); } else { FLG_CLR(c6502,flg); }
#define FLG_COPY(c6502,flg,fromflg) if(FLG_ISSET(c6502,fromflg)) { FLG_SET(c6502,flg); } else { FLG_CLR(c6502,flg); }


extern void CPUInit();
extern void CPUReset( void );
extern void CPUNMI( bool c6512);
extern void CPURun( bool c6512);
extern void CPUSetPC( uint16_t pc );
extern void CPUIRQ( bool c6512 );

extern void polltime(int c);

extern C6502       g_cpu;



static inline uint8_t C6502_Read8( uint16_t addr )
{
//    printf( "READ8: %04x\n", addr );
    return( MemRead8( g_cpu.memHandlers, CPU_MEM_BLOCKSIZE, 0, addr ) );
}
static void C6502_Write8( uint16_t addr, uint8_t val )
{
//    printf( "WRITE8: %04x %02x\n", addr, val );
    MemWrite8( g_cpu.memHandlers, CPU_MEM_BLOCKSIZE, 0, addr, val );
}

static inline uint16_t C6502_Read16( uint16_t addr )
{
    uint16_t        r;
    r = C6502_Read8( addr + 1);
    r = ( r << 8 ) | C6502_Read8( addr );
    return( r );
}

static inline uint16_t C6502_Read16_0( uint16_t addr )
{
    uint16_t        r;
    r = C6502_Read8( ( addr + 1 ) & 0xff );
    r = ( r << 8 ) | C6502_Read8( addr );
    return( r );
}

static inline uint16_t C6502_Read16_P( uint16_t addr )
{
    uint16_t        r;
    uint16_t        b;
    b = (( addr + 1 ) & 0xff ) | ( addr & 0xff00 );
    r = C6502_Read8( b );
    r = ( r << 8 ) | C6502_Read8( addr );
    return( r );
}

static inline void C6502_Write16( uint16_t addr, uint16_t val )
{
    C6502_Write8( addr, val & 0xff );
    C6502_Write8( addr, val >> 8 );
}

static inline void C6502_Push8( uint8_t val )
{
    C6502_Write8( 0x100 | g_cpu.sp, val );
    g_cpu.sp--;
}

static inline void C6502_Push16( uint16_t val )
{
    C6502_Push8( val >> 8 );
    C6502_Push8( val & 0xff);
}

static inline uint8_t C6502_Pop( void )
{
    g_cpu.sp++;
    return( C6502_Read8( 0x100 | g_cpu.sp ) );
}

static inline uint16_t C6502_Pop16( void )
{
    uint16_t            r;
    r = C6502_Pop();
    r |= ((uint16_t)C6502_Pop()) << 8;
    return( r );
}

void CPUUpdatePC(uint16_t _pc);

#if THUMB_CPU_USE_ASM
bool CPUASMBreakout();
#endif

#endif // #ifndef CPU_H_INCLUDED
