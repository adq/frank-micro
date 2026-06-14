/*
 * B-em Pico version (C) 2021 Graham Sanderson
 */
#include "macros.S"
.syntax unified
.section .time_critical.pixels_@

public_func draw_pixels8x2x2
    push {r4, r5, r6, r7, lr}

    mov r12, r3
    lsrs r4, r1, #2
    bcc 1f
    subs r1, #2

    ldmia r1!, {r3, r4, r5, r6, r7}

    lsrs r3, #16
    lsls r1, r4, #16
    add r3, r1

    lsrs r4, #16
    lsls r1, r5, #16
    add r4, r1

    lsrs r5, #16
    lsls r1, r6, #16
    add r5, r1

    lsrs r6, #16
    lsls r7, #16
    add r6, r7

    stmia r0!, {r3, r4, r5, r6}

    lsrs r4, r2, #2
    bcc 3f

2: // unaligned 2
    subs r2, #2
    ldmia r2!, {r3, r4, r5, r6, r7}

    lsrs r3, #16
    lsls r2, r4, #16
    add r3, r2

    lsrs r4, #16
    lsls r2, r5, #16
    add r4, r2

    lsrs r5, #16
    lsls r2, r6, #16
    add r5, r2

    lsrs r6, #16
    lsls r7, #16
    add r6, r7

    stmia r0!, {r3, r4, r5, r6}
    b 4f

1: // aligned 1
    ldmia r1!, {r4, r5, r6, r7}
    stmia r0!, {r4, r5, r6, r7}
    lsrs r4, r2, #2
    bcs 2b

3: // aligned 2
    ldmia r2!, {r4, r5, r6, r7}
    stmia r0!, {r4, r5, r6, r7}

4:
    mov r0, r12
    ldr r1, [sp, #20]
    ldr r2, [sp, #24]

    lsrs r4, r1, #2
    bcc aligned1
    b unaligned1

public_func draw_pixels8x2
    push {r4, r5, r6, r7, lr}
    lsrs r4, r1, #2
    bcc aligned1

unaligned1:
    subs r1, #2
    ldmia r1!, {r3, r4, r5, r6, r7}

    lsrs r3, #16
    lsls r1, r4, #16
    add r3, r1

    lsrs r4, #16
    lsls r1, r5, #16
    add r4, r1

    lsrs r5, #16
    lsls r1, r6, #16
    add r5, r1

    lsrs r6, #16
    lsls r7, #16
    add r6, r7

    stmia r0!, {r3, r4, r5, r6}

    lsrs r4, r2, #2
    bcc aligned2

unaligned2:
    subs r2, #2
    ldmia r2!, {r3, r4, r5, r6, r7}

    lsrs r3, #16
    lsls r2, r4, #16
    add r3, r2

    lsrs r4, #16
    lsls r2, r5, #16
    add r4, r2

    lsrs r5, #16
    lsls r2, r6, #16
    add r5, r2

    lsrs r6, #16
    lsls r7, #16
    add r6, r7

    stmia r0!, {r3, r4, r5, r6}
    pop {r4, r5, r6, r7, pc}

aligned1:
    ldmia r1!, {r4, r5, r6, r7}
    stmia r0!, {r4, r5, r6, r7}
    lsrs r4, r2, #2
    bcs unaligned2

aligned2:
    ldmia r2!, {r4, r5, r6, r7}
    stmia r0!, {r4, r5, r6, r7}

    pop {r4, r5, r6, r7, pc}

