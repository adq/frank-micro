# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
#
# frank-micro — BBC Micro for RP2350
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-micro
# SPDX-License-Identifier: GPL-3.0-or-later
#

cmake_minimum_required(VERSION 3.13)

# Platform selection: m1, m2, z0
set(PLATFORM "m2" CACHE STRING "Target platform: m1, m2, z0")
set_property(CACHE PLATFORM PROPERTY STRINGS m1 m2 z0)

if(NOT PLATFORM STREQUAL "m1" AND NOT PLATFORM STREQUAL "m2" AND NOT PLATFORM STREQUAL "z0")
    message(FATAL_ERROR "Unknown PLATFORM '${PLATFORM}' — must be m1, m2, or z0")
endif()

set(CPU_SPEED "252" CACHE STRING "CPU clock in MHz")
set(PSRAM_SPEED "100" CACHE STRING "PSRAM max frequency in MHz (unused, no PSRAM required)")
set(FLASH_SPEED "66"  CACHE STRING "Flash target max frequency in MHz")

if(PLATFORM STREQUAL "z0")
    set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards CACHE PATH "" FORCE)
    set(PICO_BOARD "waveshare_rp2350_pizero" CACHE STRING "Board type" FORCE)
    set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "Pico platform" FORCE)
    if(CPU_SPEED GREATER "300")
        set(CPU_SPEED "252" CACHE STRING "CPU clock in MHz" FORCE)
    endif()
else()
    set(PICO_BOARD "pico2" CACHE STRING "Board type" FORCE)
    set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "Pico platform" FORCE)
endif()

if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 300)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

if(PLATFORM STREQUAL "m1")
    set(PLATFORM_DEF PLATFORM_M1)
    set(BOARD_DEF BOARD_M1)
    set(SD_CS 5)
    set(SD_SCK 2)
    set(SD_MOSI 3)
    set(SD_MISO 4)
    set(HDMI_PIN 6)
elseif(PLATFORM STREQUAL "z0")
    set(PLATFORM_DEF PLATFORM_Z0)
    set(BOARD_DEF BOARD_Z0)
    set(SD_CS 43)
    set(SD_SCK 30)
    set(SD_MOSI 31)
    set(SD_MISO 40)
    set(HDMI_PIN 32)
else()
    # m2
    set(PLATFORM_DEF PLATFORM_M2)
    set(BOARD_DEF BOARD_M2)
    set(SD_CS 5)
    set(SD_SCK 6)
    set(SD_MOSI 7)
    set(SD_MISO 4)
    set(HDMI_PIN 12)
endif()

# Video driver: HDMI_PIO_AUDIO is the target, VGA fallback via runtime detection
set(HDMI_DRIVER "HDMI_PIO_AUDIO" CACHE STRING "HDMI driver: HDMI_PIO_AUDIO or HDMI_PIO")
set_property(CACHE HDMI_DRIVER PROPERTY STRINGS "HDMI_PIO_AUDIO" "HDMI_PIO")

message(STATUS "frank-micro  Platform: ${PLATFORM}  CPU: ${CPU_SPEED} MHz  Flash: ${FLASH_SPEED} MHz  Voltage: ${CPU_VOLTAGE}  HDMI: ${HDMI_DRIVER}")

include(pico_sdk_import.cmake)

project(frank-micro C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" FRANK_MICRO_VERSION_RAW)
string(STRIP "${FRANK_MICRO_VERSION_RAW}" FRANK_MICRO_VERSION_RAW)
string(REPLACE " " "." FRANK_MICRO_VERSION_STR "${FRANK_MICRO_VERSION_RAW}")

add_subdirectory(drivers/fatfs)
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/ps2)
add_subdirectory(drivers/nespad)
add_subdirectory(drivers/usbhid)

target_compile_definitions(ps2_driver PRIVATE ${PLATFORM_DEF}=1)

if(HDMI_DRIVER STREQUAL "HDMI_PIO_AUDIO")
    if(CPU_SPEED GREATER 252)
        set(FRANK_HDMI_SM_CLKDIV "2" CACHE STRING "" FORCE)
    else()
        set(FRANK_HDMI_SM_CLKDIV "1" CACHE STRING "" FORCE)
    endif()
    add_subdirectory(frank-hdmi-sound/src)

    add_library(drivers
        drivers/HDMI_audio.c
        drivers/HDMI_vga.c
        drivers/test_pins.c
        drivers/crash_handler.c
        drivers/audio.c
        drivers/audio_i2s_opt.S
        drivers/pwm_audio/pwm_audio.c
        drivers/psram_init.c
        drivers/psram_allocator.c
        drivers/dlmalloc.c
        drivers/uart_logging.c
    )
    target_include_directories(drivers PUBLIC drivers drivers/pwm_audio src)
    target_compile_definitions(drivers PUBLIC
        ${BOARD_DEF}
        ${PLATFORM_DEF}=1
        HDMI_PIO_AUDIO=1
    )
    target_compile_definitions(drivers PRIVATE
        PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    )
    pico_generate_pio_header(drivers ${CMAKE_CURRENT_LIST_DIR}/drivers/audio_i2s.pio)
    target_link_libraries(drivers
        pico_stdlib
        hardware_dma
        hardware_pio
        hardware_pwm
        hardware_spi
        hardware_gpio
        hardware_clocks
        hardware_irq
        hardware_vreg
        hardware_exception
        hardware_watchdog
        frank_hdmi_sound
    )
else()
    # HDMI_PIO: original PIO HDMI driver + I2S audio
    add_library(drivers
        drivers/HDMI.c
        drivers/hdmi_scanline.S
        drivers/HDMI_vga.c
        drivers/test_pins.c
        drivers/crash_handler.c
        drivers/audio.c
        drivers/audio_i2s_opt.S
        drivers/pwm_audio/pwm_audio.c
        drivers/psram_init.c
        drivers/psram_allocator.c
        drivers/dlmalloc.c
        drivers/uart_logging.c
    )
    target_include_directories(drivers PUBLIC drivers drivers/pwm_audio src)
    target_compile_definitions(drivers PUBLIC
        ${BOARD_DEF}
        ${PLATFORM_DEF}=1
        HDMI_PIO=1
    )
    target_compile_definitions(drivers PRIVATE
        PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    )
    pico_generate_pio_header(drivers ${CMAKE_CURRENT_LIST_DIR}/drivers/audio_i2s.pio)
    target_link_libraries(drivers
        pico_stdlib
        hardware_dma
        hardware_pio
        hardware_pwm
        hardware_spi
        hardware_gpio
        hardware_clocks
        hardware_irq
        hardware_vreg
        hardware_exception
        hardware_watchdog
    )
endif()

set_source_files_properties(drivers/dlmalloc.c PROPERTIES COMPILE_DEFINITIONS
    "ONLY_MSPACES=1;HAVE_MMAP=0;HAVE_MORECORE=0;NO_MALLOC_STATS=1;USE_LOCKS=0;ABORT_ON_ASSERT_FAILURE=0;MALLOC_FAILURE_ACTION=;LACKS_SYS_MMAN_H=1;LACKS_UNISTD_H=1;LACKS_SYS_PARAM_H=1")

# ── BBC Micro (beebjit) engine sources ─────────────────────────────────────
set(BEEBJIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/micro)

set(BBC_ENGINE
    # Core BBC hardware
    ${BEEBJIT_DIR}/bbc.c
    ${BEEBJIT_DIR}/config.c
    ${BEEBJIT_DIR}/cmos.c
    # 6502 CPU — interpreter only (no JIT)
    ${BEEBJIT_DIR}/cpu_driver.c
    ${BEEBJIT_DIR}/interp.c
    ${BEEBJIT_DIR}/defs_6502.c
    ${BEEBJIT_DIR}/state_6502.c
    # Hardware subsystems
    ${BEEBJIT_DIR}/via.c
    ${BEEBJIT_DIR}/sound.c
    ${BEEBJIT_DIR}/video.c
    ${BEEBJIT_DIR}/teletext.c
    ${BEEBJIT_DIR}/keyboard.c
    ${BEEBJIT_DIR}/joystick.c
    ${BEEBJIT_DIR}/adc.c
    ${BEEBJIT_DIR}/timing.c
    ${BEEBJIT_DIR}/mc6850.c
    ${BEEBJIT_DIR}/serial_ula.c
    # Disk subsystems
    ${BEEBJIT_DIR}/disc.c
    ${BEEBJIT_DIR}/disc_drive.c
    ${BEEBJIT_DIR}/disc_ssd.c
    ${BEEBJIT_DIR}/disc_adl.c
    ${BEEBJIT_DIR}/disc_hfe.c
    ${BEEBJIT_DIR}/disc_tool.c
    ${BEEBJIT_DIR}/ibm_disc_format.c
    ${BEEBJIT_DIR}/intel_fdc.c
    ${BEEBJIT_DIR}/wd_fdc.c
    # Tape
    ${BEEBJIT_DIR}/tape.c
    ${BEEBJIT_DIR}/tape_uef.c
    ${BEEBJIT_DIR}/tape_csw.c
    # Utilities
    ${BEEBJIT_DIR}/log.c
    ${BEEBJIT_DIR}/util.c
    ${BEEBJIT_DIR}/util_string.c
    ${BEEBJIT_DIR}/util_container.c
    ${BEEBJIT_DIR}/expression.c
    ${BEEBJIT_DIR}/miniz.c
    ${BEEBJIT_DIR}/asm/asm_abi.c
    ${BEEBJIT_DIR}/asm/asm_tables.c
    ${BEEBJIT_DIR}/asm/asm_util.c
    ${BEEBJIT_DIR}/asm/asm_common.c
    ${BEEBJIT_DIR}/asm/asm_jit.c
    ${BEEBJIT_DIR}/asm/asm_inturbo.c
)

# ── JIT stubs (null implementations so link succeeds) ──────────────────────
set(JIT_STUBS
    src/Pico/jit_stub.c
)

# ── Pico port layer ─────────────────────────────────────────────────────────
set(PICO_PORT
    src/Pico/main.c
    src/Pico/platform.c
    src/Pico/pico_render.c
    src/Pico/micro_boot.c
    src/Pico/micro_settings.c
    src/Pico/micro_loader.c
    src/Pico/micro_ui.c
    src/Pico/micro_serial_console.c
    src/Pico/micro_keys.c
    src/Pico/ui_draw.c
    src/Pico/ui_font.c
    # OS abstraction stubs
    src/Pico/os_alloc_pico.c
    src/Pico/os_thread_pico.c
    src/Pico/os_channel_pico.c
    src/Pico/os_lock_pico.c
    src/Pico/os_time_pico.c
    src/Pico/os_sound_pico.c
    src/Pico/os_window_pico.c
    src/Pico/os_terminal_pico.c
    src/Pico/os_poller_pico.c
    src/Pico/os_fault_pico.c
    # Pico replacements for platform-specific beebjit modules
    src/Pico/debug_pico.c
    src/Pico/state_pico.c
    src/Pico/util_file_pico.c
    src/Pico/disc_stub.c
)

add_executable(frank-micro
    ${BBC_ENGINE}
    ${JIT_STUBS}
    ${PICO_PORT}
)

target_include_directories(frank-micro PRIVATE
    src
    src/micro
    src/Pico
    drivers
    drivers/pwm_audio
    drivers/fatfs
    drivers/sdcard
    drivers/ps2
    drivers/nespad
)

if(HDMI_DRIVER STREQUAL "HDMI_PIO_AUDIO")
    target_include_directories(frank-micro PRIVATE frank-hdmi-sound/src)
endif()

target_compile_definitions(frank-micro PRIVATE
    ${BOARD_DEF}
    ${PLATFORM_DEF}=1
    PICO_BUILD=1
    PICO_ON_DEVICE=1
    FRANK_PERF_TIMING_HACKS=1
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    FLASH_MAX_FREQ_MHZ=${FLASH_SPEED}
    LSB_FIRST=1
    FRANK_MICRO_VERSION=\"${FRANK_MICRO_VERSION_STR}\"
    SDCARD_PIN_SPI0_CS=${SD_CS}
    SDCARD_PIN_SPI0_SCK=${SD_SCK}
    SDCARD_PIN_SPI0_MOSI=${SD_MOSI}
    SDCARD_PIN_SPI0_MISO=${SD_MISO}
)

if(HDMI_DRIVER STREQUAL "HDMI_PIO_AUDIO")
    target_compile_definitions(frank-micro PRIVATE HDMI_PIO_AUDIO=1)
endif()

if(PLATFORM STREQUAL "z0")
    target_compile_definitions(frank-micro PRIVATE SDCARD_SPI_BUS=spi1)
    target_link_libraries(frank-micro hardware_spi)
endif()

target_compile_options(frank-micro PRIVATE
    -O2
    -fno-strict-aliasing
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-missing-braces
    -Wno-implicit-fallthrough
    -Wno-pointer-sign
    -Wno-incompatible-pointer-types
    -Wno-char-subscripts
    -Wno-format
    -Wno-parentheses
)

# miniz is optimised separately - it's safe for O2
set_source_files_properties(${BEEBJIT_DIR}/miniz.c PROPERTIES COMPILE_OPTIONS "-O2;-DMINIZ_NO_STDIO=1;-DMINIZ_NO_TIME=1")

target_link_libraries(frank-micro
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_irq
    hardware_vreg
    hardware_clocks
    sdcard
    fatfs
    drivers
    ps2_driver
    nespad
)

pico_enable_stdio_uart(frank-micro 0)
pico_enable_stdio_usb(frank-micro 1)

pico_add_extra_outputs(frank-micro)
target_link_options(frank-micro PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)
