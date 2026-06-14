/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#ifndef PI_ASM32
.cpu cortex-m0plus
.thumb
.macro public_func name
.type \name, %function
.thumb_func
.global \name
\name:
.endm
#else
.macro public_func name
.type \name, %function
.global \name
\name:
.endm
#endif
