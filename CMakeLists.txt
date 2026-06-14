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

# ── BBC Micro (b-em) engine sources ────────────────────────────────────────
set(BEM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/bem)

set(BBC_ENGINE
    # b-em core (subset; unused features compiled out via NO_USE_* guards)
    ${BEM_DIR}/6502.c
    ${BEM_DIR}/acia.c
    ${BEM_DIR}/adc.c
    ${BEM_DIR}/cmos.c
    ${BEM_DIR}/compactcmos.c
    ${BEM_DIR}/compact_joystick.c
    ${BEM_DIR}/compat_wrappers.c
    ${BEM_DIR}/config.c
    ${BEM_DIR}/csw.c
    ${BEM_DIR}/ddnoise.c
    ${BEM_DIR}/disc.c
    ${BEM_DIR}/fdi.c
    ${BEM_DIR}/fdi2raw.c
    ${BEM_DIR}/i8271.c
    ${BEM_DIR}/ide.c
    ${BEM_DIR}/joystick.c
    ${BEM_DIR}/keyboard.c
    ${BEM_DIR}/logging.c
    ${BEM_DIR}/main.c
    ${BEM_DIR}/mem.c
    ${BEM_DIR}/model.c
    ${BEM_DIR}/mouse.c
    ${BEM_DIR}/music2000.c
    ${BEM_DIR}/music4000.c
    ${BEM_DIR}/music5000.c
    ${BEM_DIR}/pal.c
    ${BEM_DIR}/scsi.c
    ${BEM_DIR}/sdf-acc.c
    ${BEM_DIR}/sdf-geo.c
    ${BEM_DIR}/serial.c
    ${BEM_DIR}/sn76489.c
    ${BEM_DIR}/sysacia.c
    ${BEM_DIR}/sysvia.c
    ${BEM_DIR}/tape.c
    ${BEM_DIR}/tapenoise.c
    ${BEM_DIR}/uef.c
    ${BEM_DIR}/uservia.c
    ${BEM_DIR}/via.c
    ${BEM_DIR}/wd1770.c
    ${BEM_DIR}/win.c
    # linux.c omitted: host filesystem helpers not used on pico
    # pico glue: CRTC/ULA timing + rasteriser (X_GUI raw-row path)
    ${BEM_DIR}/pico/video/video.c
    ${BEM_DIR}/pico/video/display.c
    ${BEM_DIR}/pico/video/pixels.S
    ${BEM_DIR}/pico/video/gui_stub.c
    ${BEM_DIR}/pico/audio/audio.c
    ${BEM_DIR}/pico/stub_allegro5/al_stub.cpp
    # embedded ROMs (Model B: os12, basic2, dfs226)
    ${BEM_DIR}/pico/roms/embedded_roms.c
)

set(JIT_STUBS)

# ── frank port layer (b-em glue) ────────────────────────────────────────────
set(PICO_PORT
    src/frank/frank_platform.c   # main(), board/clock/SD init, run loop
    src/frank/frank_gui.c        # x_gui_* hooks: 16bpp rows -> 8bpp HDMI fb
    src/frank/frank_keyboard.c   # PS/2 -> b-em key matrix
    src/frank/frank_audio.c      # sn76489 -> frank HDMI audio ring
    src/frank/frank_stubs.c      # host-symbol stubs (midi, wav, garbage mem)
)

add_executable(frank-micro
    ${BBC_ENGINE}
    ${JIT_STUBS}
    ${PICO_PORT}
)

target_include_directories(frank-micro PRIVATE
    src
    src/Pico
    src/frank
    src/frank/vendor
    src/bem
    src/bem/pico
    src/bem/pico/video
    src/bem/pico/audio
    src/bem/pico/x_gui
    src/bem/pico/stub_allegro5/include
    src/bem/pico/roms
    src/bem/thumb_cpu/src
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
    # ── b-em build config ───────────────────────────────────────────────
    BEM
    USE_MEMORY_POINTER
    HAVE_STPCPY
    VERSION=\"frank-micro\"
    X_GUI                 # use x_gui.h raw 16bpp row abstraction (not scanvideo)
    SINGLE_CORE           # core1 is the frank HDMI encoder; no core1 video
    FRANK_MAIN            # disable al_stub main(); frank provides main()
    NO_USE_KEYCODE_MAP
    # feature trims (match kilograham pico defaults)
    NO_USE_TUBE NO_USE_DEBUGGER NO_USE_SID NO_USE_SAVE_STATE NO_USE_VDFS
    NO_USE_UEF NO_USE_CSW NO_USE_MMB NO_USE_IDE NO_USE_ADC NO_USE_MOUSE
    NO_USE_MUSIC5000 NO_USE_SCSI NO_USE_FDI NO_USE_I8271 NO_USE_ALLEGRO_GUI
    NO_USE_JOYSTICK NO_USE_PAL NO_USE_COMPACT NO_USE_CMD_LINE
    NO_USE_SOUND_FILTER NO_USE_SET_SPEED NO_USE_NULA_ATTRIBUTE
    NO_USE_NULA_PIXEL_HSCROLL NO_USE_DD_NOISE NO_USE_WRITABLE_CONFIG
    NO_USE_CLOSE NO_USE_CMOS_SAVE NO_USE_RAM_ROMS NO_USE_MIDI
    # rasteriser options (match kilograham)
    TILE8 MODE7_TILE8 USE_CRTC0_0_OPTIMIZATION LOOK_MA_NO_MA
    USE_MEM_DDNOISE USE_DDNOISE_8
    NO_USE_KEY_LOOKUP NO_USE_DISC_WRITE SHORTCIRCUIT_KEYBOARD_POLL
    PIOC_PRINTF_SUPPORT_FLOAT=0 PIOC_PRINTF_SUPPORT_LONG_LONG=0
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

# b-em sources include C++ (al_stub) — already enabled at project() level.

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
