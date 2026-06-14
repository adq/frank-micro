/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define CPU_MEM_SIZE                (0x10000)           // 64K
#define CPU_MEM_BLOCKSIZE           (0x400)             // 1K


typedef void (*write8Handler)( uint16_t addr, uint8_t value );
typedef uint8_t (*read8Handler)( uint16_t addr );

#ifndef count_of
#define count_of(a) (sizeof(a)/sizeof((a)[0]))
#endif

#endif // #ifndef COMMON_H_INCLUDED
