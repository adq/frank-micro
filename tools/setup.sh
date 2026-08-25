#!/usr/bin/env bash
#
# frank-micro — BBC Micro for RP2350
#
# Copyright (c) 2026 Andrew de Quincey <adq@lidskialf.net>
# https://github.com/rh1tech/frank-micro
# SPDX-License-Identifier: GPL-3.0-or-later
#
#
# One-shot development environment setup.  Installs host packages, initialises
# the frank-hdmi-sound submodule, clones the Pico SDK and initialises the SDK's
# own tinyusb submodule.  Safe to re-run.
#
# Usage: tools/setup.sh
#        PICO_SDK_DIR=/opt/pico-sdk tools/setup.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${PICO_SDK_DIR:-$HOME/dev/pico-sdk}"
SDK_TAG="2.2.0"          # matches sdkVersion at CMakeLists.txt:7
SDK_URL="https://github.com/raspberrypi/pico-sdk"

step() { printf '\n==> %s\n' "$1"; }
die()  { printf 'error: %s\n' "$1" >&2; exit 1; }

# ---------------------------------------------------------------- host packages
step "Checking host packages"

pkgs=()
need() { pkgs+=("$1"); echo "  missing: $2 (package $1)"; }

command -v git   >/dev/null 2>&1 || need git        git
command -v make  >/dev/null 2>&1 || need base-devel make
command -v cmake >/dev/null 2>&1 || need cmake      cmake
command -v arm-none-eabi-gcc >/dev/null 2>&1 || need arm-none-eabi-gcc arm-none-eabi-gcc

# Ruby generates the 6502 dispatch tables (CMakeLists.txt:319) and the generator
# does `require 'erb'`.  Arch's base ruby package omits erb, which fails the
# build at 56% with "cannot load such file -- erb".
if command -v ruby >/dev/null 2>&1; then
    ruby -e "require 'erb'" >/dev/null 2>&1 || need ruby-erb "ruby's erb"
else
    need ruby ruby
    need ruby-erb "ruby's erb"
fi

# On Arch, newlib is only an optional dependency of arm-none-eabi-gcc, so the
# compiler can be installed with no C library at all and the first link fails
# with "cannot find -lc".
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    libc="$(arm-none-eabi-gcc -print-file-name=libc.a)"
    [ -f "$libc" ] || need arm-none-eabi-newlib "ARM C library (libc.a)"
else
    need arm-none-eabi-newlib "ARM C library (libc.a)"
fi

if [ ${#pkgs[@]} -eq 0 ]; then
    echo "  all present"
elif command -v pacman >/dev/null 2>&1; then
    echo "  installing: ${pkgs[*]}"
    sudo pacman -S --needed "${pkgs[@]}"
else
    echo
    echo "No pacman on this system.  Install the equivalents by hand, then re-run."
    echo "  Arch:   sudo pacman -S --needed ${pkgs[*]}"
    echo "  Debian: sudo apt install build-essential cmake gcc-arm-none-eabi \\"
    echo "                           libnewlib-arm-none-eabi ruby ruby-dev"
    die "missing host packages"
fi

# ------------------------------------------------------------- repo submodules
step "Initialising repository submodules"
git -C "$REPO" submodule update --init --recursive
# CMakeLists.txt:142 does add_subdirectory(frank-hdmi-sound/src), so this is the
# exact file whose absence breaks the default build.
[ -f "$REPO/frank-hdmi-sound/src/CMakeLists.txt" ] \
    || die "frank-hdmi-sound submodule is empty; the default HDMI_PIO_AUDIO build needs it"
echo "  frank-hdmi-sound ok"

# ------------------------------------------------------------------- pico sdk
step "Setting up the Pico SDK at $SDK"
if [ -f "$SDK/pico_sdk_init.cmake" ]; then
    echo "  already present"
elif [ -e "$SDK" ]; then
    die "$SDK exists but does not contain the Pico SDK; move it or set PICO_SDK_DIR"
else
    mkdir -p "$(dirname "$SDK")"
    git clone -b "$SDK_TAG" --depth 1 "$SDK_URL" "$SDK"
fi

# Only tinyusb.  btstack, lwip and cyw43-driver are large and unused here.  If
# tinyusb is missing the build still SUCCEEDS but pico_enable_stdio_usb() is a
# no-op, so the firmware has no console at all.
step "Initialising the SDK's tinyusb submodule"
git -C "$SDK" submodule update --init --depth 1 lib/tinyusb
[ -f "$SDK/lib/tinyusb/src/tusb.h" ] \
    || die "$SDK/lib/tinyusb is empty; the build would silently produce firmware with no console"
echo "  tinyusb ok"

# The Fruit Jam board header arrived in SDK 2.2.0.  Without it PLATFORM=fj fails
# at configure time on an unknown PICO_BOARD, which is a confusing way to find
# out the SDK is too old.
step "Checking board support"
if [ -f "$SDK/src/boards/include/boards/adafruit_fruit_jam.h" ]; then
    echo "  adafruit_fruit_jam ok"
else
    echo "  WARNING: this SDK has no adafruit_fruit_jam board header."
    echo "           PLATFORM=fj needs SDK 2.2.0 or newer; the other three"
    echo "           platforms are unaffected."
fi

# Pico-PIO-USB is fetched by the recursive submodule update above.  It is the
# only USB host path on the Fruit Jam, whose USB-A ports hang off PIO pins
# rather than the native controller, so an empty checkout fails at link time
# with nothing pointing at the cause.
if [ -f "$REPO/lib/Pico-PIO-USB/src/pio_usb.c" ]; then
    echo "  Pico-PIO-USB ok"
else
    echo "  WARNING: lib/Pico-PIO-USB is empty."
    echo "           PLATFORM=fj with USB_HID=1 will not link; every other"
    echo "           build is unaffected."
fi

# ---------------------------------------------------------------------- report
cat <<EOF

Setup complete.  To build:

  export PICO_SDK_PATH=$SDK
  ./build.sh

Note: flash.sh needs picotool, which is not in the Arch repositories.  Install it
from the AUR or build it from source.  It is not required to build: you can
instead hold BOOTSEL while plugging in USB and copy build/frank-micro.uf2 onto
the RPI-RP2 drive that appears.

openocd (Arch: openocd) is also worth having.  A release build has no serial
console, so SWD over the debug connector is then the only way to read a fault.
EOF
