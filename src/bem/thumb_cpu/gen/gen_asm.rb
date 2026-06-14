#!/usr/bin/env ruby

#
# B-em Pico Version (C) 2021 Graham Sanderson
#

require 'erb'

#todo STZ timing is off
#
=begin
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
#define C6502_OFFSET_ZNFLAGS        12
#define C6502_OFFSET_CMPFLAGS       268

#define REG_SCRATCH_A               r0
#define REG_SCRATCH_B               r1
#define REG_SCRATCH_C               r2
#define REG_SCRATCH_D               r3
#define REG_OPADDR                  r4      // safe across function calls - store ADDRESS of operand in here
#define REG_INTERP                  r5
#define REG_PC                      r6
#define REG_6502                    r7
#define REG_PC_MEM_BASE             r8      // cached base ptr for PC - updated whenever we jump
#define REG_STATUS                  r9
#define REG_CLK                     r10
#define REG_MEM_PAGE0               r11
#define REG_ACC                     r12


Phases:

        Instruction fetch: r0 -> instruction in ARM address space
        Prefetch:  read from r0,
            r3 = operand value
            r4 = operand address (for writeback) if applicable
        [ execute opcode ]
            r2 = result, r4 = operand address (still)
            ( for V-flag check: r1=status, r0=prevA, r2=newA, r3=operand )
        FlagChecks:
            read r1 (status), r2 (result) [ r0 (previous), r3 (operand) ]
        Writeback:
            write r2 to r4 - can trash r0-r3


=end

CPUNAME = "g_cpu"
OUTPUT_C_FILE = ARGV[1] || "6502_c.inl"
OUTPUT_ASM_FILE = ARGV[0] || "6502_asm.inl"
PI_ASM = ARGV[2] || false

class Gen6502

    MODES = {
        :accum  =>  {
            :fmt        =>      "A",
            :bytes      =>      0,
            :cyc        =>      0,
        },
        :immed  =>  {
            :fmt        =>      "#V",
            :bytes      =>      1,
            :cyc        =>      2,
        },
        :page0  =>  {
            :fmt        =>      "$V",
            :bytes      =>      1,
            :cyc        =>      3,
        },
        :page0_x  =>  {
            :fmt        =>      "$V,x",
            :bytes      =>      1,
            :cyc        =>      4,
        },
        :page0_y  =>  {
            :fmt        =>      "$V,y",
            :bytes      =>      1,
            :cyc        =>      4,
        },
        :absolute  =>  {
            :fmt        =>      "$V",
            :bytes      =>      2,
            :cyc        =>      4,
        },
        :absolute_x =>  {
            :fmt        =>      "$V,x",
            :bytes      =>      2,
            :cyc        =>      4,
        },
        :absolute_x_cmos =>  {
            :fmt        =>      "$V,x",
            :bytes      =>      2,
            :cyc        =>      4,
        },
        :absolute_y =>  {
            :fmt        =>      "$V,y",
            :bytes      =>      2,
            :cyc        =>      4,
        },
        :indirect =>  {
            :fmt        =>      "($V)",
            :bytes      =>      1,
            :cyc        =>      5,
        },
        :indexed_indirect_x =>  {
            :fmt        =>      "($V,x)",
            :bytes      =>      1,
            :cyc        =>      6
        },
        :indexed_indirect_y =>  {
            :fmt        =>      "($V,y)",
            :bytes      =>      1,
            :cyc        =>      6
        },
        :indirect_indexed_x =>  {
            :fmt        =>      "($V),x",
            :bytes      =>      1,
            :cyc        =>      5
        },
        :indirect_indexed_y =>  {
            :fmt        =>      "($V),y",
            :bytes      =>      1,
            :cyc        =>      5
        },
        :rel =>  {
            :fmt        =>      "$V",
            :bytes      =>      1
        },
    }


    def initialize
        @processors = [:n6502, :c6512 ]
        @asm = []
        @post_asm = []
        @emitted = []

        @fussy_clocks = true
        @inst_fetch_options = {
            :assume_contig      =>  true,
            :assume_no_func     =>  true,
            :pc_direct_offset   =>  1
        }

        @out_asm = File.open( OUTPUT_ASM_FILE, "w" )
        @out_c = File.open( OUTPUT_C_FILE, "w" )

        emit_code("#pragma GCC diagnostic push")
        emit_code("#pragma GCC diagnostic ignored \"-Wunused-variable\"")

        [0, 1].each do |pass|
        @processors.each do |cpu|
            @insts = [ nil ] * 256

            if cpu == :n6502

                # undocumented crap
                insts = [ :slo, :rla, :sre, :rra, :sax, :lax, :dcp, :isb ]
                immed_insts = [ :anc, :anc, :alr, :arr, :xaa, :lax, :axs, :nmos_sbc ]
                modes = [ :indexed_indirect_x, :page0, :immed, :absolute, :indirect_indexed_y, :page0_x, :absolute_y, :absolute_x ]
                insts.each_with_index do |oinst,iidx|
                    modes.each_with_index do |mode,midx|
                        inst = oinst
                        next unless mode && inst
                        if( mode == :immed)
                            inst = immed_insts[iidx]
                        end
                       if( [ :lax, :sax ].include?( inst ) )
                           inst = {
                               [:sax, :indirect_indexed_y] => :ahx,
                               [:sax, :absolute_x] => :ahx,
                               [:sax, :absolute_y] => :tas,
                               [:lax, :absolute_y] => :las,
                           }[[inst, mode]] || inst

                            mode = {
                                :page0_x                 =>     :page0_y,
                                :absolute_x             =>      :absolute_y,
                            }[ mode ] || mode

                        end
                        byte = ( iidx << 5 ) | ( midx << 2 ) | 0x03
                        set_inst( byte, { :op => inst, :mode => mode } )
                    end
                end

            else

            end

            insts = [ :ora, :and, :eor, { :n6502 => :nmos_adc, :c6512 => :cmos_adc }[cpu],
                      :sta, :lda, :cmp, { :n6502 => :nmos_sbc, :c6512 => :cmos_sbc }[cpu] ]
            modes = [ :indexed_indirect_x, :page0, :immed, :absolute, :indirect_indexed_y, :page0_x, :absolute_y, :absolute_x ]
            insts.each_with_index do |inst,iidx|
                modes.each_with_index do |mode,midx|
                    next unless mode && inst
                    next if( inst == :sta && mode == :immed )
                    byte = ( iidx << 5 ) | ( midx << 2 ) | 0x01
                    set_inst( byte, { :op => inst, :mode => mode} )
                end
                if cpu == :c6512
                    byte = ( iidx << 5 ) | 0x12
                    set_inst( byte, { :op => inst, :mode => :indirect } )
                end
            end

            insts = [ :asl, :rol, :lsr, :ror, :stx, :ldx, :dec, :inc ]
            modes = [ :immed, :page0, :accum, :absolute, nil, :page0_x, nil, :absolute_x ]
            insts.each_with_index do |oinst,iidx|
                modes.each_with_index do |mode,midx|
                    inst = oinst
                    next unless( mode && inst )
                    next if( mode == :immed && inst != :ldx )
                    if( [ :ldx, :stx ].include?( inst ) )
                        mode = mode.to_s.gsub( "_x", "_y" ).to_sym
                    end
                    byte = ( iidx << 5 ) | ( midx << 2 ) | 0x02
                    if (cpu == :c6512 && mode == :absolute_x && [:asl, :rol, :lsr, :ror].include?(inst))
                        mode = :absolute_x_cmos
                    end
                    set_inst( byte, { :op => inst, :mode => mode } )
                end
            end

            insts = [ nil, :bit, :jmp, { :n6502 => :nmos_jmpi, :c6512 => :cmos_jmpi }[cpu], :sty, :ldy, :cpy, :cpx ]
            modes = [ :immed, :page0, nil, :absolute, nil, :page0_x, nil, :absolute_x ]
            insts.each_with_index do |oinst,iidx|
                modes.each_with_index do |mode,midx|
                    inst = oinst
                    next unless mode && inst

                    next if( mode == :page0 && [ :jmp, :nmos_jmpi, :cmos_jmpi ].include?( inst ) )
                    next if( mode == :immed && ![ :ldy, :cpy, :cpx ].include?( inst ) )
                    next if( mode == :page0_x && ![ :sty, :ldy ].include?( inst ) )
                    next if( mode == :absolute_x && ![ :ldy ].include?( inst ) )

                    byte = ( iidx << 5 ) | ( midx << 2 ) | 0x00
                    set_inst( byte, { :op => inst, :mode => mode } )
                end
                puts insts.inspect
            end

            insts = [ :bpl, :bmi, :bvc, :bvs, :bcc, :bcs, :bne, :beq ]
            insts.each_with_index do |inst,iidx|
                byte = ( iidx << 5 ) | 0x10
                set_inst( byte, { :meta_op => :branch, :op => inst, :mode => :rel, :flag => iidx/2, :set => (( iidx & 1 ) !=0 ) ? true : false } )
            end

            set_inst( 0x00, { :op => { :n6502 => :brk6502, :c6512 => :brk6512 }[cpu] } )
            set_inst( 0x20, { :op => :jsr, :mode => :absolute } )
            set_inst( 0x40, { :op => :rti } )
            set_inst( 0x60, { :op => :rts } )

            insts = [
                :php,   :clc,   :plp,   :sec,   :pha,   :cli,   :pla,   :sei,
                :dey,   :tya,   :tay,   :clv,   :iny,   :cld,   :inx,   :sed
            ]
            insts.each_with_index do |inst,idx|
                byte = 0x10 * idx + 0x08
                set_inst( byte, { :op => inst } )
            end
            insts = [
                :txa, :txs, :tax, :tsx, :dex, nil, :nop
            ]
            insts.each_with_index do |inst,idx|
                next if( !inst )
                byte = 0x10 * idx + 0x8A
                set_inst( byte, { :op => inst } )
            end

            if cpu == :c6512
                # remaining c6512 instructions are pretty random
                set_inst( 0x7c, { :op => :jmpix } )
                set_inst( 0x89, { :op => :bit, :mode => :immed } )
                set_inst( 0x34, { :op => :bit, :mode => :page0_x } )
                set_inst( 0x3c, { :op => :bit, :mode => :absolute_x } )
                set_inst( 0x04, { :op => :tsb, :mode => :page0 } )
                set_inst( 0x0c, { :op => :tsb, :mode => :absolute } )
                set_inst( 0x14, { :op => :trb, :mode => :page0 } )
                set_inst( 0x1c, { :op => :trb, :mode => :absolute } )
                set_inst( 0x64, { :op => :stz, :mode => :page0 } )
                set_inst( 0x9c, { :op => :stz, :mode => :absolute } )
                set_inst( 0x74, { :op => :stz, :mode => :page0_x } )
                set_inst( 0x9e, { :op => :stz, :mode => :absolute_x } )
                set_inst( 0x80, { :op => :bra, :mode => :rel} )
                set_inst( 0x1a, { :op => :ina } )
                set_inst( 0x3a, { :op => :dea } )
                set_inst( 0x5a, { :op => :phy } )
                set_inst( 0x7a, { :op => :ply } )
                set_inst( 0xda, { :op => :phx } )
                set_inst( 0xfa, { :op => :plx } )
            end

            # fill in missing instructions with nops that respect addr mode param ct
            256.times do |ii|
                next if( @insts[ ii ] )
                cc = ( ii >> 0 ) & 0x03
                bb = ( ii >> 2 ) & 0x07
                aa = ( ii >> 5 ) & 0x07
                if( cc == 0 )
                    modes = [ :immed, :page0, nil, :absolute, nil, :page0_x, nil, :absolute_x ]
                elsif( cc == 1 )
                    [ :indexed_indirect_x, :page0, :immed, :absolute, :indirect_indexed_y, :page0_x, :absolute_y, :absolute_x ]
                elsif( cc ==  2 )
                    modes = [ :immed, :page0, :accum, :absolute, nil, :page0_x, nil, :absolute_x ]
                else
                    # no fucking clue
                    [ :indexed_indirect_x, :page0, :immed, :absolute, :indirect_indexed_y, :page0_x, :absolute_y, :absolute_x ]
                end
                mode = modes[ bb ]
                badop = { :n6502 => :nmos_badop, :c6512 => :cmos_badop }[cpu]
                if( mode )
                    @insts[ ii ] = { :op => badop, :mode => mode }
                else
                    @insts[ ii ] = { :op => badop, :mode => :accum }
                end
            end

            ops = {}

            @insts.each_with_index do |inst,idx|
                if( inst )
                    ops[ inst[ :op ] ] = true
                end
            end

            emit_functions(cpu, pass)
        end
        end
        emit_code("#pragma GCC diagnostic pop")

        @out_asm.close
        @out_c.close()

    end

    def set_inst( byte, inst )
        if( @insts[ byte ] )
            printf( "Instruction 0x%02x already exists: #{@insts[ byte ].inspect}\n", byte )
            printf( "Replacing with #{inst.inspect}\n" )
        end
        @insts[ byte ] = inst
    end


    def gen_label()
        @label_id ||= 0
        @label_id += 1
        "label_#{@label_id}"
    end

    def post_asm( *args )
        @post_asm << sprintf( *args )
    end

    def emit_asm( *args )
        @asm << sprintf( *args )
    end

    def write_asm_line( asm )
        ss = ""
        if( asm[ 0..0 ] == "."  || asm[ 0..0 ] == "#")
            ss = asm
        elsif( asm[ 0..1 ] == "//" )
            ss = sprintf( "%40s%s", "", asm )
        else
            asm = asm.split
            if( asm.length > 0 )
                if( asm[ 0 ][ -1..-1 ] == ":" )
                    ss = sprintf( "%-40s", asm[ 0 ] )
                    asm = asm[ 1 .. -1 ]
                else
                    ss = sprintf( "%-40s", "" )
                end
            end
            if( asm.length > 0 )
                ss += sprintf( "%-10s", asm[ 0 ] )
                asm = asm[ 1 .. -1 ]
            end
            ss += asm.join( " " )
        end
        @out_asm.puts( ss )
    end

    def flush_asm()
        @asm.each do |l|
            write_asm_line( l )
        end
        @post_asm.each do |l|
            write_asm_line( l )
        end
        @asm = []
        @post_asm = []
    end

    ###############################################################################
    #
    # utility
    #
    ###############################################################################

    def emit_load_6502_reg( arm_reg, reg )
        if( reg == "a" )
            emit_asm( "mov #{arm_reg}, REG_ACC" )
        else
            emit_asm( "ldrb #{arm_reg}, [ REG_6502, #C6502_OFFSET_#{reg.upcase} ]" )
        end
    end
    def emit_store_6502_reg( arm_reg, reg )
        if( reg == "a" )
            emit_asm( "mov REG_ACC, #{arm_reg}" )
        else
            emit_asm( "strb #{arm_reg}, [ REG_6502, #C6502_OFFSET_#{reg.upcase} ]" )
        end
    end

    ###############################################################################
    #
    # memory read functions
    #
    ###############################################################################

    def emit_read8_pc_rel( out_reg, offset )
        raise "can't use r0 as scratch or out_reg" if( out_reg == "r0" )
        # emit_asm( "mov #{out_reg}, REG_PC" )
        # emit_asm( "add #{out_reg}, REG_PC_MEM_BASE" )
        # emit_asm( "ldrb #{out_reg}, [ #{out_reg}, ##{offset} ]")
        emit_asm( "ldrb #{out_reg}, [ r0, ##{offset} ]")
    end
    
    def emit_pc_changed( tmp0, tmp1)
        ## BEGIN BEEB SPECIFIC - FF is an executable page, but we map FC-00 as hardware - moving to 256
        ##                       byte pages would be costly on remapping
        emit_asm("#ifdef THUMB_CPU_BEEB")
        post_asm("#ifdef THUMB_CPU_BEEB")

        emit_asm( "lsrs #{tmp0}, REG_PC, #8")

        continue = gen_label()
        out_of_order = gen_label()

        emit_asm( "#ifndef MODEL_MASTER")
        emit_asm( "cmp #{tmp0}, #0xff")
        emit_asm( "bge #{out_of_order}")
        emit_asm( "#else")
        emit_asm( "cmp #{tmp0}, #0xc0") # note this doesn't help with Bplus (we need 0xa0 there)
        emit_asm( "bge #{out_of_order}")
        emit_asm( "#endif")

        post_asm( "#{out_of_order}:")
        ptr = gen_label()
        ptr2 = gen_label()
        post_asm( "#ifndef MODEL_MASTER")
        post_asm(" ldr #{tmp0}, #{ptr}")
        post_asm( "#else")
        post_asm( "cmp #{tmp0}, #0xff")
        isff = gen_label()
        post_asm( "bge #{isff}")
        post_asm( "ldr #{tmp1}, #{ptr2}")
        post_asm( "lsrs #{tmp0}, #4")
        post_asm( "lsls #{tmp0}, #2")
        post_asm( "ldr #{tmp0}, [#{tmp1}, #{tmp0}]")

        if (PI_ASM)
            post_asm( "mov REG_PI_MEM_HANDLERS, #{tmp0}")
            post_asm( "lsrs #{tmp0}, REG_PC, #10" )
            post_asm( "add #{tmp0}, REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3" )
        else
            post_asm( "str #{tmp0}, [REG_INTERP, #INTERP_OFFSET_BASE0]")
            post_asm( "str REG_PC, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            post_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
        end

        post_asm( "b #{continue}")
        post_asm( "#{isff}:")
        post_asm(" ldr #{tmp0}, #{ptr}")
        post_asm( "#endif")

        post_asm( "b #{continue}")
        post_asm( ".align 2")
        post_asm( "#{ptr}: .word fcff_ram_mapping")
        post_asm( "#ifdef MODEL_MASTER")
        post_asm( "#{ptr2}: .word cpu_memHandlers")
        post_asm( "#endif")

        emit_asm("#endif")
        post_asm("#endif")
        ## END BEEB SPECIFIC

#         emit_asm( "#ifdef MODEL_MASTER")
#         emit_asm( "ldr #{tmp0}, =RAMBank")
#         emit_asm(" ")
#         emit_asm( "#endif")

        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, REG_PC, #10" )
            emit_asm( "add #{tmp0}, REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3" )
        else
            emit_asm( "str REG_PC, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
        end
        emit_asm("#{continue}:")
        emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        emit_asm( "mov REG_PC_MEM_BASE, #{tmp0}")
    end

    def emit_read8_page0( in_reg, out_reg, tmp0 )
        emit_asm( "mov #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
    end

    def emit_read8_nofunc( in_reg, out_reg, tmp0 )
        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{in_reg}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{in_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
    end

    def emit_read8_full( in_reg, out_reg, tmp0, tmp1 )

        do_func = gen_label()
        continue = gen_label()

        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{in_reg}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{in_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "lsrs #{tmp1}, #{tmp0}, #1" )
        emit_asm( "bcs #{do_func}")
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "#{continue}:" )
        post_asm( "#{do_func}:" )
        raise "Balls" if( in_reg == "r0" )
        raise "Balls" if( out_reg == "r0" )

        if (true)
            if (PI_ASM)
                post_asm( "subs #{tmp0}, #1")
            end
            post_asm( "mov r0, REG_ACC" )
            post_asm( "push {r0}" )
            post_asm( "mov r0, REG_STATUS" )
            post_asm( "strb r0, [ REG_6502, #C6502_OFFSET_STATUS ]" )
            post_asm( "mov r0, REG_CLK" )
            post_asm( "str r0, [ REG_6502, #C6502_OFFSET_CLK ]" )
            post_asm( "mov r0, #{in_reg}" )
            post_asm( "mov #{tmp1}, REG_PC_MEM_BASE")
            post_asm( "ldrb r1, [#{tmp1}, REG_PC]")
            # this is a fixed function but we already have the ptr! and blx is smaller/cheaper
            post_asm( "blx #{tmp0}" )
            post_asm( "ldr #{tmp1}, [r7, #C6502_OFFSET_CLK]");
            post_asm( "mov REG_CLK, #{tmp1}");
            post_asm( "ldrb #{tmp1}, [ REG_6502, #C6502_OFFSET_STATUS]");
            post_asm( "mov REG_STATUS, #{tmp1}");
            post_asm( "mov #{out_reg}, r0" )
            post_asm( "pop {r0}" )
            post_asm( "mov REG_ACC, r0" )
            post_asm( "b #{continue}" )
        else
            # non beeb
            post_asm( "mov r0, REG_ACC" )
            post_asm( "push {r0}" )
            post_asm( "mov r0, #{in_reg}" ) if( in_reg != "r0" )
            post_asm( "blx #{tmp0}" )
            post_asm( "mov #{out_reg}, r0" ) if( out_reg != "r0" )
            post_asm( "pop {r0}" )
            post_asm( "mov REG_ACC, r0" )
            post_asm( "b #{continue}" )
        end
    end

    def emit_read16_full( in_reg, out_reg, tmp0, tmp1 )
        emit_asm("push {#{in_reg}}")
        emit_asm("adds #{in_reg}, #1")
        emit_read8_full( in_reg, out_reg, tmp0, tmp1)
        emit_asm("pop {#{in_reg}}")
        emit_asm("push {#{out_reg}}")
        emit_read8_full( in_reg, out_reg, tmp0, tmp1)
        emit_asm("pop {#{tmp1}}")
        emit_asm("lsls #{tmp1}, #8")
        emit_asm("orrs #{out_reg}, #{tmp1}")
    end

    def emit_read16_pc_rel( out_reg, offset, tmp0 )
        raise "can't use r0 as scratch or out_reg" if( out_reg == "r0" || tmp0 == "r0" )
        # emit_asm( "mov #{tmp0}, REG_PC" )
        # emit_asm( "add #{tmp0}, REG_PC_MEM_BASE" )
        # emit_asm( "ldrb #{out_reg}, [ #{tmp0}, ##{offset+1} ]")
        # emit_asm( "ldrb #{tmp0}, [ #{tmp0}, ##{offset} ]")
        # emit_asm( "lsls #{out_reg}, #8" )
        # emit_asm( "orrs #{out_reg}, #{tmp0}" )
        emit_asm( "ldrb #{out_reg}, [ r0, ##{offset+1} ]")
        emit_asm( "ldrb #{tmp0}, [ r0, ##{offset} ]")
        emit_asm( "lsls #{out_reg}, #8" )
        emit_asm( "orrs #{out_reg}, #{tmp0}" )
    end

    def emit_read16_page0( in_reg, out_reg, tmp0 )
        raise "todo: implement this" if( in_reg == out_reg )
        emit_asm( "mov #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "adds #{in_reg}, #{in_reg}, #1" )
        emit_asm( "uxtb #{in_reg}, #{in_reg}" )
        emit_asm( "ldrb #{tmp0}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "subs #{in_reg}, #{in_reg}, #1" )
        emit_asm( "uxtb #{in_reg}, #{in_reg}" )
        emit_asm( "lsls #{tmp0}, #8")
        emit_asm( "orrs #{out_reg}, #{tmp0}")
    end

    def emit_read16_contiguous_nofunc( in_reg, out_reg, tmp0 )
        raise "todo: implement this" if( in_reg == out_reg )
        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{in_reg}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{in_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "adds #{tmp0}, #1")
        emit_asm( "ldrb #{tmp0}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "lsls #{tmp0}, #8" )
        emit_asm( "orrs #{out_reg}, #{tmp0}" )
    end

    def emit_read16_nofunc( in_reg, out_reg, tmp0, tmp1 )
        raise "todo: implement this" if( in_reg == out_reg )
        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{in_reg}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{in_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )
        emit_asm( "adds #{tmp1}, #{in_reg}, #1" )
        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{tmp1}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{tmp1}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{tmp1}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "lsls #{tmp1}, #8 ")
        emit_asm( "orrs #{out_reg}, #{out_reg}, #{tmp1}" )
    end

    def emit_read16_nofunc_p( in_reg, out_reg, tmp0, tmp1 )
        raise "todo: implement this" if( in_reg == out_reg )

        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{in_reg}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{in_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{in_reg} ]" )

        emit_asm( "uxtb #{tmp0}, #{in_reg}" )
        emit_asm( "eors #{in_reg}, #{tmp0}" )
        emit_asm( "adds #{tmp0}, #1" )
        emit_asm( "uxtb #{tmp0}, #{tmp0}" )
        emit_asm( "adds #{tmp1}, #{tmp0}, #{in_reg}")

        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{tmp1}, #10" )
            emit_asm( "ldr #{tmp0}, [ REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ]" )
        else
            emit_asm( "str #{tmp1}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0} ]" )
        end
        emit_asm( "ldrb #{tmp1}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "lsls #{tmp1}, #8 ")
        emit_asm( "orrs #{out_reg}, #{out_reg}, #{tmp1}" )
    end

    ###############################################################################
    #
    # stack
    #
    ###############################################################################

    def emit_push8( in_reg, tmp0, tmp1 )
        emit_asm( "movs #{tmp0}, #0x80" )
        emit_asm( "lsls #{tmp0}, #1" )
        emit_asm( "ldrb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_asm( "orrs #{tmp1}, #{tmp0}" )
        emit_asm( "mov #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "strb #{in_reg}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "subs #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )        
        emit_asm( "strb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
    end

    def emit_pop8( out_reg, tmp0, tmp1 )
        emit_asm( "ldrb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_asm( "adds #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )        
        emit_asm( "strb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_asm( "movs #{tmp0}, #0x80" )
        emit_asm( "lsls #{tmp0}, #1" )
        emit_asm( "orrs #{tmp1}, #{tmp0}" )
        emit_asm( "mov #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{tmp1} ]" )
    end

    def emit_push16( in_reg, tmp0, tmp1, tmp2 )
        emit_asm( "movs #{tmp0}, #0x80" )
        emit_asm( "lsls #{tmp0}, #1" )
        emit_asm( "add #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "ldrb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_asm( "lsrs #{tmp2}, #{in_reg}, #8" )
        emit_asm( "strb #{tmp2}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "subs #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )
        emit_asm( "strb #{in_reg}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "subs #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )
        emit_asm( "strb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
    end

    def emit_pop16( out_reg, tmp0, tmp1, tmp2 )
        emit_asm( "movs #{tmp0}, #0x80" )
        emit_asm( "lsls #{tmp0}, #1" )
        emit_asm( "add #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "ldrb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_asm( "adds #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )
        emit_asm( "ldrb #{tmp2}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "adds #{tmp1}, #1" )
        emit_asm( "uxtb #{tmp1}, #{tmp1}" )
        emit_asm( "ldrb #{out_reg}, [ #{tmp0}, #{tmp1} ]" )
        emit_asm( "lsls #{out_reg}, #8" )
        emit_asm( "orrs #{out_reg}, #{tmp2}" )
        emit_asm( "strb #{tmp1}, [ REG_6502, #C6502_OFFSET_SP ]" )
    end


    ###############################################################################
    #
    # memory write functions
    #
    ###############################################################################

    def emit_write8_page0( addr_reg, val_reg, tmp0 )
        emit_asm( "mov #{tmp0}, REG_MEM_PAGE0" )
        emit_asm( "strb #{val_reg}, [ #{tmp0}, #{addr_reg} ]" )
    end

=begin
    Hmmm... this is only used to write r2->[r4]
    We have to use r0/r1 as params
    We're going to trash r2 anyway.
=end

    def emit_write8_full( addr_reg, val_reg, tmp0, tmp1 )

        raise "bad tmp0 reg" if( [ "r0", "r1" ].include?( tmp0 ) )

        do_func = gen_label()
        continue = gen_label()
        if (PI_ASM)
            emit_asm( "lsrs #{tmp0}, #{addr_reg}, #10" )
            emit_asm( "add #{tmp1}, REG_PI_MEM_HANDLERS, #{tmp0}, LSL #3 ")
            emit_asm( "ldr #{tmp0}, [ #{tmp1}, #4 ]" )
        else
            emit_asm( "str #{addr_reg}, [ REG_INTERP, #INTERP_OFFSET_ACCUM0 ]" )
            emit_asm( "ldr #{tmp0}, [ REG_INTERP, #INTERP_OFFSET_PEEK0 ]" )
            emit_asm( "ldr #{tmp0}, [ #{tmp0}, #4 ]" )
        end
        emit_asm( "lsrs #{tmp1}, #{tmp0}, #1" )
        emit_asm( "bcs #{do_func}")
        emit_asm( "strb #{val_reg}, [ #{tmp0}, #{addr_reg} ]" )       
        emit_asm( "#{continue}:" )
        post_asm( "#{do_func}:" )
        if (PI_ASM)
            post_asm( "subs #{tmp0}, #1")
        end
        raise "balls" if( addr_reg == "r0" )
        post_asm( "mov r0, REG_ACC" )
        post_asm( "push {r0}" )
        post_asm( "mov r0, #{addr_reg}" ) if( addr_reg != "r0" )
        post_asm( "mov r1, #{val_reg}" ) if( val_reg != "r1" )
        post_asm( "blx #{tmp0}" )
        post_asm( "pop {r0}" )
        post_asm( "mov REG_ACC, r0" )
        post_asm( "b #{continue}" )
    end

    ###############################################################################
    #
    # instruction and operand fetch functions
    #
    # BEFORE:
    #     r0 = ptr to instruction (ARM mem, not 6502)
    #
    # AFTER:
    #     r3 = operand value
    #     r4 = operand address (for writeback) if applicable
    #
    ###############################################################################

    def emit_prefetch_immed( op, read )
        emit_code( "    uint8_t value = C6502_Read8( #{CPUNAME}.pc + 1 );\n" )

        emit_read8_pc_rel( "r3", 1 )
    end

    def emit_prefetch_page0( op, read  )
        emit_code( "    uint16_t address = C6502_Read8( #{CPUNAME}.pc + 1 );\n" )

        emit_read8_pc_rel( "r4", 1 )

        if( read && op != :badop )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_page0( "r4" ,"r3", "r2" )
        end
    end

    def emit_prefetch_page0_xy( xy, op, read  )
        emit_code( "    uint16_t address = ( C6502_Read8( #{CPUNAME}.pc + 1 ) + #{CPUNAME}.#{xy} );\n" )
        emit_code( "    address &= 0xff;\n" )

        emit_read8_pc_rel( "r4", 1 )
        emit_load_6502_reg( "r1", xy )
        emit_asm( "add r4, r1" )
        emit_asm( "uxtb r4, r4" )

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_page0( "r4" ,"r3", "r2" )
        end
    end

    def emit_prefetch_page0_x( op, read )
        emit_prefetch_page0_xy( "x", op, read )
    end

    def emit_prefetch_page0_y( op, read  )
        emit_prefetch_page0_xy( "y", op, read )
    end

    def emit_prefetch_absolute( op, read  )
        emit_code( "    uint16_t address = C6502_Read16( #{CPUNAME}.pc + 1 );\n" )

        emit_read16_pc_rel( "r4", 1, "r3" )

        if( read )
            emit_read8_full( "r4", "r3", "r2", "r1" )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
        end
    end

    def emit_prefetch_absolute_xy( xy, op, read )
        @clks += 1 if( [ :slo, :rla, :rra, :dcp, :isb, :sre, :sta, :stz, :lsr, :asl, :ror, :rol, :inc, :dec ].include?( op ) )
        emit_code( "    uint16_t address2 = C6502_Read16( #{CPUNAME}.pc + 1 );\n" )
        emit_code( "    uint16_t address = address2 + #{CPUNAME}.#{xy};\n" )

        emit_read16_pc_rel( "r2", 1, "r3" )
        emit_load_6502_reg( "r1", xy )
        emit_asm( "adds r4, r2, r1" )
        emit_asm( "uxth r4, r4" )
        if( @fussy_clocks )
            if( [:lda,:ldx].include?( op ) )
                emit_code( "    if( (address2&0xff00) != (address&0xff00) ) #{CPUNAME}.clk++;\n" )
                emit_asm( "eors r2, r4")
                emit_asm( "lsrs r2, #8")
                no_boundary = gen_label()
                emit_asm( "beq #{no_boundary}")
                emit_asm( "movs r2, #1")
                emit_asm( "add REG_CLK, r2")
                emit_asm( "#{no_boundary}:")
            end
        end

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_full( "r4", "r3", "r2", "r1" ) if( read )
        end
    end

    def emit_prefetch_absolute_xy_cmos( xy, op, read )
        @clks += 1 if( [ :slo, :rla, :rra, :dcp, :isb, :sre, :sta, :stz, :inc, :dec ].include?( op ) )
        emit_code( "    uint16_t address2 = C6502_Read16( #{CPUNAME}.pc + 1 );\n" )
        emit_code( "    uint16_t address = address2 + #{CPUNAME}.#{xy};\n" )

        emit_read16_pc_rel( "r2", 1, "r3" )
        emit_load_6502_reg( "r1", xy )
        emit_asm( "adds r4, r2, r1" )
        emit_asm( "uxth r4, r4" )
        if( @fussy_clocks )
            if( [:lda, :ldx, :lsr, :asl, :ror, :rol].include?( op ) )
                emit_code( "    if( (address2&0xff00) != (address&0xff00) ) #{CPUNAME}.clk++;\n" )
                emit_asm( "eors r2, r4")
                emit_asm( "lsrs r2, #8")
                no_boundary = gen_label()
                emit_asm( "beq #{no_boundary}")
                emit_asm( "movs r2, #1")
                emit_asm( "add REG_CLK, r2")
                emit_asm( "#{no_boundary}:")
            end
        end

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_full( "r4", "r3", "r2", "r1" ) if( read )
        end
    end

    def emit_prefetch_absolute_x( op, read )
        emit_prefetch_absolute_xy( "x", op, read )
    end
    
    def emit_prefetch_absolute_x_cmos( op, read )
        emit_prefetch_absolute_xy_cmos( "x", op, read )
    end

    def emit_prefetch_absolute_y( op, read )
        emit_prefetch_absolute_xy( "y", op, read )
    end

    def emit_prefetch_indirect( op, read )
        emit_code( "    uint16_t address = C6502_Read8( #{CPUNAME}.pc + 1 );\n" )
        emit_code( "    address = C6502_Read16_0( address );\n")

        emit_read8_pc_rel( "r2", 1 )
        emit_read16_page0( "r2", "r4", "r1" )

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_full( "r4", "r3", "r2", "r1" )
        end
        #emit_asm("bkpt #0") # check
    end

    def emit_prefetch_indexed_indirect_xy( xy, op, read )
        emit_code( "    uint16_t address = ( C6502_Read8( #{CPUNAME}.pc + 1 ) + #{CPUNAME}.#{xy} ) & 0xff;\n" )
        emit_code( "    address = C6502_Read16_0( address );\n")

        emit_read8_pc_rel( "r2", 1 )
        emit_load_6502_reg( "r1", xy )
        emit_asm( "add r2, r1" )
        emit_asm( "uxtb r2, r2" )
        emit_read16_page0( "r2", "r4", "r1" )

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_full( "r4", "r3", "r2", "r1" )
        end
    end

    def emit_prefetch_indexed_indirect_x( op, read )
        emit_prefetch_indexed_indirect_xy( "x", op, read )
    end

    def emit_prefetch_indexed_indirect_y( op, read )
        emit_prefetch_indexed_indirect_xy( "y", op, read )
    end

    def emit_prefetch_indirect_indexed_xy( xy, op, read )
        @clks += 1 if( [ :rla, :rra, :slo, :sre, :isb, :dcp, :sta ].include?( op ) )
        emit_code( "    uint16_t address = ( C6502_Read8( #{CPUNAME}.pc + 1 ) );\n" )
        emit_code( "    uint16_t address2 = C6502_Read16_0( address );")
        emit_code( "    address = address2 + #{CPUNAME}.#{xy};" )

        emit_read8_pc_rel( "r3", 1 )
        emit_read16_page0( "r3", "r2", "r1" )
        emit_load_6502_reg( "r1", xy )
        emit_asm( "adds r4, r2, r1" )
        emit_asm( "uxth r4, r4" )

        if( @fussy_clocks )
            if( [:lda, :cmp].include?( op ) )
                emit_code( "    if( (address2&0xff00) != (address&0xff00) ) #{CPUNAME}.clk++;\n" )
                emit_asm( "eors r2, r4")
                emit_asm( "lsrs r2, #8")
                no_boundary = gen_label()
                emit_asm( "beq #{no_boundary}")
                emit_asm( "movs r2, #1")
                emit_asm( "add REG_CLK, r2")
                emit_asm( "#{no_boundary}:")
            end
        end

        if( read )
            emit_code( "    uint8_t value = C6502_Read8( address );" )
            emit_read8_full( "r4", "r3", "r2", "r1" )
        end
    end

    def emit_prefetch_indirect_indexed_x( op, read )
        emit_prefetch_indirect_indexed_xy( "x", op, read )
    end
    
    def emit_prefetch_indirect_indexed_y( op, read )
        emit_prefetch_indirect_indexed_xy( "y", op, read )
    end

    def emit_prefetch_rel( op, read )
        emit_code( "    int8_t offset = ( C6502_Read8( #{CPUNAME}.pc + 1 ) );\n" )
        emit_code( "    uint16_t address = ( #{CPUNAME}.pc + 2 + offset ) & 0xffff;")

        emit_read8_pc_rel( "r2", 1 )
        emit_asm( "sxtb r2, r2" )
        emit_asm( "add r2, r2, REG_PC" )
        emit_asm( "adds r2, r2, #2" )
        emit_asm( "uxth r4, r2" )
        
        # no operand fetch - this is just for jumps
    end

    def emit_prefetch_accum( op, read )
        if( op != :badop )
            emit_code( "    uint8_t value = #{CPUNAME}.a;\n" )
        end
        @clks += 2 if( [ :badop ].include?( op ) )

        emit_load_6502_reg( "r3", "a" )
    end

    def emit_prefetch( mode, op, read = true )
        read = false if( op == :badop )
        send( "emit_prefetch_#{mode}", op, read )
    end

    def emit_epilog()
        # emit_asm( "b main_loop" )
       emit_asm( "bl main_loop" )       # need bl for range. Don't care about LR.
    end

    def emit_functions(cpu, pass)
        table = []
        table2 = []
        table3 = []


        if (pass == 0)
            if (cpu == :n6502)
                emit_asm("#ifndef MODEL_MASTER")
            else
                emit_asm("#ifdef MODEL_MASTER")
            end
            # @ is alphabeticallty first, since it must be before the functions
            if (PI_ASM)
                emit_asm( ".section .text.#{cpu}_@") # want gc_sections to work ..
            else
                emit_asm( ".section .time_critical.#{cpu}_@") # want gc_sections to work ..
            end
            emit_asm( "opcode_jmp_table_#{cpu}:" )
            @insts.each_with_index do |inst,idx|
                mode = inst[ :mode ]
                rop = inst[ :op ]
                op = inst[ :meta_op ] || inst[ :op ]
                fname = [ rop, mode ].compact.map { |x| x.to_s }.join( "_" )
                if (op == :cmos_badop)
                    case idx & 0xf
                        when 2
                          clks = 2
                          args = 1
                        when 3, 7, 0xb, 0xf
                          clks = 1
                          args = 0
                        when 4
                          clks = idx == 0x44 ? 4 : 3
                          args = 1
                        when 0xc
                          clks = idx == 0x5c ? 7 : 4
                          args = 2
                        else
                          raise sprintf( "unexpected badop %02x", idx)
                    end
                    if (PI_ASM)
                        emit_asm( ".word #{ sprintf( "asm_execute_cmos_badop_%d_%d", clks, args + 1 ) }" )
                    else
                        emit_asm( ".word #{ sprintf( "asm_execute_cmos_badop_%d_%d + 1", clks, args + 1 ) }" )
                    end
                else
                    if (PI_ASM)
                        emit_asm( ".word #{ sprintf( "asm_execute_%02x_#{fname}", idx ) }" )
                    else
                        emit_asm( ".word #{ sprintf( "asm_execute_%02x_#{fname} + 1", idx ) }" )
                    end
                end
            end
            emit_asm("#endif")
        else

        @insts.each_with_index do |inst,idx|
            if( !inst )
                raise "undefined inst #{inst.inspect}"
            end

#            next unless idx == 0x01

            mode = inst[ :mode ]
            rop = inst[ :op ]
            op = inst[ :meta_op ] || inst[ :op ]
            fname = [ rop, mode ].compact.map { |x| x.to_s }.join( "_" )

            table << sprintf( "execute_%02X_#{fname}", idx )
            table2 << sprintf( "#{fname}", idx )


            key = sprintf( "%02X_#{fname}", idx )
            next unless @emitted.index(key) == nil
            @emitted << key

            emit_code( "void execute_%02X_#{fname}( void )", idx, fname )
            emit_code( "{" )

            if (PI_ASM)
                emit_asm( ".section .text.#{cpu}_#{fname}") # want gc_sections to work
            else
                emit_asm( ".section .time_critical.#{cpu}_#{fname}") # want gc_sections to work
            end
            emit_asm( ".global asm_execute_%02x_#{fname}", idx )
            emit_asm( "asm_execute_%02x_#{fname}:", idx )
            
            @status_in_r1 = false
            @clks = ( MODES[ mode ] || {} )[ :cyc ] || 2

            method_name = "emit_#{op}"
            @no_pc = false
            if( respond_to?( method_name ) )
                #table3 << sprintf( "asm_execute_%02x_#{fname}", idx )
                send( method_name, op, mode, inst )
                if (op == :nmos_badop)
                    emit_code(sprintf("printf(\"BADOP: %02x\\n\");", idx))
                end
                if (op == :cmos_badop)
                   # emit_code(sprintf("printf(\"BADOP: %02x\\n\");", idx))
                    if (cpu == :c6512)
                        case idx & 0xf
                        when 2
                            @clks = 2
                            args = 1
                        when 3, 7, 0xb, 0xf
                            @clks = 1
                            args = 0
                        when 4
                            @clks = idx == 0x44 ? 4 : 3
                            args = 1
                        when 0xc
                            @clks = idx == 0x5c ? 7 : 4
                            args = 2
                        else
                            raise sprintf( "unexpected badop %02x", idx)
                        end
                        @no_pc = true
                        emit_advance_pc_c(op, mode, 1 + args)
                        emit_advance_pc_asm(op, mode, 1 + args)
                    end
                end
                if( @clks )
                    emit_code( "    #{CPUNAME}.clk += #{@clks};" )
                    emit_asm( "movs r0, ##{@clks}")
                    emit_asm( "add REG_CLK, r0" )
                end
            else
                raise "undefined method #{method_name}"
            end
            if( !@no_pc )
                emit_advance_pc_c( op, mode )
                emit_advance_pc_asm( op, mode )
            end
            emit_code( "}" )

            emit_epilog()

            if( @asm.length > 0 )
                flush_asm()
            end

        end



        emit_code( "" )
        emit_code( "void (*g_optable_#{cpu}[])(void)={" )
        table.each do |func|
            emit_code( "  #{func},")
        end
        emit_code( "};" )
        emit_code( "" )
        emit_code( "const char* g_opnames_#{cpu}[]={" )
        table2.each do |func|
            emit_code( "  \"#{func}\",")
        end
        emit_code( "};" )

        end
        flush_asm()

    end

    def emit_code( *args )
        s = sprintf( *args )
        @out_c.puts s
    end

    def emit_advance_pc_c( op, mode, force_bytes = nil )
        bytes = force_bytes
        bytes ||= MODES[ mode ] && MODES[ mode ][ :bytes ] + 1
        bytes ||= 1
        if( bytes > 0 )
            emit_code( "    #{CPUNAME}.pc = ( #{CPUNAME}.pc + %d );\n", bytes );
        end
    end

    def emit_advance_pc_asm( op, mode, force_bytes = nil )
        bytes = force_bytes
        bytes ||= MODES[ mode ] && MODES[ mode ][ :bytes ] + 1
        bytes ||= 1
        if( bytes > 0 )
            emit_asm( "adds REG_PC, REG_PC, ##{bytes}")
        end
    end

    ###############################################################################
    #
    # flag checks
    #
    # r2 holds computation result (or r3 for really simple ops)
    # r3 holds temporary status flag (V checks only)
    # r1 holds previous value (V checks only)
    #
    ###############################################################################

    def emit_zn_flag_check( opts = {} )
        opts[ :reg ] ||= "r2"    
        var = opts[ :var ] || "#{CPUNAME}.a"
        emit_code( "    if( #{var} & 0xff ) { FLG_CLR( #{CPUNAME}, z ); } else { FLG_SET( #{CPUNAME}, z ); }" )
        emit_code( "    if( #{var} & 0x80 ) { FLG_SET( #{CPUNAME}, n ); } else { FLG_CLR( #{CPUNAME}, n ); }" )

        emit_asm( "mov REG_STATUS, r1" ) if( @status_in_r1 )
        # we know this is zero in upper bits
        emit_asm( "strh #{opts[:reg]}, [ REG_6502, #C6502_OFFSET_ZNSOURCE ]" )
    end

    ###############################################################################
    #
    # write result back to memory - r2 = value, r4 = address
    #
    ################################{sprintf( "0x%02x", bm ) }################################################

    def emit_writeback( op, mode )

        if( mode == :accum )

            emit_code( "    #{CPUNAME}.a = value;")
            emit_store_6502_reg( "r2", "a" )

        else
            emit_code( "    C6502_Write8( address, value );")

            if( mode == :page0 || mode == :page0_x || mode == :page0_y )
                emit_write8_page0( "r4", "r2", "r0" )
            else
                emit_write8_full( "r4", "r2", "r3", "r0" )
            end
        end
        @clks += 2
    end

    ###############################################################################
    #
    # actual opcodes
    #
    ###############################################################################

    def emit_compare( reg )

        emit_code( "    if( #{CPUNAME}.#{reg} < value ) { FLG_CLR( #{CPUNAME}, c ); FLG_CLR( #{CPUNAME}, z ); }" )
        emit_code( "    else if( #{CPUNAME}.#{reg} == value ) { FLG_SET( #{CPUNAME}, c ); FLG_SET( #{CPUNAME}, z ); }" )
        emit_code( "    else if( #{CPUNAME}.#{reg} > value ) { FLG_SET( #{CPUNAME}, c ); FLG_CLR( #{CPUNAME}, z ); }" )
        emit_code( "    if( ( #{CPUNAME}.#{reg} - value ) & 0x80 ) { FLG_SET( #{CPUNAME}, n ); } else { FLG_CLR( #{CPUNAME}, n ); }" )

        ll = gen_label()

        # TODO: use second interpolator?
        emit_load_6502_reg( "r2", reg )
        emit_asm( "subs r2, r3" )
        emit_asm( "uxtb r2, r2" )
        emit_asm( "strh r2, [REG_6502, #C6502_OFFSET_ZNSOURCE]")
        emit_asm( "sbcs r0, r0");
        emit_asm( "adds r0, #1");
        emit_asm( "movs r1, #0x83" )            # clear CZN
        emit_asm( "mov r3, REG_STATUS" )
        emit_asm( "bics r3, r1" )
        emit_asm( "orrs r3, r0" )
        emit_asm( "mov REG_STATUS, r3" )

    end

    def emit_branch( op, mode, inst )
        flags = "nvcz"
        flag = flags[ inst[ :flag ] .. inst[ :flag ] ]
        set = inst[ :set ]
        emit_prefetch( mode, op, false )

        emit_code( "    if( #{ set ? "" : "!" }FLG_ISSET(#{CPUNAME}, #{flag} ) ) {")
        emit_code( "        #{CPUNAME}.clk += #{@clks+1};")
        if( @fussy_clocks )
            emit_code( "    if( (address&0xff00) != ((#{CPUNAME}.pc+2)&0xff00) ) #{CPUNAME}.clk++;\n" )
        end
        emit_code( "        #{CPUNAME}.pc = address;" )
        emit_code( "    } else { " )
        emit_code( "        #{CPUNAME}.clk += #{@clks};")
        emit_advance_pc_c( op, mode )
        emit_code( "    }" )

        label_skip = gen_label()
        label_cont = gen_label()
        if flag == "v" || flag == "c"
            emit_asm( "mov r0, REG_STATUS" )
            emit_asm( "movs r1, ##{ { "n" => 0x80, "v" => 0x40, "c" => 0x01, "z" => 0x02 }[ flag ] }")
            emit_asm( "tst r0, r1" )
            emit_asm( "#{ set ? "beq" : "bne" } #{label_skip}")
        elsif flag == "z"
            emit_asm( "ldrh r0, [REG_6502, #C6502_OFFSET_ZNSOURCE]" )
            emit_asm( "uxtb r0, r0" )
            emit_asm( "cmp r0, #0" )
            emit_asm( "#{ set ? "bne" : "beq" } #{label_skip}")
        else
            emit_asm( "ldrh r0, [REG_6502, #C6502_OFFSET_ZNSOURCE]" )
            emit_asm( "cmp r0, #128" )
            emit_asm( "#{ set ? "blo" : "bhs" } #{label_skip}")
        end
        emit_asm( "mov r1, REG_PC")
        emit_asm( "mov REG_PC, r4" )
        emit_asm( "movs r0, ##{@clks+1}")
        emit_asm( "adds r1, #2")
        # not overflow from 16 bit here only affets a branch instruction at the end of RAM which is the IRQ
        # vector, but doesn't break anyway
        emit_asm( "eors r1, r4")
        emit_asm( "lsrs r1, #8")
        no_boundary = gen_label()
        if( @fussy_clocks )
            emit_asm( "beq #{no_boundary}")
            emit_asm( "adds r0, #1")
            emit_pc_changed( "r1", "r4")
            emit_asm( "#{no_boundary}:")
        end

        emit_asm( "add REG_CLK, r0" )
        emit_asm( "b #{label_cont}" )
        emit_asm( "#{label_skip}:" )
        emit_asm( "movs r0, ##{@clks}")
        emit_asm( "add REG_CLK, r0" )
        emit_advance_pc_asm( op, mode )
        emit_asm( "#{label_cont}:" )

        @clks = nil
        @no_pc = true
    end

    def emit_bra( op, mode, inst )
        emit_prefetch( mode, op, false )

        emit_code( "    #{CPUNAME}.clk += #{@clks+1};")
        if( @fussy_clocks )
            emit_code( "    if( (address&0xff00) != ((#{CPUNAME}.pc+2)&0xff00) ) #{CPUNAME}.clk++;\n" )
        end
        emit_code( "    #{CPUNAME}.pc = address;" )

        emit_asm( "movs r0, ##{@clks+1}")
        if( @fussy_clocks )
            emit_asm( "mov r1, REG_PC")
            emit_asm( "adds r1, #2")
            # not overflow from 16 bit here only affets a branch instruction at the end of RAM which is the IRQ
            # vector, but doesn't break anyway
            emit_asm( "eors r1, r4")
            emit_asm( "lsrs r1, #8")
            no_boundary = gen_label()
            emit_asm( "beq #{no_boundary}")
            emit_asm( "adds r0, #1")
            emit_asm( "#{no_boundary}:")
        end

         emit_asm( "add REG_CLK, r0" )
         emit_asm( "mov REG_PC, r4" )
         emit_pc_changed( "r0", "r4" )

         @clks = nil
         @no_pc = true
    end

    def emit_nmos_adc_body( op, mode, inst )
        emit_code( "    uint8_t prev = #{CPUNAME}.a;" )
        emit_code( "    uint cval = ( FLG_ISSET( #{CPUNAME}, c ) ? 1 : 0 );")
        emit_code( "    #{CPUNAME}.status &= ~( FLG_VAL(z) | FLG_VAL(c) | FLG_VAL(v) | FLG_VAL(n) );" )
        emit_code( "    uint16_t result = (uint16_t)#{CPUNAME}.a + value + cval;") #note this IS used below in decimal mode
        emit_code( "    if (!FLG_ISSET(#{CPUNAME}, d)) {")
        emit_code( "        #{CPUNAME}.a = (uint8_t)result;" )
        emit_code( "        if( #{CPUNAME}.a == 0 ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "        if( #{CPUNAME}.a & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "        if( result >> 8 ) #{CPUNAME}.status |= FLG_VAL( c );")
        emit_code( "        if( (( #{CPUNAME}.a ^ prev ) & ( #{CPUNAME}.a ^ value ) & 0x80) ) #{CPUNAME}.status |= FLG_VAL( v );" )
        emit_code( "    } else {" )
        #emit_code( "        printf(\"%%02x + %%02x + %%d \", #{CPUNAME}.a, value, cval);")
        emit_code( "        uint resultl = (#{CPUNAME}.a & 0xfu) + (value & 0xfu)  + cval;")
        emit_code( "        if( !(uint8_t)result ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "        uint resulth = 0;");
        emit_code( "        if (resultl > 9) {" )
        emit_code( "           resultl -= 10;" )
        emit_code( "           resultl &= 0xf;" )
        emit_code( "           resulth = 1 << 4;" )
        emit_code( "        }" )
        emit_code( "        resulth += (#{CPUNAME}.a & 0xf0u) + (value & 0xf0u);" )
        #emit_code( "        printf(\": %%02x %%02x\\n\", resultl, resulth);")
        emit_code( "        if( resulth & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "        if( (( resulth ^ prev ) & ( resulth ^ value ) & 0x80) ) #{CPUNAME}.status |= FLG_VAL( v );" )
        emit_code( "        if (resulth > 0x90) {" )
        emit_code( "           resulth -= 0xa0;" )
        emit_code( "           #{CPUNAME}.status |= FLG_VAL( c);" )
        emit_code( "        }" )
        emit_code( "        result = resulth | resultl; ")
        emit_code( "        #{CPUNAME}.a = (uint8_t)result;" )
      #  emit_code( "        printf(\"A %%d + %%d + %%d -> %%d C = %%d V = %%d N = %%d Z = %%d\\n\", prev, value, cval, #{CPUNAME}.a, FLG_ISSET(#{CPUNAME},c), FLG_ISSET(#{CPUNAME},v), FLG_ISSET(#{CPUNAME},n), FLG_ISSET(#{CPUNAME},z));" )
        emit_code( "    }" )


        emit_asm( "mov r1, REG_STATUS" )
        # ADC doesn't do writebacks, so can trash r4

        done = gen_label()
        emit_load_6502_reg( "r4", "a" )
        emit_asm( "mvns r4, r4" )
        emit_asm( "lsls r4, #24")
        emit_asm( "mvns r4, r4" )
        emit_asm( "lsls r3, #24")
        emit_asm( "lsrs r2, r1, #FLG_BIT_c + 1")
        emit_asm( "adcs r4, r3" )
        emit_asm( "mrs r2, apsr" )
        emit_asm( "lsrs r4, #24" )

        emit_asm( "strh r4, [REG_6502, #C6502_OFFSET_ZNSOURCE]")
        emit_asm( "lsrs r0, r1, #FLG_BIT_d + 1")
        decimal = gen_label()
        emit_asm( "bcs #{decimal}")

        emit_store_6502_reg( "r4", "a" )
        emit_asm( "movs r0, #0xc3  // NVZC")
        emit_asm( "bics r1, r0" )
        emit_asm( "lsrs r2, #28" )
        emit_asm( "adds r2, REG_6502" )
        emit_asm( "ldrb r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]" )
        emit_asm( "orrs r1, r2" )
        emit_asm( "mov REG_STATUS, r1" )
        emit_asm( "b #{done}")

        emit_asm( "#{decimal}:")
        emit_asm( "push {lr}")
        emit_asm( "bl nmos_adc_decimal_guts")
        emit_asm( "pop {r0}")
        emit_asm( "mov lr, r0")

        emit_asm(" #{done}:")

    end

    #
    # actual ops
    #

    def emit_nmos_adc( op, mode, inst )
        emit_prefetch( mode, op )
        emit_nmos_adc_body( op, mode, inst )
    end

    def emit_cmos_adc( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    int cval = ( FLG_ISSET( #{CPUNAME}, c ) ? 1 : 0 );")
        emit_code( "    #{CPUNAME}.status &= ~( FLG_VAL(z) | FLG_VAL(c) | FLG_VAL(v) | FLG_VAL(n) );" )
        emit_code( "    int result;")
        emit_code( "    if (!FLG_ISSET(#{CPUNAME}, d)) {")
        emit_code( "        result = #{CPUNAME}.a + value + cval;")
        emit_code( "        if( result >> 8 ) #{CPUNAME}.status |= FLG_VAL( c );")
        emit_code( "        if (!((#{CPUNAME}.a ^ value) & 0x80) && ((#{CPUNAME}.a ^ result) & 0x80)) #{CPUNAME}.status |= FLG_VAL( v);")
        emit_code( "        if( (uint8_t)result == 0 ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "        if( result & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "    } else {" )
        emit_code( "        uint resultl = (#{CPUNAME}.a & 0xfu) + (value & 0xfu)  + cval;")
        emit_code( "        uint resulth = 0;");
        emit_code( "        if (resultl > 9) {" )
        emit_code( "           resultl -= 10;" )
        emit_code( "           resultl &= 0xf;" )
        emit_code( "           resulth = 1 << 4;" )
        emit_code( "        }" )
        emit_code( "        resulth += (#{CPUNAME}.a & 0xf0u) + (value & 0xf0u);" )
        emit_code( "        if( resulth & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "        if (!((#{CPUNAME}.a ^ value) & 0x80) && ((#{CPUNAME}.a ^ resulth) & 0x80)) #{CPUNAME}.status |= FLG_VAL( v);")
        emit_code( "        if (resulth > 0x90) {" )
        emit_code( "           resulth -= 0xa0;" )
        emit_code( "           #{CPUNAME}.status |= FLG_VAL( c);" )
        emit_code( "        }" )
        emit_code( "        result = (uint8_t)(resulth | resultl);" )
        emit_code( "    }" )
        emit_code( "    #{CPUNAME}.a = (uint8_t)result;" )

        emit_asm( "mov r1, REG_STATUS" )
        # ABC doesn't do writebacks, so can trash r4
        done = gen_label()
        emit_load_6502_reg( "r4", "a" )

        decimal = gen_label()
        emit_asm( "lsrs r0, r1, #FLG_BIT_d + 1")
        emit_asm( "bcs #{decimal}")

        emit_asm( "mvns r4, r4" )
        emit_asm( "lsls r4, #24")
        emit_asm( "mvns r4, r4" )
        emit_asm( "lsls r3, #24")
        emit_asm( "lsrs r2, r1, #FLG_BIT_c + 1")
        emit_asm( "adcs r4, r3" )

        emit_asm( "mrs r2, apsr" )
        emit_asm( "lsrs r4, #24" )

        emit_asm( "strh r4, [REG_6502, #C6502_OFFSET_ZNSOURCE]")

        emit_store_6502_reg( "r4", "a" )
        emit_asm( "movs r0, #0xc3  // NVZC")
        emit_asm( "bics r1, r0" )
        emit_asm( "lsrs r2, #28" )
        emit_asm( "adds r2, REG_6502" )
        emit_asm( "ldrb r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]" )
        emit_asm( "orrs r1, r2" )
        emit_asm( "mov REG_STATUS, r1" )
        emit_asm( "b #{done}")

        emit_asm( "#{decimal}:")
        if (mode == :page0)
#             emit_asm("bkpt #0")
        end
        emit_asm( "push {lr}")
        emit_asm( "bl cmos_adc_decimal_guts")
        emit_asm( "pop {r0}")
        emit_asm( "mov lr, r0")

        emit_asm( "#{done}:")

    end

    def emit_nmos_sbc( op, mode, inst )
        emit_prefetch( mode, op )

        emit_code( "    uint8_t prev = #{CPUNAME}.a;" )
        emit_code( "    int cval = ( FLG_ISSET( #{CPUNAME}, c ) ? 0 : 1 );")
        emit_code( "    #{CPUNAME}.status &= ~( FLG_VAL(z) | FLG_VAL(c) | FLG_VAL(v) | FLG_VAL(n) );" )
        emit_code( "    int result = (uint16_t)#{CPUNAME}.a - value - cval;")
        emit_code( "    if (!FLG_ISSET(#{CPUNAME}, d)) {")
        emit_code( "        int tempv = (signed char)#{CPUNAME}.a - (signed char)value - cval;")
        emit_code( "        if( result >= 0) #{CPUNAME}.status |= FLG_VAL( c );")
        emit_code( "        if( ((result & 0x80) > 0) ^ ((tempv & 0x100) != 0)) #{CPUNAME}.status |= FLG_VAL( v );" )
        emit_code( "        #{CPUNAME}.a = (uint8_t)result;" )
        emit_code( "        if( #{CPUNAME}.a == 0 ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "        if( #{CPUNAME}.a & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "    } else {" )
        emit_code( "        uint resultl = (#{CPUNAME}.a & 0xfu) - (value & 0xfu) - cval;")
        emit_code( "        if( !(uint8_t)result ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "        if( result & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );
        emit_code( "        int resulth = 0;");
        emit_code( "        if (resultl & 16) {" )
        emit_code( "           resultl -= 6;" )
        emit_code( "           resultl &= 0xf;" )
        emit_code( "           resulth = -(1 << 4);" )
        emit_code( "        }" )
        emit_code( "        resulth += (#{CPUNAME}.a & 0xf0u) - (value & 0xf0u);" )
        emit_code( "        if (((#{CPUNAME}.a ^ value) & 0x80) && ((#{CPUNAME}.a ^ result) & 0x80)) #{CPUNAME}.status |= FLG_VAL( v );" )
        emit_code( "        if (resulth & 0x100u) {" )
        emit_code( "           resulth -= 0x60;" )
        emit_code( "        } else {" )
        emit_code( "           #{CPUNAME}.status |= FLG_VAL( c);" )
        emit_code( "        }" )
        emit_code( "        result = resulth | resultl; ")
        emit_code( "        #{CPUNAME}.a = (uint8_t)result;" )
        emit_code( "    }" )

        emit_asm( "mov r1, REG_STATUS" )
        # SBC doesn't do writebacks, so can trash r4
        done = gen_label()
        emit_load_6502_reg( "r4", "a" )
        emit_asm( "lsls r4, #24")
        emit_asm( "lsls r3, #24")
        emit_asm( "lsrs r2, r1, #FLG_BIT_c + 1")
        emit_asm( "sbcs r4, r4, r3" )
        emit_asm( "mrs r2, apsr" )
        emit_asm( "lsrs r4, #24" )
        emit_asm( "strh r4, [REG_6502, #C6502_OFFSET_ZNSOURCE]")
        emit_asm( "lsrs r0, r1, #FLG_BIT_d + 1")
        decimal = gen_label()
        emit_asm( "bcs #{decimal}")

        emit_store_6502_reg( "r4", "a" )
        emit_asm( "movs r0, #0xc3  // NVZC")
        emit_asm( "bics r1, r0" )
        emit_asm( "lsrs r2, #28" )
        emit_asm( "adds r2, REG_6502" )
        emit_asm( "ldrb r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]" )
        emit_asm( "orrs r1, r2" )

#         # todo just change the flags above
#         emit_asm( "movs r2, #1")
#         emit_asm( "eors r1, r2")

        emit_asm( "mov REG_STATUS, r1" )
        emit_asm( "b #{done}")

        emit_asm( "#{decimal}:")
        emit_asm( "push {lr}")
        emit_asm( "bl nmos_sbc_decimal_guts")
        emit_asm( "pop {r0}")
        emit_asm( "mov lr, r0")

        emit_asm(" #{done}:")
    end

    def emit_cmos_sbc( op, mode, inst )
        emit_prefetch( mode, op )

        emit_code( "    uint8_t prev = #{CPUNAME}.a;" )
        emit_code( "    int cval = ( FLG_ISSET( #{CPUNAME}, c ) ? 0 : 1 );")
        emit_code( "    #{CPUNAME}.status &= ~( FLG_VAL(z) | FLG_VAL(c) | FLG_VAL(v) | FLG_VAL(n) );" )
        emit_code( "    int result = (uint16_t)#{CPUNAME}.a - value - cval;")
        emit_code( "    int tempv = (signed char)#{CPUNAME}.a - (signed char)value - cval;")
        emit_code( "    if (((result & 0x80) > 0) ^ ((tempv & 0x100) != 0)) #{CPUNAME}.status |= FLG_VAL( v);" )
        emit_code( "    if (result >= 0 ) #{CPUNAME}.status |= FLG_VAL( c );")
        emit_code( "    if (FLG_ISSET(#{CPUNAME}, d)) {")
#        emit_code("if (#{CPUNAME}.a == 0 && value == 11 && !FLG_ISSET( #{CPUNAME}, c )) { ")
#                            emit_code("printf(\"waloomera\\n\");")
#                        emit_code("}")

        emit_code("     int al = (#{CPUNAME}.a & 0xf) - (value & 0xf) - cval;")
        emit_code("     if (result < 0)")
        emit_code("        result -= 0x60;")
        emit_code("     if (al < 0)")
        emit_code("         result -= 0x06;")
  #      emit_code( "        printf(\"A 0x%%02x + 0x%%02x + %%d -> 0x%%02x C = %%d V = %%d N = %%d Z = %%d\\n\", prev, value, cval, result&0xff, FLG_ISSET(#{CPUNAME},c), FLG_ISSET(#{CPUNAME},v), FLG_ISSET(#{CPUNAME},n), FLG_ISSET(#{CPUNAME},z));" )

        emit_code( "    }" )
        emit_code( "    #{CPUNAME}.a = (uint8_t)result;" )
        emit_code( "    if( #{CPUNAME}.a == 0 ) #{CPUNAME}.status |= FLG_VAL( z );" );
        emit_code( "    if( #{CPUNAME}.a & 0x80 ) #{CPUNAME}.status |= FLG_VAL( n );" );


        emit_asm( "mov r1, REG_STATUS" )
        # SBC doesn't do writebacks, so can trash r4
        done = gen_label()
        emit_load_6502_reg( "r4", "a" )
        emit_asm( "lsls r4, #24")
        emit_asm( "lsls r3, #24")
        emit_asm( "lsrs r2, r1, #FLG_BIT_c + 1")
        emit_asm( "sbcs r4, r4, r3" )
        emit_asm( "mrs r2, apsr" )
        emit_asm( "lsrs r4, #24" )
        emit_asm( "lsrs r2, #28" )
        emit_asm( "adds r2, REG_6502" )
        emit_asm( "ldrb r2, [ r2, #C6502_OFFSET_NVCZFLAGS ]" )
        emit_asm( "lsrs r0, r1, #FLG_BIT_d + 1")
        emit_asm( "bcc #{done}")
        #emit_asm( "bkpt #0")

        emit_asm( "push {lr}")
        emit_asm( "bl cmos_sbc_decimal_guts")
        emit_asm( "pop {r0}")
        emit_asm( "mov lr, r0")

        emit_asm( "#{done}:")
        emit_asm( "uxtb r4, r4")
        emit_asm( "movs r0, #0xc3  // NVZC")
        emit_asm( "bics r1, r0" )
        emit_asm( "orrs r1, r2 // add VC back" )

        emit_asm( "strh r4, [REG_6502, #C6502_OFFSET_ZNSOURCE]")
        emit_asm( "mov REG_STATUS, r1")
        emit_asm ("mov REG_ACC, r4")

#        @status_in_r1 = true
#        emit_store_6502_reg( "r4", "a" )
#        emit_zn_flag_check({:reg => "r4"})
    end

    def emit_ora( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a |= value;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "orrs r2, r3" )
        emit_store_6502_reg( "r2", "a" )

        emit_zn_flag_check()
    end

    def emit_and( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a &= value;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "ands r2, r3" )
        emit_store_6502_reg( "r2", "a" )

        emit_zn_flag_check()
    end

    def emit_eor( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a ^= value;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "eors r2, r3" )
        emit_store_6502_reg( "r2", "a" )

        emit_zn_flag_check()
    end

    def emit_sta( op, mode, inst )
        emit_prefetch( mode, op, false )
        emit_code( "    uint8_t value = #{CPUNAME}.a;" )
        emit_load_6502_reg( "r2", "a" )
        emit_writeback( op, mode )
        @clks -= 2
    end

    def emit_stx( op, mode, inst )
        emit_prefetch( mode, op, false )
        emit_code( "    uint8_t value = #{CPUNAME}.x;" )
        emit_load_6502_reg( "r2", "x" )
        emit_writeback( op, mode )
        @clks -= 2
    end

    def emit_sty( op, mode, inst )
        emit_prefetch( mode, op, false )
        emit_code( "    uint8_t value = #{CPUNAME}.y;" )
        emit_load_6502_reg( "r2", "y" )
        emit_writeback( op, mode )
        @clks -= 2
    end

    def emit_lda( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a = value;" )
        emit_store_6502_reg( "r3", "a" )
        emit_zn_flag_check( { :reg => "r3" } )
    end

    def emit_ldx( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.x = value;" )
        emit_store_6502_reg( "r3", "x" )
        emit_zn_flag_check( :var => "#{CPUNAME}.x", :reg => "r3" )
    end

    def emit_ldy( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.y = value;" )
        emit_store_6502_reg( "r3", "y" )
        emit_zn_flag_check( :var => "#{CPUNAME}.y", :reg => "r3"  )
    end

    def emit_cmp( op, mode, inst )
        emit_prefetch( mode, op )
        emit_compare( "a" )
    end

    def emit_cpx( op, mode, inst )
        emit_prefetch( mode, op )
        emit_compare( "x" )
    end

    def emit_cpy( op, mode, inst )
        emit_prefetch( mode, op )
        emit_compare( "y" )
    end

    def emit_asl( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    if( value & 0x80 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "    value <<= 1;" )

        emit_asm( "movs r0, #( 1 << FLG_BIT_c )" )  # carry bit mask
        emit_asm( "lsls r2, r3, #1" )               # shift left
        emit_asm( "uxtb r2, r2" )                   # clamp
        emit_asm( "lsrs r3, r3, #7" )               # carry bit in bit 0
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "bics r1, r0" )                   # clear bit 0
        emit_asm( "orrs r1, r3" )                   # bring in bit 0
        @status_in_r1 = true

        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, mode )
    end

    def emit_rol( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    if( FLG_ISSET( #{CPUNAME}, c ) ) {")
        emit_code( "        if( value & 0x80 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value = ( value << 1 ) | 1;" )
        emit_code( "    } else {" )
        emit_code( "        if( value & 0x80 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value <<= 1;" )
        emit_code( "    }" )

        emit_asm( "movs r0, #(1 << FLG_BIT_c)" )    # carry-bit mask
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "ands r1, r0" )                   # mask off carry bit
        emit_asm( "lsls r2, r3, #1" )               # shift left
        emit_asm( "orrs r2, r1" )                   # add carry bit in
        emit_asm( "uxtb r2, r2" )                   # clamp
        emit_asm( "lsrs r3, r3, #7" )               # carry bit in bit 0
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "bics r1, r0" )                   # clear bit 0
        emit_asm( "orrs r1, r3" )                   # bring in bit 0
        @status_in_r1 = true
        
        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, mode )
    end

    def emit_lsr( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "    value >>= 1;" )

        emit_asm( "movs r0, #( 1 << FLG_BIT_c )" )  # carry bit mask
        emit_asm( "lsrs r2, r3, #1" )               # shift right
        emit_asm( "ands r3, r0" )                   # get new carry bit
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "bics r1, r0" )                   # clear carry
        emit_asm( "orrs r1, r3" )                   # set carry
        @status_in_r1 = true

        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, mode )
    end

    def emit_ror( op, mode, inst )
        emit_prefetch( mode, op )

        emit_code( "    if( FLG_ISSET( #{CPUNAME}, c ) ) {")
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value = ( value >> 1 ) | 0x80;" )
        emit_code( "    } else {" )
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value >>= 1;" )
        emit_code( "    }" )

        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "lsls r0, r1, #7" )               # move carry bit to bit 7
        emit_asm( "uxtb r0, r0" )                   # mask off carry bit
        emit_asm( "lsrs r2, r3, #1" )               # shift right
        emit_asm( "orrs r2, r2, r0" )               # OR in the old carry
        emit_asm( "movs r0, #1" )                   # carry bit mask
        emit_asm( "bics r1, r0" )                   # clear in status
        emit_asm( "ands r3, r0" )                   # get bit 0 from original operand
        emit_asm( "orrs r1, r3" )                   # OR it in
        @status_in_r1 = true

        emit_zn_flag_check( :var => "value" ) if( inst[ :op ] == :ror )
        emit_writeback( op, mode )
    end

    def emit_dec( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    value--;" )
        emit_asm( "subs r2, r3, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, mode )
    end

    def emit_dex( op, mode, inst )
        emit_code( "    #{CPUNAME}.x--;" )
        emit_load_6502_reg( "r2", "x" )
        emit_asm( "subs r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "x" )
        emit_zn_flag_check( :var => "#{CPUNAME}.x" )
    end

    def emit_dey( op, mode, inst )
        emit_code( "    #{CPUNAME}.y--;" )
        emit_load_6502_reg( "r2", "y" )
        emit_asm( "subs r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "y" )
        emit_zn_flag_check( :var => "#{CPUNAME}.y", :reg => "r2" )
    end

    def emit_inc( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    value++;" )
        emit_asm( "adds r2, r3, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, mode )
    end

    def emit_inx( op, mode, inst )
        emit_code( "    #{CPUNAME}.x++;" )
        emit_load_6502_reg( "r2", "x" )
        emit_asm( "adds r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "x" )
        emit_zn_flag_check( :var => "#{CPUNAME}.x" )
    end

    def emit_iny( op, mode, inst )
        emit_code( "    #{CPUNAME}.y++;" )
        emit_load_6502_reg( "r2", "y" )
        emit_asm( "adds r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "y" )
        emit_zn_flag_check( :var => "#{CPUNAME}.y" )
    end

    def emit_bit( op, mode, inst )
        emit_prefetch( mode, op )
        if (mode != :immed)
            emit_code( "    if( value & 0x80 ) { FLG_SET( #{CPUNAME}, n ); } else { FLG_CLR( #{CPUNAME}, n ); }" )
            emit_code( "    if( value & 0x40 ) { FLG_SET( #{CPUNAME}, v ); } else { FLG_CLR( #{CPUNAME}, v ); }" )
        end
        emit_code( "    if( value & #{CPUNAME}.a )  { FLG_CLR( #{CPUNAME}, z ); } else { FLG_SET( #{CPUNAME}, z ); }")

        cont = gen_label()
        if (mode != :immed)
            emit_asm( "mov r1, REG_STATUS" )
            emit_asm( "movs r0, #0x40" )
            emit_asm( "bics r1, r0" )
            emit_asm( "mov r2, r3" )
            emit_asm( "ands r2, r0" )
            emit_asm( "orrs r1, r2" )
            emit_asm( "mov REG_STATUS, r1" )

            emit_load_6502_reg( "r2", "a" )
            emit_asm( "ands r2, r3" )
            emit_asm( "bne #{cont}" )
            emit_asm( "lsrs r3, #7" )
            emit_asm( "lsls r3, #8" )
            emit_asm( "#{cont}:" )
            emit_asm( "strh r3, [REG_6502, #C6502_OFFSET_ZNSOURCE]")
        else
            emit_asm( "mov r1, REG_STATUS" )
            emit_asm( "movs r0, #(1 << FLG_BIT_z)" )
            emit_asm( "bics r1, r0" )
            emit_load_6502_reg( "r2", "a" )
            emit_asm( "ands r2, r3" )
            emit_asm( "bne #{cont}" )
            emit_asm( "orrs r1, r0")
            emit_asm( "#{cont}:" )
            emit_asm( "mov REG_STATUS, r1" )
            emit_asm( "update_znsource")
        end
    end

    def emit_jmp( op, mode, inst )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( #{CPUNAME}.pc + 1 );" )

        emit_read16_pc_rel( "REG_PC", 1, "r1" )
         emit_pc_changed( "r0", "r4" )

        @clks = 3
        @no_pc = true
    end

    def emit_nmos_jmpi( op, mode, inst )
        # todo
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( #{CPUNAME}.pc + 1 );" )
        emit_code( "assert(!(0xff == (#{CPUNAME}.pc & 0xff))); // todo incorrect wrap bug")
        emit_code( "    #{CPUNAME}.pc = C6502_Read16_P( #{CPUNAME}.pc );" )

        emit_read16_pc_rel( "r2", 1, "r1" )
        emit_read16_nofunc_p( "r2", "REG_PC", "r1", "r3" )
         emit_pc_changed( "r0", "r4" )

        @clks = 6
        @no_pc = true
    end

    def emit_cmos_jmpi( op, mode, inst )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( #{CPUNAME}.pc + 1 );" )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16_P( #{CPUNAME}.pc );" )

        emit_read16_pc_rel( "r2", 1, "r1" )
        emit_read16_nofunc_p( "r2", "REG_PC", "r1", "r3" )
         emit_pc_changed( "r0", "r4" )

        @clks = 6
        @no_pc = true
    end

    def emit_php( op, mode, inst )
        emit_code( "    C6502_Push8( #{CPUNAME}.status | 0x30 );" )

        emit_asm( "update_status_zn")
        emit_asm( "mov r2, REG_STATUS" )
        emit_asm( "movs r1, #0x30" )
        emit_asm( "orrs r2, r1" )
        emit_push8( "r2", "r0", "r1" )

        @clks = 3
    end

    # no need to preserve 0-3
    def emit_breakout_check()
        emit_code("#ifdef USE_HW_EVENT")
        emit_code("int check = possible_cpu_irq_breakout();")
        emit_code("assert(g_cpu.clk == check);")
        emit_code("#endif")
        #emit_asm("push \{r0-r3\}")
        emit_asm("mov r0, r12")
        emit_asm("push \{r0\}")
        # todo this is somewhat beeb specific - we only care about status and clk (and indeed only i bit in status)
        emit_asm("mov r0, REG_STATUS")
        emit_asm("strb r0, [r7, #C6502_OFFSET_STATUS]")
        emit_asm("mov r0, REG_CLK")
        emit_asm("str r0, [r7, #C6502_OFFSET_CLK]")
        # dont think we need to save lr as it always is
        emit_asm("bl possible_cpu_irq_breakout")
        emit_asm("mov REG_CLK, r0")
        emit_asm("pop \{r0\}")
        emit_asm("mov r12, r0")
        #emit_asm("pop \{r0-r3\}")
    end

    def emit_plp( op, mode, inst )
        emit_code( "    #{CPUNAME}.status = C6502_Pop() & ~0x30;" )

        emit_pop8( "r2", "r1", "r0" )
        emit_asm( "movs r1, #0x30" )
        emit_asm( "bics r2, r1" )
        emit_asm( "mov REG_STATUS, r2" )
        emit_asm( "update_znsource")
        emit_breakout_check()
        @clks = 4
    end

    def emit_pha( op, mode, inst )
        emit_code( "    C6502_Push8( #{CPUNAME}.a );" )

        emit_load_6502_reg( "r2", "a" )
        emit_push8( "r2", "r0", "r1" )

        @clks = 3
    end

    def emit_pla( op, mode, inst )
        emit_code( "    #{CPUNAME}.a = C6502_Pop();" )

        emit_pop8( "r2", "r1", "r0" )
        emit_store_6502_reg( "r2", "a" )

        emit_zn_flag_check()
        @clks = 4
    end

    def emit_tax( op, mode, inst )
        emit_code( "    #{CPUNAME}.x = #{CPUNAME}.a;" )
        emit_load_6502_reg( "r2", "a" )
        emit_store_6502_reg( "r2", "x" )
        emit_zn_flag_check()
    end

    def emit_tay( op, mode, inst )
        emit_code( "    #{CPUNAME}.y = #{CPUNAME}.a;" )
        emit_load_6502_reg( "r2", "a" )
        emit_store_6502_reg( "r2", "y" )
        emit_zn_flag_check()
    end

    def emit_txa( op, mode, inst )
        emit_code( "    #{CPUNAME}.a = #{CPUNAME}.x;" )
        emit_load_6502_reg( "r2", "x" )
        emit_store_6502_reg( "r2", "a" )
        emit_zn_flag_check()
    end

    def emit_tya( op, mode, inst )
        emit_code( "    #{CPUNAME}.a = #{CPUNAME}.y;" )
        emit_load_6502_reg( "r2", "y" )
        emit_store_6502_reg( "r2", "a" )
        emit_zn_flag_check()
    end

    def emit_tsx( op, mode, inst )
        emit_code( "    #{CPUNAME}.x = #{CPUNAME}.sp;" )
        emit_asm( "ldrb r2, [ REG_6502, #C6502_OFFSET_SP ]" )
        emit_store_6502_reg( "r2", "x" )
        emit_zn_flag_check( :var => "#{CPUNAME}.x" )
    end

    def emit_txs( op, mode, inst )
        emit_code( "    #{CPUNAME}.sp = #{CPUNAME}.x;" )
        emit_load_6502_reg( "r2", "x" )
        emit_asm( "strb r2, [ REG_6502, #C6502_OFFSET_SP ]" )
    end

    def set_clear_flag( flg, set )
        emit_code( "    FLG_#{set ? "SET" : "CLR" }( #{CPUNAME}, #{flg} );" )
        emit_asm( "movs r2, #(1<<FLG_BIT_#{flg})" )
        emit_asm( "mov r3, REG_STATUS" )
        emit_asm( "#{set ? "orrs" : "bics"} r3, r2" )
        emit_asm( "mov REG_STATUS, r3" )
    end

    def emit_clc( op, mode, inst )
        set_clear_flag( "c", false )
    end
    def emit_sec( op, mode, inst )
        set_clear_flag( "c", true )
    end
    def emit_cli( op, mode, inst )
        set_clear_flag( "i", false )
        emit_breakout_check()
        emit_code( "#{CPUNAME}.cli_breakout = true; ")
    end
    def emit_sei( op, mode, inst )
        set_clear_flag( "i", true )
    end
    def emit_clv( op, mode, inst )
        set_clear_flag( "v", false )
    end
    def emit_cld( op, mode, inst )
        set_clear_flag( "d", false )
    end
    def emit_sed( op, mode, inst )
        set_clear_flag( "d", true )
    end
    def emit_nop( op, mode, inst )
    end

    def emit_jsr( op, mode, inst )
        emit_prefetch( mode, op, false );
        emit_code( "    C6502_Push16( #{CPUNAME}.pc + 2 );" )
        emit_code( "    #{CPUNAME}.pc = address;" );

        emit_asm( "adds REG_PC, #2" )
        emit_push16( "REG_PC", "r0", "r1", "r2" )
        emit_asm( "mov REG_PC, r4" )
         emit_pc_changed( "r0", "r4" )

        @clks = 6
        @no_pc = true
    end

    def emit_rts( op, mode, inst )
        emit_code( "    #{CPUNAME}.pc = C6502_Pop16();" )
        # advance pc will move onto next instruction

        emit_pop16( "REG_PC", "r0", "r1", "r2" )
         emit_pc_changed( "r0", "r4" )

        @clks = 6
    end

    def emit_rti( op, mode, inst )
        emit_plp( op, mode, inst )
        emit_rts( op, mode, inst )
        @no_pc = true
    end

    def emit_brk6502( op, mode, inst )
        emit_code( "    C6502_Push16( #{CPUNAME}.pc + 2 );" )

        emit_asm( "adds REG_PC, #2" )
        emit_push16( "REG_PC", "r0", "r1", "r2" )

        emit_php( op, mode, inst )

        emit_code( "    FLG_SET( #{CPUNAME}, i );" )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( 0xfffe );" )

        emit_asm( "mov r0, REG_STATUS" )
        emit_asm( "movs r1, #( 1 << FLG_BIT_i )" )
        emit_asm( "orrs r0, r1" )
        emit_asm( "mov REG_STATUS, r0" )

        emit_asm( "eors r2, r2" )
        emit_asm( "subs r2, #2" )
        emit_asm( "uxth r2, r2" )
        # for beeb (and it is safe on others, we have to allow BRK via a RAM function hosted address)
        #emit_read16_contiguous_nofunc( "r2", "REG_PC", "r0" )
        emit_read16_full( "r2", "REG_PC", "r3", "r1" )
         emit_pc_changed( "r0", "r4" )

        @clks += 4
        @no_pc = true
    end

    def emit_brk6512( op, mode, inst )
        emit_code( "    C6502_Push16( #{CPUNAME}.pc + 2 );" )

        emit_asm( "adds REG_PC, #2" )
        emit_push16( "REG_PC", "r0", "r1", "r2" )

        emit_php( op, mode, inst )

        emit_code( "    FLG_SET( #{CPUNAME}, i );" )
        emit_code( "    FLG_CLR( #{CPUNAME}, d );" )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( 0xfffe );" )

        emit_asm( "mov r0, REG_STATUS" )
        emit_asm( "movs r1, #( 1 << FLG_BIT_i )" )
        emit_asm( "orrs r0, r1" )
        emit_asm( "movs r1, #( 1 << FLG_BIT_d )" )
        emit_asm( "bics r0, r1" )
        emit_asm( "mov REG_STATUS, r0" )

        emit_asm( "eors r2, r2" )
        emit_asm( "subs r2, #2" )
        emit_asm( "uxth r2, r2" )

        # for beeb (and it is safe on others, we have to allow BRK via a RAM function hosted address)
        #emit_read16_contiguous_nofunc( "r2", "REG_PC", "r0" )
        emit_read16_full( "r2", "REG_PC", "r3", "r1" )
         emit_pc_changed( "r0", "r4" )

        @clks += 4
        @no_pc = true
    end

    #
    # undocumented crap
    #

    def emit_nmos_badop( op, mode, inst )
        emit_prefetch( mode, op, false )
        #emit_code("assert(false);" )
    end

    def emit_xaa( op, mode, inst)
        emit_nmos_badop(op, mode, inst)
    end

    def emit_ahx( op, mode, inst)
        emit_nmos_badop(op, mode, inst)
    end

    def emit_tas( op, mode, inst)
        emit_nmos_badop(op, mode, inst)
    end

    def emit_las( op, mode, inst)
        emit_nmos_badop(op, mode, inst)
    end

    def emit_axs( op, mode, inst)
        emit_nmos_badop(op, mode, inst)
    end

    def emit_anc( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a &= value;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "ands r2, r3" )
        emit_store_6502_reg( "r2", "a" )

        emit_zn_flag_check();

        emit_code("FLG_COPY(#{CPUNAME},c,n);")
        emit_asm("bkpt #0") #todo
    end

    def emit_alr( op, mode, inst )
        emit_prefetch( mode, op )

        # difference from lsr
        emit_code( "    value &= #{CPUNAME}.a; ")

        emit_code( "    if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "    value >>= 1;" )

        # difference from lsr
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "ands r3, r2")

        emit_asm( "movs r0, #( 1 << FLG_BIT_c )" )  # carry bit mask
        emit_asm( "lsrs r2, r3, #1" )               # shift right
        emit_asm( "ands r3, r0" )                   # get new carry bit
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "bics r1, r0" )                   # clear carry
        emit_asm( "orrs r1, r3" )                   # set carry
        @status_in_r1 = true

        emit_zn_flag_check( :var => "value" )
        emit_writeback( op, :accum )
    end

    def emit_arr( op, mode, inst )
        emit_prefetch( mode, op )

        # difference from ror
        emit_code( "    value &= #{CPUNAME}.a; ")

        emit_code( "    value &= #{CPUNAME}.a; ") # difference from lsr
        emit_code( "    if( FLG_ISSET( #{CPUNAME}, c ) ) {")
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value = ( value >> 1 ) | 0x80;" )
        emit_code( "    } else {" )
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value >>= 1;" )
        emit_code( "    }" )

        # difference from ror
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "ands r3, r2")

        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "lsls r0, r1, #7" )               # move carry bit to bit 7
        emit_asm( "uxtb r0, r0" )                   # mask off carry bit
        emit_asm( "lsrs r2, r3, #1" )               # shift right
        emit_asm( "orrs r2, r2, r0" )               # OR in the old carry
        emit_asm( "movs r0, #1" )                   # carry bit mask
        emit_asm( "bics r1, r0" )                   # clear in status
        emit_asm( "ands r3, r0" )                   # get bit 0 from original operand
        emit_asm( "orrs r1, r3" )                   # OR it in
        @status_in_r1 = true

        emit_zn_flag_check( :var => "value" ) if( inst[ :op ] == :ror )
        emit_writeback( op, :accum )
    end

    def emit_lax( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    #{CPUNAME}.a = value;")
        emit_code( "    #{CPUNAME}.x = value;")
        emit_store_6502_reg( "r3", "a" )
        emit_store_6502_reg( "r3", "x" )
        emit_zn_flag_check( { :reg => "r3" } )
    end

    def emit_sax( op, mode, inst )
        emit_prefetch( mode, op, false )
        emit_code( "    uint8_t value = #{CPUNAME}.a & #{CPUNAME}.x;")
        emit_load_6502_reg( "r3", "a" )
        emit_load_6502_reg( "r2", "x" )
        emit_asm( "ands r2, r2, r3" )
        emit_writeback( op, mode )
        @clks -= 2
    end

    def emit_dcp( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    value--;" )
        emit_asm( "subs r3, r3, #1" )
        emit_asm( "uxtb r3, r3" )
        emit_asm( "push {r3}" )
        emit_compare( "a" )
        emit_asm( "pop {r2}" )
        emit_writeback( op, mode )
    end

    def emit_isb( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    value++;" )
        emit_asm( "adds r2, r3, #1" )

        emit_asm( "push {r2}" )
        emit_writeback( op, mode )          # could be problematic
        emit_asm( "pop {r3}" )

        emit_code( "    value = ~value;" )
        emit_asm( "mvns r3, r3" )
        emit_asm( "uxtb r3, r3" )

        emit_nmos_adc_body( op, mode, inst )
    end

    def emit_slo( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    if( value & 0x80 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "    value <<= 1;" )
        emit_code( "    #{CPUNAME}.a |= value;" )

        emit_asm( "mov r1, REG_STATUS" )
        emit_asm( "movs r0, #1" )
        emit_asm( "bics r1, r0" )
        emit_asm( "lsrs r0, r3, #7" )
        emit_asm( "orrs r1, r0" )
        emit_asm( "mov REG_STATUS, r1" )
        emit_load_6502_reg( "r0", "a" )
        emit_asm( "lsls r2, r3, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_asm( "mov r3, r0" )
        emit_asm( "orrs r3, r2" )
        emit_store_6502_reg( "r3", "a" )

        emit_zn_flag_check( { :reg => "r3" } )
        emit_writeback( op, mode )
    end

    def emit_rla( op, mode, inst )

        emit_rol( op, mode, inst )
        emit_code( "    #{CPUNAME}.a &= value;")
        emit_load_6502_reg( "r0", "a" )
        emit_asm( "ands r0, r2" )
        emit_store_6502_reg( "r0", "a" )
        emit_zn_flag_check( { :reg => "r0" } )
    end

    def emit_rra( op, mode, inst )

        emit_prefetch( mode, op )
        emit_code( "    if( FLG_ISSET( #{CPUNAME}, c ) ) {")
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value = ( value >> 1 ) | 0x80;" )
        emit_code( "    } else {" )
        emit_code( "        if( value & 0x01 ) { FLG_SET( #{CPUNAME}, c ); } else { FLG_CLR( #{CPUNAME}, c ); }" )
        emit_code( "        value >>= 1;" )
        emit_code( "    }" )
        emit_asm( "mov r1, REG_STATUS" )            # get status
        emit_asm( "lsls r0, r1, #7" )               # move carry bit to bit 7
        emit_asm( "uxtb r0, r0" )                   # mask off carry bit
        emit_asm( "lsrs r2, r3, #1" )               # shift right
        emit_asm( "orrs r2, r2, r0" )               # OR in the old carry
        emit_asm( "movs r0, #1" )                   # carry bit mask
        emit_asm( "bics r1, r0" )                   # clear in status
        emit_asm( "ands r3, r0" )                   # get bit 0 from original operand
        emit_asm( "orrs r1, r3" )                   # OR it in
        emit_asm( "mov REG_STATUS, r1" )            # store status - writeback will trash r1
        emit_asm( "push {r2}" )
        emit_writeback( op, mode )
        emit_asm( "pop {r3}" )
        emit_nmos_adc_body( op, mode, inst )

        # emit_ror( op, mode, inst )
        # emit_asm( "mov r3, r2" )
        # emit_adc_body( op, mode, inst )
        # @status_in_r1 = false
    end

    def emit_sre( op, mode, inst )
        emit_lsr( op, mode, inst )
        emit_code( "    #{CPUNAME}.a ^= value;")

        emit_load_6502_reg( "r3", "a" )
        emit_asm( "eors r3, r2" )
        emit_store_6502_reg( "r3", "a" )
        @status_in_r1 = false
        emit_zn_flag_check( { :reg => "r3" } )
    end

    #
    # c6512
    #

    def emit_tsb( op, mode, inst )
        emit_prefetch( mode, op )
        emit_code( "    if( value & #{CPUNAME}.a )  { FLG_CLR( #{CPUNAME}, z ); } else { FLG_SET( #{CPUNAME}, z ); }")
        emit_code( "    value |= #{CPUNAME}.a;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "mov r1, REG_STATUS")
        emit_asm( "movs r0, #(1 << FLG_BIT_z)")
        emit_asm( "bics r1, r0")
        emit_asm( "ands r2, r3")
        no_z = gen_label()
        emit_asm( "bne #{no_z}")
        emit_asm( "orrs r1, r0")
        emit_asm( "#{no_z}:")
        emit_asm( "mov REG_STATUS, r1")
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "orrs r2, r3")
        emit_writeback( op, mode )

        emit_asm( "update_znsource")
    end

    def emit_trb( op, mode, inst )
        emit_prefetch( mode, op )

        emit_code( "    if( value & #{CPUNAME}.a )  { FLG_CLR( #{CPUNAME}, z ); } else { FLG_SET( #{CPUNAME}, z ); }")
        emit_code( "    value &= ~#{CPUNAME}.a;" )

        emit_load_6502_reg( "r2", "a" )
        emit_asm( "mov r1, REG_STATUS")
        emit_asm( "movs r0, #(1 << FLG_BIT_z)")
        emit_asm( "bics r1, r0")
        emit_asm( "ands r2, r3")
        no_z = gen_label()
        emit_asm( "bne #{no_z}")
        emit_asm( "orrs r1, r0")
        emit_asm( "#{no_z}:")
        emit_asm( "mov REG_STATUS, r1")
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "mvns r2, r2")
        emit_asm( "ands r2, r3")
        emit_writeback( op, mode )

        emit_asm( "update_znsource")
    end

    def emit_ina( op, mode, inst)
        emit_code( "    #{CPUNAME}.a++;" )
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "adds r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "a" )
        emit_zn_flag_check( :var => "#{CPUNAME}.a" )
    end

    def emit_dea( op, mode, inst)
        emit_code( "    #{CPUNAME}.a--;" )
        emit_load_6502_reg( "r2", "a" )
        emit_asm( "subs r2, r2, #1" )
        emit_asm( "uxtb r2, r2" )
        emit_store_6502_reg( "r2", "a" )
        emit_zn_flag_check( :var => "#{CPUNAME}.a" )
    end


    def emit_phx( op, mode, inst )
        emit_code( "    C6502_Push8( #{CPUNAME}.x );" )

        emit_load_6502_reg( "r2", "x" )
        emit_push8( "r2", "r0", "r1" )

        @clks = 3
    end


    def emit_phy( op, mode, inst )
        emit_code( "    C6502_Push8( #{CPUNAME}.y );" )

        emit_load_6502_reg( "r2", "y" )
        emit_push8( "r2", "r0", "r1" )

        @clks = 3
    end

    def emit_stz( op, mode, inst )
        emit_prefetch( mode, op, false )
        emit_code( "    uint8_t value = 0;" )
        emit_asm( "movs r2, #0" )
        emit_writeback( op, mode )
        @clks -= 2
    end

    def emit_plx( op, mode, inst )
        emit_code( "    #{CPUNAME}.x = C6502_Pop();" )

        emit_pop8( "r2", "r1", "r0" )
        emit_store_6502_reg( "r2", "x" )

        emit_zn_flag_check( :var => "#{CPUNAME}.x" )
        @clks = 4
    end

    def emit_ply( op, mode, inst )
        emit_code( "    #{CPUNAME}.y = C6502_Pop();" )

        emit_pop8( "r2", "r1", "r0" )
        emit_store_6502_reg( "r2", "y" )

        emit_zn_flag_check( :var => "#{CPUNAME}.y" )
        @clks = 4
    end

    def emit_jmpix( op, mode, inst )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16( #{CPUNAME}.pc + 1 ) + #{CPUNAME}.x;" )
        emit_code( "    #{CPUNAME}.pc = C6502_Read16_P( #{CPUNAME}.pc );" )

        emit_read16_pc_rel( "r2", 1, "r1" )
        emit_load_6502_reg( "r3", "x")
        emit_asm("adds r2, r3")
        emit_asm("uxth r2, r2")
        emit_read16_full( "r2", "REG_PC", "r1", "r3" )
#         emit_read16_nofunc_p( "r2", "REG_PC", "r1", "r3" )
         emit_pc_changed( "r0", "r4" )

        @clks = 6
        @no_pc = true
        @no_pc = true
    end

    def emit_cmos_badop( op, mode, inst )
        # handled in generator loop
    end

end

# TODO BADOP ON c6512 b-em has this - i.e. delays only
#                default:
#                         switch (opcode & 0xF) {
#                         case 2:
#                                 pc++;
#                                 polltime(2);
#                                 break;
#                         case 3:
#                         case 7:
#                         case 0xB:
#                         case 0xF:
#                                 polltime(1);
#                                 break;
#                         case 4:
#                                 pc++;
#                                 if (opcode == 0x44) {
#                                         polltime(3);
#                                 } else {
#                                         polltime(4);
#                                 }
#                                 break;
#                         case 0xC:
#                                 pc += 2;
#                                 if (opcode == 0x5C) {
#                                         polltime(7);
#                                 } else {
#                                         polltime(4);
#                                 }
#                                 break;
#                         }
#                         takeint = (interrupt && !p.i);
#
Gen6502.new()

