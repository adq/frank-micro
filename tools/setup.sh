#!/bin/bash
#
# frank-micro — BBC Micro for RP2350
#
# Copyright (c) 2026 Andrew de Quincey <adq@lidskialf.net>
# https://github.com/rh1tech/frank-micro
# SPDX-License-Identifier: GPL-3.0-or-later
#
#
# tools/setup.sh — check and prepare everything a build needs.
#
# Initialises the git submodules, locates the Pico SDK and writes its path to
# tools/env.sh, then reports any tool that is missing.  Exits non-zero if a
# build cannot succeed, so it can be used as a gate.
#
# Usage:
#   ./tools/setup.sh          check and fix
#   ./tools/setup.sh --check  check only, change nothing
#
# After a successful run:
#   . tools/env.sh && ./build.sh
#

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$SCRIPT_DIR/env.sh"

CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

BLOCKERS=0
WARNINGS=0

ok()    { echo -e "  ${GREEN}ok${NC}      $*"; }
fixed() { echo -e "  ${CYAN}fixed${NC}   $*"; }
warn()  { echo -e "  ${YELLOW}warn${NC}    $*"; WARNINGS=$((WARNINGS + 1)); }
fail()  { echo -e "  ${RED}BLOCKED${NC} $*"; BLOCKERS=$((BLOCKERS + 1)); }

echo ""
echo "frank-micro setup"
echo "repository: $REPO_DIR"
[ "$CHECK_ONLY" = 1 ] && echo "mode: check only, nothing will be changed"
echo ""

# ------------------------------------------------------------------ #
# 1. Submodules                                                      #
# ------------------------------------------------------------------ #
echo "Submodules"

submodule_init() {
    # $1 = submodule path relative to the repository root
    ( cd "$REPO_DIR" && git submodule update --init --recursive "$1" ) >/dev/null 2>&1
}

# frank-hdmi-sound supplies the default HDMI_PIO_AUDIO video and audio driver.
# CMakeLists.txt does add_subdirectory(frank-hdmi-sound/src), so an
# uninitialised submodule fails at configure time, not at link time.
if [ -f "$REPO_DIR/frank-hdmi-sound/src/CMakeLists.txt" ]; then
    ok "frank-hdmi-sound"
elif [ "$CHECK_ONLY" = 1 ]; then
    fail "frank-hdmi-sound not initialised (run without --check)"
else
    if submodule_init frank-hdmi-sound && [ -f "$REPO_DIR/frank-hdmi-sound/src/CMakeLists.txt" ]; then
        fixed "frank-hdmi-sound initialised"
    else
        fail "frank-hdmi-sound could not be initialised; needs network access to github.com/rh1tech/frank-hdmi-audio"
    fi
fi

# Pico-PIO-USB is the USB host path on boards whose USB-A ports hang off PIO
# rather than the native controller, which is every Fruit Jam port.  Only
# needed for a USB_HID build, so its absence is a warning and not a blocker.
if [ -f "$REPO_DIR/lib/Pico-PIO-USB/src/pio_usb.c" ]; then
    ok "Pico-PIO-USB"
elif ! grep -q "Pico-PIO-USB" "$REPO_DIR/.gitmodules" 2>/dev/null; then
    warn "Pico-PIO-USB not declared as a submodule yet; only needed for USB_HID=1 on PLATFORM=fj"
elif [ "$CHECK_ONLY" = 1 ]; then
    warn "Pico-PIO-USB not initialised; only needed for USB_HID=1 on PLATFORM=fj"
else
    if submodule_init lib/Pico-PIO-USB && [ -f "$REPO_DIR/lib/Pico-PIO-USB/src/pio_usb.c" ]; then
        fixed "Pico-PIO-USB initialised"
    else
        warn "Pico-PIO-USB could not be initialised; USB_HID=1 on PLATFORM=fj will not link"
    fi
fi
echo ""

# ------------------------------------------------------------------ #
# 2. Pico SDK                                                        #
# ------------------------------------------------------------------ #
echo "Pico SDK"

find_sdk() {
    local candidate
    for candidate in \
        "${PICO_SDK_PATH:-}" \
        "$HOME/dev/pico-sdk" \
        "$HOME/pico/pico-sdk" \
        "$HOME/pico-sdk" \
        "/opt/pico-sdk" \
        "/usr/share/pico-sdk"
    do
        [ -n "$candidate" ] || continue
        if [ -f "$candidate/pico_sdk_init.cmake" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

SDK_PATH="$(find_sdk)" || SDK_PATH=""

if [ -z "$SDK_PATH" ]; then
    fail "Pico SDK not found. Clone it and export PICO_SDK_PATH, or put it at ~/dev/pico-sdk"
else
    SDK_VER="$(sed -n 's/^[[:space:]]*set(PICO_SDK_VERSION_\(MAJOR\|MINOR\|REVISION\) \([0-9]*\))/\2/p' \
               "$SDK_PATH/pico_sdk_version.cmake" 2>/dev/null | paste -sd. -)"
    ok "found at $SDK_PATH (version ${SDK_VER:-unknown})"

    # The Fruit Jam board header arrived in SDK 2.2.0.  Without it a
    # PLATFORM=fj configure fails on an unknown PICO_BOARD.
    if [ -f "$SDK_PATH/src/boards/include/boards/adafruit_fruit_jam.h" ]; then
        ok "adafruit_fruit_jam board header present"
    else
        warn "SDK has no adafruit_fruit_jam board header; PLATFORM=fj needs SDK 2.2.0 or newer"
    fi

    # Without lib/tinyusb, pico_enable_stdio_usb() is a silent no-op: CMake
    # prints one warning, the build succeeds, and the firmware has no console
    # at all.  See CLAUDE.md section 5.
    if [ -f "$SDK_PATH/lib/tinyusb/src/tusb.h" ]; then
        ok "SDK lib/tinyusb initialised"
    else
        fail "SDK lib/tinyusb not initialised. Without it every printf goes nowhere and the build still succeeds. In $SDK_PATH run: git submodule update --init"
    fi

    if [ "$CHECK_ONLY" = 0 ]; then
        {
            echo "# Written by tools/setup.sh — not tracked in git."
            echo "# Source this before building:  . tools/env.sh"
            echo "export PICO_SDK_PATH=\"$SDK_PATH\""
        } > "$ENV_FILE"
        fixed "wrote $ENV_FILE"
    fi

    if [ "${PICO_SDK_PATH:-}" != "$SDK_PATH" ]; then
        warn "PICO_SDK_PATH is not exported in this shell; run: . tools/env.sh"
    fi
fi
echo ""

# ------------------------------------------------------------------ #
# 3. Toolchain                                                       #
# ------------------------------------------------------------------ #
echo "Toolchain"

if command -v cmake >/dev/null 2>&1; then
    ok "cmake $(cmake --version | head -1 | awk '{print $3}')"
else
    fail "cmake missing (Arch: cmake)"
fi

if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    ok "arm-none-eabi-gcc $(arm-none-eabi-gcc -dumpversion)"
else
    fail "arm-none-eabi-gcc missing (Arch: arm-none-eabi-gcc)"
fi

# newlib is an *optional* dependency of arm-none-eabi-gcc on Arch, so the
# compiler alone gives "cannot find -lc" at the first link.
if arm-none-eabi-gcc -print-file-name=libc.a 2>/dev/null | grep -q '/'; then
    ok "arm-none-eabi C library"
else
    fail "no C library for the ARM toolchain; the first link fails with 'cannot find -lc' (Arch: arm-none-eabi-newlib)"
fi

# CMakeLists.txt generates the 6502 dispatch tables with a Ruby ERB template.
# The base ruby package on Arch omits erb and the build dies at 56 percent.
if command -v ruby >/dev/null 2>&1; then
    if ruby -e "require 'erb'" >/dev/null 2>&1; then
        ok "ruby $(ruby -e 'print RUBY_VERSION') with erb"
    else
        fail "ruby has no erb; the 6502 table generator dies with 'cannot load such file -- erb' (Arch: ruby-erb)"
    fi
else
    fail "ruby missing; needed to generate the 6502 dispatch tables (Arch: ruby ruby-erb)"
fi
echo ""

# ------------------------------------------------------------------ #
# 4. Flashing and debugging                                          #
# ------------------------------------------------------------------ #
echo "Flashing and debugging (neither is required to build)"

if command -v picotool >/dev/null 2>&1; then
    ok "picotool, so ./flash.sh works"
else
    warn "picotool missing, so ./flash.sh will not run (Arch AUR: picotool). Instead hold BOOTSEL while plugging in USB and copy build/frank-micro.uf2 to the RPI-RP2 drive"
fi

if command -v openocd >/dev/null 2>&1; then
    ok "openocd"
else
    warn "openocd missing, so there is no SWD path (Arch: openocd). Release builds have no serial console, so SWD is the only way to read a fault"
fi
echo ""

# ------------------------------------------------------------------ #
# Summary                                                            #
# ------------------------------------------------------------------ #
if [ "$BLOCKERS" -gt 0 ]; then
    echo -e "${RED}$BLOCKERS blocker(s), $WARNINGS warning(s). A build will fail.${NC}"
    echo ""
    exit 1
fi

if [ "$WARNINGS" -gt 0 ]; then
    echo -e "${GREEN}No blockers${NC}, $WARNINGS warning(s)."
else
    echo -e "${GREEN}Everything present.${NC}"
fi
echo ""
echo "Next:  . tools/env.sh && PLATFORM=fj ./build.sh"
echo ""
exit 0
