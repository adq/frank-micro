/*
 * B-em Pico Version (C) 2021 Graham Sanderson
 */
#include "macros.S"
#if PI_ASM32
#ifndef MODEL_MASTER
.section .text.n6502_@ "ax"
#else
.section .text.c6512_@ "ax"
#endif
.arm
#else
#ifndef MODEL_MASTER
.section .time_critical.n6502_@ "ax"
#else
.section .time_critical.c6512_@ "ax"
#endif
#endif
.syntax unified

# A subroutine must preserve the contents of the registers r4-r8, r10, r11 and SP (and r9 in PCS variants that designate r9 as v6).

/*

Assumptions:
------------

The PC will never execute from function-controlled addresses (ie. mem-mapped HW)

The PC can only switch out of a contiguous buffer as the result of a
jmp/brk/rti/rts/nmi/etc... Thus REG_PC_MEM_BASE is fine as long as we adjust it after 
each of those. All 16-bit parameter reads are therefore also contiguous.

*/

/*
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
    uint8_t                 pad2[2];
    uint8_t                 nvczFlags[ 16 ];
    uint8_t                 znFlags[ 257 ];
    uint8_t                 pad3[3];
    MemHandler              memHandlers[ CPU_MEM_SIZE / CPU_MEM_BLOCKSIZE ];
} C6502;
*/

#define C6502_OFFSET_CLK            0
#define C6502_OFFSET_PC             4
#define C6502_OFFSET_A              6
#define C6502_OFFSET_X              7
#define C6502_OFFSET_Y              8
#define C6502_OFFSET_SP             9
#define C6502_OFFSET_STATUS         10
#define C6502_OFFSET_ZNSOURCE       12
#define C6502_OFFSET_NVCZFLAGS      16
#define C6502_OFFSET_ZNFLAGS        32

#define REG_SCRATCH_A               r0
#define REG_SCRATCH_B               r1
#define REG_SCRATCH_C               r2
#define REG_SCRATCH_D               r3
#define REG_OPADDR                  r4      // safe across function calls - store ADDRESS of operand in here
#if PI_ASM32
#define REG_PI_MEM_HANDLERS         r5
#else
#define REG_INTERP                  r5
#endif
#define REG_PC                      r6
#define REG_6502                    r7
#define REG_PC_MEM_BASE             r8      // cached base ptr for PC - updated whenever we jump
#define REG_STATUS                  r9
#define REG_CLK                     r10
#define REG_MEM_PAGE0               r11
#define REG_ACC                     r12

#if !PI_ASM32
#define INTERP_OFFSET_ACCUM0        0x00
#define INTERP_OFFSET_BASE0         0x08
#define INTERP_OFFSET_PEEK0         0x20
#endif

#define FLG_BIT_c                   (0)
#define FLG_BIT_z                   (1)
#define FLG_BIT_i                   (2)
#define FLG_BIT_d                   (3)
#define FLG_BIT_b                   (4)
#define FLG_BIT_x                   (5)
#define FLG_BIT_v                   (6)
#define FLG_BIT_n                   (7)


#define FLG_ZN 0x82

.macro update_status_zn
    mov r1, REG_STATUS
    movs r0, #(1 << FLG_BIT_z) | (1 << FLG_BIT_n)
    bics r1, r0
    ldrh r0, [REG_6502, #C6502_OFFSET_ZNSOURCE]
    adds r0, REG_6502
    adds r0, #4 // hacky
    ldrb r0, [r0, #C6502_OFFSET_ZNFLAGS - 4]
    orrs r1, r0
    mov REG_STATUS, r1
.endm

.macro update_znsource
        mov             r2, REG_STATUS
        movs            r1, #1
        movs            r0, #(1 << FLG_BIT_z) | (1 << FLG_BIT_n)
        ands            r0, r2
        beq             1f // N=0, Z=0 ==> r1 = 1
        lsls            r1, #7
        cmp             r0, #(1 << FLG_BIT_n)
        beq             1f // N=1, Z=0 ==> r1 = 128
        bhi             2f // N=1, Z=1 ==> r1 = 128 * 2 (below)
        subs            r1, r1 // // N=0, Z=1 ==> r1 = 0 * 2 (below)
2:
        adds            r1, r1
1:
        strh            r1, [ REG_6502, #C6502_OFFSET_ZNSOURCE]
.endm

/////////////////////////////////////////////////////////////////////////////
//
// 6502 test entry function
// extern void entry_6502( C6502* cpu );
//
/////////////////////////////////////////////////////////////////////////////

public_func entry_6502
            // save registers
            push            { r4-r7, lr }
            mov             r4,r8
            mov             r5,r9
            mov             r6,r10
            mov             r7,r11
            push            { r4-r7 }
            mov             r4,r12
            push            { r4 }


            // 6502
            mov             REG_6502,r0

            // MEM_PAGE0
            eors            r4, r4
#if PI_ASM32
            ldr             REG_PI_MEM_HANDLERS, =pi_mem_handler_base
            ldr             REG_PI_MEM_HANDLERS, [REG_PI_MEM_HANDLERS]
            lsrs            r4, #10
            ldr             r4, [REG_PI_MEM_HANDLERS, r4, LSL #3]
#else
            // INTERP
            ldr             REG_INTERP, interp0_addr
            str             r4, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]
            ldr             r4, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]
            ldr             r4, [ r4 ]
#endif
            mov             REG_MEM_PAGE0, r4

reloop:
            mov             r0, REG_6502
            // ACC
            ldrb            r4, [ r0, #C6502_OFFSET_A ]
            mov             REG_ACC, r4

            // CLK, PC
            ldr             r2, [ r0, #C6502_OFFSET_CLK ]
            mov             REG_CLK, r2
            ldrh            REG_PC, [r0, #C6502_OFFSET_PC]
            uxth            REG_PC, REG_PC

            // PC_MEM_BASE
#ifdef THUMB_CPU_BEEB
            lsrs            r4, REG_PC, #8
            cmp             r4, #0xff
            blt             1f
            ldr             r4, =fcff_ram_mapping
            b               2f
            .ltorg
#endif
1:
#if PI_ASM32
            lsrs            r4, REG_PC, #10
            add             r4, REG_PI_MEM_HANDLERS, r4, LSL #3
#else
            str             REG_PC, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]
            ldr             r4, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]
#endif
2:
            ldr             r4, [ r4 ]
            mov             REG_PC_MEM_BASE, r4

            // STATUS
            ldrb            r2, [ r0, #C6502_OFFSET_STATUS ]
            mov             REG_STATUS, r2
            update_znsource

            // MAIN LOOP!!!
main_loop:
// poor mans breakpoint
#if 0
            ldr         r0, foop
            cmp         r0, REG_PC
            bne 1f
            bkpt #0
            b 1f
.align 2
foop:
            .word 0x2f9c
            1:
#endif
            mov         r0, REG_CLK
            orrs        r0, r0
            bpl         main_loop_done
#ifdef PRINT_INSTRUCTIONS
            str         r0, [REG_6502, #C6502_OFFSET_CLK]
            mov         r0, r12
            push        {r0}
            update_status_zn

            strh        REG_PC, [REG_6502, #C6502_OFFSET_PC]
            mov         r2, REG_STATUS
            strb        r2, [REG_6502, #C6502_OFFSET_STATUS]
            mov         r2, REG_ACC
            strb        r2, [REG_6502, #C6502_OFFSET_A]
            bl          print_instructions
            pop         {r0}
            mov         r12, r0
#endif
            mov         r0, REG_PC
#if 0 // todo remove me
            lsrs        r1, r0, #16
            beq 1f
            bkpt #0
            1:
#endif
            add         r0, REG_PC_MEM_BASE
            ldrb        r1, [ r0 ]
            lsls        r1, r1, #2
#ifndef MODEL_MASTER
            adr         r2, opcode_jmp_table_n6502 // todo depends
#else
            adr         r2, opcode_jmp_table_c6512 // todo depends
#endif
            ldr         r1, [ r2, r1 ]
            bx          r1

main_loop_done:
            // write PC, CLK, STATUS, ACC back to C6502 structure
            strh            REG_PC, [ REG_6502, #C6502_OFFSET_PC ]
            mov             r4, REG_CLK
            str             r4, [ REG_6502, #C6502_OFFSET_CLK ]
            update_status_zn
            mov             r4, REG_STATUS
            strb            r4, [ REG_6502, #C6502_OFFSET_STATUS ]
            mov             r4, REG_ACC
            strb            r4, [ REG_6502, #C6502_OFFSET_A ]

#ifdef CPU_ASM_INLINE_BREAKOUT
            bl              CPUASMBreakout
            cmp             r0, #0
            beq             reloop
#endif

            // restore registers
            pop             { r4 }
            mov             r12,r4
            pop             { r4-r7 }
            mov             r8,r4
            mov             r9,r5
            mov             r10,r6
            mov             r11,r7
            pop             { r4-r7, pc }

.align 4

interp0_addr:       .word   0xd0000080

#ifndef MODEL_MASTER
public_func nmos_adc_decimal_guts
            mov       r4, REG_ACC
            lsls      r0, r4, #28
            lsrs      r0, r0, #28
            lsls      r2, r3, #4
            lsrs      r2, r2, #28
            lsrs      r1, r1, #FLG_BIT_c + 1
            adcs      r0, r2
            movs      r2, #0
            cmp       r0, #10
            bcc       1f
            // carry lo
            subs      r0, #10
            lsls      r0, #28
            lsrs      r0, #28
            movs      r2, #1
1:
            lsrs      r4, #4
            mvns      r4, r4
            lsls      r4, #28
            mvns      r4, r4
            lsrs      r3, #28
            lsls      r3, #28
            lsrs      r2, #1
            adcs      r4, r3
            mrs       r2, apsr
            lsrs      r2, #28
            adds      r2, REG_6502
            ldrb      r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]
            movs      r3, #0xc3 // NVZC
            lsls      r1, #1 // restore STATUS, note we lose carry bit but want to clear it anyway
            bics      r1, r3
            orrs      r2, r1
            movs      r3, #(1 << FLG_BIT_c)
            bics      r2, r3
            lsrs      r4, #28
            cmp       r4, #10
            bcc       1f
            // carry hi
            subs      r4, #10
            orrs      r2, r3
1:
            movs      r3, #(1 << FLG_BIT_z)
            bics      r2, r3
            ldrh      r1, [REG_6502, #C6502_OFFSET_ZNSOURCE]
            cmp       r1, #0
            bne       1f
            // binary result zero
            orrs      r2, r3
1:
            lsls      r4, #4
            orrs      r4, r0
            mov       REG_ACC, r4
            mov       REG_STATUS, r2
            update_znsource
            bx        lr


public_func nmos_sbc_decimal_guts

            mov       r4, REG_ACC
            lsls      r0, r4, #28
            lsrs      r0, r0, #28
            lsls      r2, r3, #4
            lsrs      r2, r2, #28
            lsrs      r1, r1, #FLG_BIT_c + 1
            sbcs      r0, r2
            asrs      r2, r0, #5
            bcc       1f
            // carry lo
            subs      r0, #6
            lsls      r0, #28
            lsrs      r0, #28
1:
            lsrs      r4, #4
            lsls      r4, #28
            lsrs      r3, #28
            lsls      r3, #28
            mvns      r2, r2
            lsrs      r2, #1
            sbcs      r4, r3
            mrs       r2, apsr
            lsrs      r2, #28
            adds      r2, REG_6502
            ldrb      r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]
            movs      r3, #0xc3 // NVZC
            lsls      r1, #1 // restore STATUS, note we lose carry bit but want to clear it anyway
            bics      r1, r3
            orrs      r1, r2
            movs      r3, #(1 << FLG_BIT_c)
            orrs      r1, r3
            lsrs      r2, #(FLG_BIT_c + 1)
            bcs       1f
            // no carry
            adds      r4, #10
            bics      r1, r3
1:
            movs      r3, #(1 << FLG_BIT_z)
            bics      r1, r3
            ldrh      r2, [REG_6502, #C6502_OFFSET_ZNSOURCE]
            cmp       r2, #0
            bne       1f
            # binary reuslt == zero
            orrs      r1, r3
1:
            lsrs      r4, #28
            lsls      r4, #4
            orrs      r4, r0
            mov       REG_ACC, r4
            mov       REG_STATUS, r1
            update_znsource
            bx        lr

#else
public_func cmos_adc_decimal_guts
        //uint resultl = (g_cpu.a & 0xfu) + (value & 0xfu)  + cval;
        //uint resulth = 0;
        //if (resultl > 9) {
        //   resultl -= 10;
        //   resultl &= 0xf;
        //   resulth = 1 << 4;
        //}

            // r4 = a
            // r3 = value
            lsls      r0, r4, #28
            lsrs      r0, r0, #28

            // r0 = a & 0xf

            lsrs      r4, #4
            mvns      r4, r4
            lsls      r4, #28
            mvns      r4, r4

            // r4 = (a &0xf0) << 24 + 0xffffff

            lsls      r2, r3, #28
            lsrs      r2, r2, #28
            lsrs      r3, #4
            lsls      r3, #28

            // r2 = value & 0xf
            // r3 = (value & 0xf0) << 24

            // note we are capturing c here, but we also use the shift value of status later
            lsrs      r1, r1, #FLG_BIT_c + 1
            adcs      r0, r2

            // r0 = resultl = (a & 0xfu) + (value & 0xfu)  + cval;
            cmp       r0, #10
            bcc       1f

            // carry lo.. note subs sets carry still
            subs      r0, #10
1:
            // r4 = (resulth (g_cpu.a & 0xf0u) + (value & 0xf0u) + (resultl carry)) << 24 (possibly plus 0xfffffff)
            adcs      r4, r3
            mrs r2, apsr

            lsrs r2, #28
            adds r2, REG_6502
            ldrb r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]

            // resulth << 4
            lsrs r4, #28
            lsls r4, #4

            // if we had actual carry jut subtract 100 (carray already set at this point)
            lsrs      r3, r2, #FLG_BIT_c + 1
            bcs       1f

            // otherwse clear carry flag and check for decimal carry
            movs      r3, #(1 << FLG_BIT_c)
            bics      r2, r3

            cmp       r4, #0xa0
            bcc       2f
            orrs      r2, r3
1:
            subs      r4, #0xa0
            uxtb      r4, r4
2:
            movs      r3, #0xc3 // NVZC
            lsls      r1, #1 // restore STATUS, note we lose carry bit but want to clear it anyway
            bics      r1, r3
            orrs      r2, r1

            // result = resulth | (resultl & 0xfu)
            lsls r0, #28
            lsrs r0, #28
            orrs r0, r4

        //resulth += (g_cpu.a & 0xf0u) + (value & 0xf0u);
        //if( resulth & 0x80 ) g_cpu.status |= FLG_VAL( n );
        //if (!((g_cpu.a ^ value) & 0x80) && ((g_cpu.a ^ resulth) & 0x80)) g_cpu.status |= FLG_VAL( v);
        //if (resulth > 0x90) {
        //   resulth -= 0xa0;
        //   g_cpu.status |= FLG_VAL( c);
        //}
            mov       REG_ACC, r0
            mov       REG_STATUS, r2
            strh      r0, [ REG_6502, #C6502_OFFSET_ZNSOURCE]
            bx        lr

public_func cmos_sbc_decimal_guts // this one mostly shared with non decimal as carries happen at the same time

        lsrs r0, r2, #(FLG_BIT_c + 1)
        bcs 1f
        subs r4, #0x60
1:
        mov r0, REG_ACC
        lsls r0, #28
        lsrs r0, #28
        lsls r3, #4
        lsrs r3, #28
        mov REG_ACC, r4
        lsrs r4, r1, #FLG_BIT_c + 1
        mov r4, REG_ACC // note his toesn't affect carry
        sbcs r0, r3
        bcs 1f
        subs r4, #6
1:
        bx lr

#endif
.align 2
/////////////////////////////////////////////////////////////////////////////
//
// generated code
//
/////////////////////////////////////////////////////////////////////////////

#ifdef MODEL_MASTER
.macro cmos_badop clks, pc
#if PI_ASM32
.section .text.c6512_asm_execute_cmos_badop_\clks\()_\pc\()
#else
.section .time_critical.c6512_asm_execute_cmos_badop_\clks\()_\pc\()
#endif
.global asm_execute_cmos_badop_\clks\()_\pc\()
asm_execute_cmos_badop_\clks\()_\pc\():
        adds      REG_PC, REG_PC, #\pc
        movs      r0, #\clks
        add       REG_CLK, r0
        bl        main_loop
.endm

cmos_badop 1, 1
cmos_badop 2, 2
cmos_badop 4, 2
cmos_badop 3, 2
cmos_badop 4, 3
cmos_badop 7, 3
#endif
#include "6502_asm.inl"

/////////////////////////////////////////////////////////////////////////////
//
//
//
/////////////////////////////////////////////////////////////////////////////

