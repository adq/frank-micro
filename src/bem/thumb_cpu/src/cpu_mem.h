/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef MEM_H_INCLUDED
#define MEM_H_INCLUDED

#include "common.h"

#if THUMB_CPU_USE_ASM
typedef uint8_t (*MemReadFunction)( uint16_t addr, uint32_t op_code);
#else
typedef uint8_t (*MemReadFunction)( uint16_t addr );
#endif
typedef void (*MemWriteFunction)( uint16_t addr, uint8_t val );


typedef struct
{
    struct
    {
        union
        {
            MemReadFunction     function;
            const uint8_t*      ptr;
        };
#if !THUMB_CPU_USE_ASM
        uint8_t             isFunction;
#endif                
    } read;
    struct
    {
        union
        {
            MemWriteFunction     function;
            uint8_t*            ptr;
        };
#if !THUMB_CPU_USE_ASM
        uint8_t             isFunction;
#endif                
    } write;
} MemHandler;

#if THUMB_CPU_USE_ASM
static_assert(sizeof(MemHandler) == 8, "");
#endif

static uint8_t inline MemRead8Handler( MemHandler* p, uint16_t addr )
{
#if THUMB_CPU_USE_ASM
    if( ((uintptr_t)p->read.function ) & 1 )
    {
        // hack to avoid the ASM wrapper function - we only have one handler
        extern uint8_t CWrapMemReadFunctionFCFF(uint16_t addr);
        return CWrapMemReadFunctionFCFF(addr);
    }
#else
    if( p->read.isFunction )
    {
        return( p->read.function( addr ) );
    }
#endif
    else
    {
        return( p->read.ptr[ addr ] );
    }
}


static void inline MemWrite8Handler( MemHandler* p, uint16_t addr, uint8_t val )
{
#if THUMB_CPU_USE_ASM
    if( ((uintptr_t)p->write.function ) & 1 )
    {
        // hack to avoid the ASM wrapper function - we only have one handler
        extern void CWrapMemWriteFunctionFCFF(uint16_t addr, uint8_t val );
        CWrapMemWriteFunctionFCFF(addr, val);
    }
#else
    if( p->write.isFunction )
    {
        p->write.function( addr, val );
    }
#endif
    else
    {
        p->write.ptr[ addr ] = val;
    }
}

static inline MemHandler* MemHandlerGet( MemHandler* handlers, uint16_t blocksize, uint16_t base, uint16_t addr )
{
//    printf("base %04x addr %04x read %p write %p\n", base, addr, &handlers[(addr-base)/blocksize].read.ptr, &handlers[(addr-base)/blocksize].write.ptr);
    return( &handlers[ ( addr - base ) / blocksize ] );
}

static uint8_t inline MemRead8( MemHandler* handlers, uint16_t blocksize, uint16_t base, uint16_t addr )
{
    return( MemRead8Handler( MemHandlerGet( handlers, blocksize, base, addr ), addr ) );
}
static void inline MemWrite8( MemHandler* handlers, uint16_t blocksize, uint16_t base, uint16_t addr, uint8_t val )
{
    MemWrite8Handler( MemHandlerGet( handlers, blocksize, base, addr ), addr, val );
}


extern void MemHandlerSetReadFunction( MemHandler* p, MemReadFunction w );
extern void MemHandlerSetReadPtr( MemHandler* p, const uint8_t* ptr );
extern void MemHandlerSetWriteFunction( MemHandler* p, MemWriteFunction w );
extern void MemHandlerSetWritePtr( MemHandler* p, uint8_t* ptr );

extern MemHandler *MemMapInit();
extern void MemMapUpdatePC(uint32_t pc);

#endif // #ifndef MEM_H_INCLUDED
