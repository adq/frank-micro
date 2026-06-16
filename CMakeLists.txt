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

# Optional USB HID host (keyboard + gamepad).  When enabled, the native USB
# port is owned by the TinyUSB host stack, so USB-CDC stdio (serial console)
# and the boot-time USB-enumeration delay are disabled.  Off by default: the
# native USB port is then used for USB-CDC printf instead (PS/2 only input).
option(USB_HID_ENABLED "Enable USB HID host (keyboard + gamepad)" OFF)

message(STATUS "frank-micro  Platform: ${PLATFORM}  CPU: ${CPU_SPEED} MHz  Flash: ${FLASH_SPEED} MHz  Voltage: ${CPU_VOLTAGE}  HDMI: ${HDMI_DRIVER}  USB_HID: ${USB_HID_ENABLED}")

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

    # Use the STANDARD 32000 Hz HDMI audio rate (CEA-861).  Non-standard rates
    # such as the BBC SN76489's native 31250 Hz are mishandled by real HDMI
    # sinks (their audio clock does not lock to our Clock-Regeneration, so they
    # drop a sample every few seconds).  b-em produces at 31250 Hz and the
    # producer bridge (frank_audio.c) resamples to 32000 Hz.  For 32000 Hz the
    # default N=4096 gives CTS=25200 exactly.  (Matches the working frank-cpc.)
    target_compile_definitions(frank_hdmi_sound INTERFACE FRANK_HDMI_AUDIO_RATE=32000)

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

# Use the hand-written Thumb-assembly 6502 + hw_event scheduler (fast path).
# Set to OFF to fall back to the portable C interpreter (src/bem/6502.c).
option(USE_THUMB_CPU "Use thumb_cpu asm 6502 + hw_event scheduler" ON)

set(THUMB_DIR ${BEM_DIR}/thumb_cpu)
set(THUMB_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/thumb_gen)

if(USE_THUMB_CPU)
    file(MAKE_DIRECTORY ${THUMB_GEN_DIR})
    find_program(RUBY_EXECUTABLE ruby REQUIRED)
    add_custom_command(
        OUTPUT ${THUMB_GEN_DIR}/6502_asm.inl ${THUMB_GEN_DIR}/6502_c.inl
        DEPENDS ${THUMB_DIR}/gen/gen_asm.rb
        WORKING_DIRECTORY ${THUMB_DIR}
        COMMAND ${RUBY_EXECUTABLE} gen/gen_asm.rb
                ${THUMB_GEN_DIR}/6502_asm.inl ${THUMB_GEN_DIR}/6502_c.inl
        COMMENT "Generating thumb_cpu 6502 tables"
        VERBATIM
    )
    set(CPU_SOURCES
        ${THUMB_DIR}/src/cpu/cpu.c
        ${THUMB_DIR}/src/cpu/cpu_asm.S
        ${THUMB_DIR}/src/cpu_mem.c
        ${THUMB_DIR}/src/adapter.c
        ${THUMB_GEN_DIR}/6502_asm.inl
        ${THUMB_GEN_DIR}/6502_c.inl
    )
else()
    set(CPU_SOURCES ${BEM_DIR}/6502.c)
endif()

set(BBC_ENGINE
    # 6502 CPU (thumb asm + hw_event, or portable C interpreter)
    ${CPU_SOURCES}
    # b-em core (subset; unused features compiled out via NO_USE_* guards)
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
    ${BEM_DIR}/pico/sector_read/sector_read.c
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
    src/frank/frank_disc.c       # load SSD from SD card -> b-em disc
    src/frank/frank_console.c    # USB-serial key injection (headless control)
    src/frank/frank_settings.c   # runtime settings store + INI persistence
    src/frank/frank_loader.c     # SD disc image browser
    src/frank/frank_ui.c         # F11 media browser + F12 settings overlay
    src/Pico/ui_draw.c           # 8bpp framebuffer drawing primitives
    src/Pico/ui_font.c           # 6x8 bitmap font for the overlay
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
    src/bem/pico/sector_read
    src/bem/thumb_cpu/src
    src/bem/thumb_cpu/src/cpu
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
    MODEL_MASTER          # boot as BBC Master 128 by default
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
    USE_SECTOR_READ       # disc images streamed sector-by-sector from SD
    # -DFRANK_NO_AUTOBOOT=1 to skip the Prince of Persia disc autoboot
    $<$<BOOL:${FRANK_NO_AUTOBOOT}>:FRANK_NO_AUTOBOOT>
    PIOC_PRINTF_SUPPORT_FLOAT=0 PIOC_PRINTF_SUPPORT_LONG_LONG=0
)

if(USE_THUMB_CPU)
    # Thumb-assembly 6502 + event-driven hardware scheduler.
    target_compile_definitions(frank-micro PRIVATE
        USE_PICO_CPU
        USE_HW_EVENT
        THUMB_CPU_BEEB
        THUMB_CPU_USE_ASM=1
        CPU_ASM_INLINE_BREAKOUT
    )
    target_include_directories(frank-micro PRIVATE ${THUMB_GEN_DIR})
    target_link_libraries(frank-micro hardware_interp)
endif()

if(HDMI_DRIVER STREQUAL "HDMI_PIO_AUDIO")
    target_compile_definitions(frank-micro PRIVATE HDMI_PIO_AUDIO=1)
endif()

if(USB_HID_ENABLED)
    target_include_directories(frank-micro PRIVATE drivers/usbhid)
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

if(USB_HID_ENABLED)
    target_link_libraries(frank-micro usbhid)
endif()

pico_enable_stdio_uart(frank-micro 0)

# Dev builds (USB_HID off) use USB-CDC stdio for printf + the serial console;
# release builds (USB_HID on) disable it because the USB HID host needs
# exclusive use of the native USB controller.
if(USB_HID_ENABLED)
    pico_enable_stdio_usb(frank-micro 0)
else()
    pico_enable_stdio_usb(frank-micro 1)
endif()

# Never let a printf() to USB-CDC block Core 0: if the host has the port
# open but is not draining it, the default 500 ms blocking write stalls the
# audio producer and causes a recurring dropout.  0 = drop chars instead.
target_compile_definitions(frank-micro PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)

pico_add_extra_outputs(frank-micro)
target_link_options(frank-micro PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)
