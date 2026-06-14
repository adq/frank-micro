if (PI_BUILD AND PI_ASM32)
    set(GEN_ASM32 1)
endif()

set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/src_generated)
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
        OUTPUT ${GEN_DIR}/6502_asm.inl ${GEN_DIR}/6502_c.inl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen/gen_asm.rb
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND gen/gen_asm.rb ${GEN_DIR}/6502_asm.inl ${GEN_DIR}/6502_c.inl ${GEN_ASM32}
)

add_library(pico_cpu_common INTERFACE)
if (NOT PICO_ON_DEVICE)
    add_library(pico_cpu_no_asm INTERFACE)
endif()
add_library(pico_cpu INTERFACE)

if (PICO_ON_DEVICE)
    set(THUMB_CPU_USE_ASM 1)
else()
    if (PI_BUILD)
        target_compile_definitions(pico_cpu_common INTERFACE
                PI_BUILD=1
        )
    endif()

    if (PI_BUILD AND PI_ASM32)
        set(THUMB_CPU_USE_ASM 1)
        target_link_options(pico_cpu INTERFACE "LINKER:-Map=$<TARGET_PROPERTY:NAME>${CMAKE_EXECUTABLE_SUFFIX}.map")
        target_compile_definitions(pico_cpu INTERFACE
                PI_ASM32=1
        )
        # right now the ruby generates crud from master in beeb and vice versa, and you'd get unresolved references
        target_compile_options(pico_cpu INTERFACE -ffunction-sections -fdata-sections)
        target_link_options(pico_cpu INTERFACE "LINKER:--gc-sections")
    else ()
        set(THUMB_CPU_USE_ASM 0)
    endif ()
endif ()

target_sources(pico_cpu_common INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/cpu.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu_mem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/adapter.c
)

target_include_directories(pico_cpu_common INTERFACE ${GEN_DIR})
add_custom_target(cstuff DEPENDS ${GEN_DIR}/6502_c.inl)

target_compile_definitions(pico_cpu_common INTERFACE
        THUMB_CPU_BEEB # there are a few beeb specific things in the asm
)

if (THUMB_CPU_USE_ASM)
    target_compile_definitions(pico_cpu INTERFACE THUMB_CPU_USE_ASM=1)
    target_sources(pico_cpu INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/cpu_asm.S)
    add_custom_target(asmstuff DEPENDS ${GEN_DIR}/6502_asm.inl)
    add_dependencies(pico_cpu asmstuff)
    if (PICO_ON_DEVICE)
        target_link_libraries(pico_cpu INTERFACE hardware_interp)
    endif ()
else ()
    target_compile_definitions(pico_cpu INTERFACE THUMB_CPU_USE_ASM=0)
    add_dependencies(pico_cpu cstuff)
endif ()

target_include_directories(pico_cpu_common INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)

target_link_libraries(pico_cpu INTERFACE pico_cpu_common)

if (TARGET pico_cpu_no_asm)
    target_compile_definitions(pico_cpu_no_asm INTERFACE THUMB_CPU_USE_ASM=0)
    add_dependencies(pico_cpu_no_asm cstuff)
    target_link_libraries(pico_cpu_no_asm INTERFACE pico_cpu_common)
endif()

