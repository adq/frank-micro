# frank-micro: code investigation

Date: 2026-08-21
Scope: whole repository at commit `c27ffbf`
Method: static reading of the tree, `CMakeLists.txt` and the git history, plus
three parallel sweeps of the build system, the emulation core and the
application layer.

The investigation was carried out first by static reading, then a toolchain was
installed and **the firmware was built successfully**. Build-derived facts are
called out where they appear. No hardware test was performed, so findings that
need a board are marked **needs confirmation** and say what to run.

Building settled three open questions. One of them retracted what had been the
report's highest-severity finding; see finding 1.

Every claim cites a file and line. Nothing in the repository was changed.

---

## Summary

The firmware is a disciplined port. The divergence from vendored upstream code is
about 104 lines across six files, which is the right structure and keeps a
future rebase feasible.

The problems are concentrated in three places: documentation that describes a
previous architecture, source files left behind by a rewrite, and build settings
that do not do what they appear to do.

Ranked by what I would fix first:

1. `PERFORMANCE.md` describes a different emulator that is no longer in the tree.
   Finding 2.
2. 32 of the 34 files in `src/Pico/` cannot compile and are never built.
   Finding 3.
3. The no-SD-card error screen reboots on the watchdog instead of halting.
   Finding 4.
4. A live emulation bug passes the wrong object to the ACIA timer callback.
   Finding 5.
5. Copyrighted ROM and disc images in the tree conflict with redistributing the
   repository under GPLv3. Finding 6.

Finding 1 as originally written has been **retracted**; see below. It was the
only finding that needed a compiler to settle, and the compiler disagreed with
it.

---

## What the project is

`frank-micro` is a BBC Microcomputer emulator, Model B and Master 128, running on
the Raspberry Pi Pico 2 (RP2350). It drives HDMI video with audio embedded in the
HDMI stream, reads disc images from a FAT32 SD card, and takes input from a PS/2
keyboard, NES or SNES gamepads, or USB HID devices. Three boards are supported:
Murmulator 2.0, Murmulator 1.x, and the Waveshare RP2350-PiZero.

Author: Mikhail Matveev. Version 1.00 (`version.txt`). The port is GPLv3
(`LICENSE`); the vendored emulation core keeps its original terms.

The engine is B-em by Tom Walker. The parts that make it fit on a
microcontroller, being the Thumb-assembly 6502 core, the raw-row rasteriser, the
sector-streaming disc loader and the `x_gui` display abstraction, come from
Graham Sanderson's Pico fork. The work original to this repository is the
platform layer in `src/frank/` and `drivers/`.

### Size

| Area | Lines | Files |
|---|---|---|
| `src/bem/` vendored B-em, all subdirectories | 127,943 | 271 |
| `drivers/` | 41,883 | 55 |
| `src/Pico/` | 4,123 | 34 |
| `src/frank/` | 3,446 | 21 |

About 180,000 lines are tracked. Roughly 13,000 are actually compiled, of which
about 3,400 are this project's own application layer.

---

## The rewrite that was never finished

This explains findings 2 and 3, so it comes first.

The project began as a port of **beebjit**, a different BBC Micro emulator, and
was rewritten onto **B-em**. The beebjit core was deleted. Its platform layer and
its documentation were not.

| Commit | Effect |
|---|---|
| `4ca6c4f` initial commit | project starts as a beebjit port. `src/micro/` holds the beebjit core, `src/Pico/` is its platform layer |
| `f27d3aa` | adds `PERFORMANCE.md` as a handover note for the beebjit work |
| `2c71dad` | deletes the beebjit core, imports B-em into `src/bem/`, leaves `src/Pico/` and `PERFORMANCE.md` behind |
| `c149034` | the emulation speed problem is solved on the B-em side, by the Thumb 6502 core |
| `c567103` | per-file GPL headers are stamped across the tree, including onto the dead files |

---

## Finding 1: the application is built at `-O2` while the drivers are built at `-O3`

Severity: low. **This finding replaces an earlier, wrong version of itself.**

The original claim was that optimisation reached only the `frank-micro` target and
that the driver libraries, holding every video and audio interrupt handler, were
compiled unoptimised. That was wrong, and a configured build settles it.

The Pico SDK sets `CMAKE_BUILD_TYPE=Release` when none is given, so
`CMAKE_C_FLAGS_RELEASE` is `-g -O3 -DNDEBUG` (`build/CMakeCache.txt`). Every
target therefore gets `-O3` and `NDEBUG` by default:

```
CMakeFiles/drivers.dir/flags.make        -DNDEBUG -O3
CMakeFiles/frank-micro.dir/flags.make    -DNDEBUG -O2 -O3
```

The interrupt handlers are optimised at `-O3`. Assertions do **not** ship,
because `NDEBUG` is defined everywhere. Both of my original concerns were
unfounded.

What is left is smaller, and points the other way. For `frank-micro` the raw
flag order is `... -g -O3 -DNDEBUG -std=gnu11 -O2 ...`: the `-O2` from
`target_compile_options` at `CMakeLists.txt:523` comes last and therefore wins.
So the application layer, the B-em emulation core and the Thumb 6502 CPU build
at `-O2`, while the driver libraries build at `-O3`. If anything here is
under-optimised it is the emulation hot path, not the ISRs.

That may well be deliberate, since `-O3` can hurt on a cache-poor target. But it
is not stated anywhere, and it is the opposite of what the explicit `-O2` looks
like it is doing.

**Recommendation.** Decide it explicitly. Either drop the `-O2` and let the
Release flags apply uniformly, or keep it with a comment saying why the
emulation core wants `-O2` when everything else gets `-O3`.

## Finding 2: `PERFORMANCE.md` documents an emulator that is not here

Severity: high. It is the first document a contributor is likely to open after
the README, and following it wastes a working session.

`PERFORMANCE.md:4-5` describes the project as a port of beebjit. Every file it
directs the reader to is gone or dead:

- `src/micro/bbc.c`, cited at `:45`, `:51`, `:54`, `:140`, `:148`, `:150`. There
  is no `src/micro/` directory.
- `src/Pico/main.c:177`, `src/Pico/os_time_pico.c:33-43` and
  `src/Pico/micro_serial_console.c`, named as the files to edit at `:41-42`,
  `:58`, `:95` and `:171`. None are compiled (finding 3).
- beebjit concepts absent from this codebase: `k_cpu_mode_interp`, the `inturbo`
  driver, the x86 JIT, `accurate_flag`, `bbc_cycles_timer_callback`,
  `wakeup_rate`.

Its central premise is obsolete. `:7-8` states the firmware runs at about 28
percent of real time and that emulation speed is the one remaining problem.
Commit `c149034` reports moving from 44 to 106 percent by integrating the Thumb
6502 core, a route the document does not consider. Its hard constraint at `:14`,
never to raise the clock above 252 MHz, is also contradicted by the composite
driver, which forces 378 MHz at `CMakeLists.txt:51-54`.

It carries host-specific detail that was never portable: a macOS serial device
path at `:167`, two OpenOCD configs under `/tmp` at `:174`, and fixed RAM
addresses such as `0x20012154` at `:175`. It is written in the second person as
a task brief for an automated coding session, beginning at `:3`. Nothing links
to it; `grep PERFORMANCE README.md` returns nothing.

**Recommendation.** Delete it, having first moved the parts that are still true
into contributor documentation. Three are recorded nowhere else: the BOOTSEL
flashing protocol and the Cortex-M33 lockup it avoids (`:23-28`), diagnosing over
SWD rather than by serial polling (`:29-31`), and the constraint that Core 1 must
stay resident in RAM and must never block (`:37-39`). These are now in
`CLAUDE.md`.

## Finding 3: 32 of the 34 files in `src/Pico/` cannot compile

Severity: high. About 3,600 lines that look live and are not.

`CMakeLists.txt:400-414` is the complete application source list. From
`src/Pico/` it names two files: `ui_draw.c` at `:412` and `ui_font.c` at `:413`.
`fpu_enable.h` is also live, included at `src/frank/frank_platform.c:29`.
Everything else is never built.

They cannot be built. They include beebjit headers that no longer exist anywhere
in the tree: `bbc.h`, `bbc_options.h`, `cpu_driver.h`, `interp.h`, `inturbo.h`,
`jit.h`, `render.h`, `state.h`, `state_6502.h`, `teletext.h`, `util.h`,
`wd_fdc.h`, `debug.h`, seven `disc_*.h` headers and ten `os_*.h` headers.

The largest are `pico_render.c` (752 lines), `main.c` (403),
`micro_serial_console.c` (373), `platform.c` (327) and `micro_boot.c` (323).

Four shadow the live implementation by name, so a contributor searching for a
feature finds two plausible candidates:

| Dead | Live |
|---|---|
| `src/Pico/micro_ui.c` | `src/frank/frank_ui.c` |
| `src/Pico/micro_settings.c` | `src/frank/frank_settings.c` |
| `src/Pico/micro_loader.c` | `src/frank/frank_loader.c` |
| `src/Pico/micro_serial_console.c` | `src/frank/frank_console.c` |

Two are actively misleading rather than merely dead. `micro_settings.c` uses the
**same file path** `/micro/micro.ini` as the live implementation
(`src/Pico/micro_settings.h:15` against `src/frank/frank_settings.c:364`) but a
different schema: string values such as `model=master128` where the live code
writes `model=1`. And `micro_serial_console.c:9-18` defines the line-based
protocol (`PING`, `DISK A INSERT`, `STATUS`, `SPEED`) that both
`tools/micro_console.py` and `PERFORMANCE.md` document as if it were live. The
live console accepts single bytes only (finding 10).

**Recommendation.** Delete the 32 files and the five dead `micro_*.h` headers.
Move `ui_draw.c`, `ui_draw.h`, `ui_font.c` and `fpu_enable.h` into `src/frank/`,
then remove the directory and its include path at `CMakeLists.txt:424`.

## Finding 4: the no-SD-card error screen reboots instead of halting

Severity: high. It defeats the purpose of the screen, and it was found
independently by two of the three sweeps.

`crash_handler_install()` starts the watchdog at `drivers/crash_handler.c:124`
(`watchdog_enable(60000, false)`) and is called from
`src/frank/frank_platform.c:333`. `boot_halt_if_required()` at `:301` then calls
`boot_error_screen()`, whose animation loop at `src/frank/frank_platform.c:293-298`
is:

```c
while (true) {
    for (int i = 0; i < 64; i++)
        graphics_set_palette((uint8_t)(16 + i), plasma_pal[(uint8_t)(i + phase) & 63]);
    phase++;
    sleep_ms(33);
}
```

It never calls `crash_handler_feed()` or `watchdog_update()`. The board therefore
reboots on the watchdog and redisplays the screen, indefinitely, rather than
halting on it as the comment at `:194-197` intends.

A second issue compounds it, and the SDK source settles it exactly.
`watchdog_enable(60000, ...)` asks for 60 seconds. That is not representable and
**it is silently clamped**, in
`pico-sdk/src/rp2_common/hardware_watchdog/watchdog.c:68-70`:

```c
load_value = delay_ms * (1000 * WATCHDOG_XFACTOR);
if (load_value > WATCHDOG_LOAD_BITS)
    load_value = WATCHDOG_LOAD_BITS;
```

`WATCHDOG_LOAD_BITS` is `0x00ffffff`
(`pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/watchdog.h:78`) and
`WATCHDOG_XFACTOR` is 1 on RP2350, the doubling being an RP2040 erratum
(`watchdog.c:31-36`). The ceiling is therefore 16,777 ms, about **16.8 seconds**.

The line above the clamp is `valid_params_if(HARDWARE_WATCHDOG, ...)`, a
parameter assertion that is disabled in a default build, so nothing warns about
the out-of-range request.

The real watchdog period is thus roughly 16.8 seconds, not the 60 the code
comment at `drivers/crash_handler.c:124` and `PERFORMANCE.md:119-122` both
assume. Combined with the missing feed, the no-SD error screen reboots about
every 17 seconds.

**Recommendation.** Call `crash_handler_feed()` inside the loop, and correct the
watchdog period to a representable value.

## Finding 5: the ACIA timer callback receives the wrong object

Severity: high as a correctness defect, narrow in blast radius. This is a
vendored upstream bug that frank-micro inherits unmodified, and the fix is one
line.

`src/bem/acia.c:27-30`:

```c
bool __time_critical_func(invoke_acia)(struct hw_event *event) {
    acia_poll((ACIA *)event);
    return false;
}
```

The `struct hw_event *` is cast to `ACIA *`. The correct pointer is stored in
`event->user_data`, set at `src/bem/acia.c:66`.

The layouts do not correspond. `struct hw_event` begins with
`struct list_element e` (`src/bem/thumb_cpu/src/hw_event_queue.h:14-20`).
`struct acia` has `control_reg` at offset 0 and `status_reg` at offset 1
(`src/bem/acia.h:7-16`). So `acia_poll` at `src/bem/acia.c:123-129` reads, and
conditionally writes, byte 1 of the event's list pointers.

The path is live. `NO_USE_ACIA` is not defined anywhere in `CMakeLists.txt`;
`acia.c` and `sysacia.c` are both compiled at `CMakeLists.txt:345` and `:375`,
and `USE_HW_EVENT` is defined at `CMakeLists.txt:497`. `acia_write` clears
`TXD_REG_EMP` on any write to the ACIA data register, and
`updated_TXD_REG_EMP()` at `src/bem/acia.c:36-43` then schedules the event 128
cycles out. Any BBC program writing to the ACIA transmit register arms it.

I traced the consequence rather than assuming the worst. `advance_hardware()` at
`src/bem/thumb_cpu/src/cpu_mem.c:243-244` calls `list_remove_head()` **before**
`firing->invoke(firing)`, and `invoke_acia` returns false so the node is not
requeued. The stray byte therefore lands in a node already removed from the
queue, so this is not live-list corruption.

What does go wrong is the intended effect. The real `sysacia` is never touched by
this timer, so its `TXD_REG_EMP` flag is never set by the scheduled event. A
program that writes a byte to the ACIA and then polls the status register for
transmit-ready will not see it become ready through this path. That affects
RS423 and cassette output, which is uncommon in games and would explain why the
bug has gone unnoticed.

**Recommendation.** Change line 28 to `acia_poll((ACIA *)event->user_data);`.
Report it upstream to B-em as well, since the defect is not local.

## Finding 6: copyrighted ROM and disc images conflict with redistributing under GPLv3

Severity: high as a licensing exposure. This is structural, not cosmetic, and it
is the one finding with consequences outside the repository.

Tracked binaries that are not freely licensable:

| Location | Contents |
|---|---|
| `sdcard/micro.zip` | 34 Acorn ROM images, about 475 KB uncompressed: MOS 3.20 and 3.50 sets, ADFS, View, Edit, Terminal, Master Compact ROMs, and several DFS variants |
| `src/bem/roms_bin/` | 39 further ROM images across `os/`, `general/` and `tube/`, about 1.3 MB |
| `src/bem/NS32016/pandora/` | 3 Pandora ROM images |
| `src/bem/pico/roms/embedded_roms.c` | 640 KB of source holding four of the same ROMs as C arrays, with no Acorn copyright statement in its header |
| `src/bem/pico/discs/` | 6 BBC disc images, about 1.3 MB, demoscene productions with no stated licence, unused by this build |

`README.md` carries a disclaimer that the ROMs are the copyright of their owners
and are included for emulation and preservation. A disclaimer does not create a
licence. GPLv3 section 5 requires the whole combined work to be licensable under
those terms, and these binaries cannot be. The port code is fine; the bundled
binaries are what prevent the repository as a whole from being redistributed
under the licence it declares.

Only four ROMs are actually used: `embedded_roms.c:9031-9035` embeds `os12`,
`basic2`, `dfs226` and `mos320`, 180,224 bytes total. The other 35 images in
`src/bem/roms_bin/` are referenced by nothing in the build.

**Recommendation.** This is the user's call and it is a policy decision, not a
technical one. The usual remedies are to remove the ROM and disc binaries from
the tree and fetch them at setup time, or to ship freely licensable equivalents.
At minimum, removing the 35 unused ROMs and the 6 unused disc images cuts 2.6 MB
and most of the exposure with no functional change.

---

## Finding 7: 27 files carry another project's copyright notice

Severity: medium, and the fix is mechanical.

27 files state that they belong to `frank-cpc`, an Amstrad CPC emulator, and give
`https://github.com/rh1tech/frank-cpc` as the project URL. For comparison, 62
files carry the correct `frank-micro` header. All 27 are under `drivers/`:

```
drivers/HDMI.c                     drivers/psram_dlmalloc.h
drivers/HDMI.h                     drivers/psram_init.c
drivers/HDMI_audio.c               drivers/psram_init.h
drivers/HDMI_vga.c                 drivers/pwm_audio/pwm_audio.c
drivers/HDMI_vga_hstx.c            drivers/pwm_audio/pwm_audio.h
drivers/audio.c                    drivers/sdcard/CMakeLists.txt
drivers/audio.h                    drivers/test_pins.c
drivers/crash_handler.c            drivers/uart_logging.c
drivers/crash_handler.h            drivers/uart_logging.h
drivers/fatfs/CMakeLists.txt       drivers/usbhid/CMakeLists.txt
drivers/ps2/CMakeLists.txt         drivers/usbhid/usbhid_wrapper.c
drivers/ps2/ps2kbd_wrapper.c       drivers/usbhid_wrapper.h
drivers/ps2/ps2kbd_wrapper.h       drivers/psram_allocator.c
drivers/psram_allocator.h
```

These headers came in with commit `c567103`, which introduced per-file licence
headers. The template was taken from the sibling `frank-cpc` project and the name
and URL were not corrected.

Related stale comments point at yet other projects.
`drivers/ps2/CMakeLists.txt` describes its wrapper as adapting the driver to the
keyboard-event API that the MSX platform consumes, and notes that `ps2.c` tracks
`murmsnes`. `src/Pico/ui_draw.h:11-12` and `:30-31` describe an MSX loader
overlay and avoiding collisions with fMSX's palette writes, and
`src/Pico/ui_draw.c:12` credits murmapple's `disk_ui.c`. That is the live UI
drawing code for a BBC Micro emulator.

`src/board_config.h:88-90` also keeps `CPC_FB_WIDTH`, `CPC_FB_HEIGHT` and
`CPC_SCREEN_LINES` aliases from the same lineage.

**Recommendation.** Rewrite the 27 headers. Keep any upstream attribution the
files also carry, since several are third-party drivers with their own authors;
only the project line and URL are wrong. Note that `drivers/HDMI_vga_hstx.c` is
itself dead (finding 11) and can be deleted rather than corrected.

## Finding 8: two vendored components have no licence and no attribution

Severity: medium. Unlike finding 7, this is missing attribution rather than wrong
attribution, which is the more serious of the two.

**`drivers/tv/` has no licence header on any file.** Nine files, over 2,000 lines,
being `tv-software.c` (1,279 lines), `tv-software.h`, `graphics.c`, `graphics.h`,
`font6x8.h`, `font8x8.h`, `font8x16.h`, `tv_mem.h` and `tv_rename.h`. Commit
`64c04a0` and the comment in `tv_rename.h` identify it as a port of the
"murmnes" software composite TV engine, and `tv-software.c:1` still carries the
original Russian source comment. It appears in neither the README attribution
table nor the Acknowledgments.

**`drivers/ps2/` is absent from the attribution table.** `ps2.c`, `ps2.h` and
`ps2.pio` are tagged `GPL-2.0-or-later`, which makes them the only GPL-2 code
outside `src/bem/`. mrmltr is thanked in the README's Acknowledgments but the
component is not listed with its licence.

Smaller gaps in the same table:

- **`src/bem/` has no vendored `COPYING` or `LICENSE` file.** Upstream B-em ships
  the GPL v2 text; it was not copied in. Only 17 of about 250 files under
  `src/bem/` carry any licence notice, so for the rest that missing file *was*
  the licence statement. `src/bem/resid-fp/COPYING` is present, so the omission is
  specific to B-em itself.
- **The pico_fatfs row says BSD-2-Clause; the files say BSD-3-Clause**
  (`drivers/sdcard/pio_spi.c`, `pio_spi.h`, `spi.pio`).
- **Missing rows** for `src/frank/vendor/pico/util/buffer.h` and
  `src/boards/waveshare_rp2350_pizero.h`, both BSD-3-Clause, and for the
  `frank-hdmi-sound` submodule, which is a required build dependency and ships
  its own GPLv3 licence.
- **Missing headers** on `drivers/sdcard/sdcard.c` and `sdcard.h` (no project and
  no upstream notice), and no SPDX tag on `drivers/fatfs/*` or
  `drivers/dlmalloc.c`, though both carry their upstream licence text in the
  banner comment.
- **`drivers/HDMI*.c` and `drivers/audio.c` are tagged GPL-3.0-or-later with no
  upstream attribution.** These are murmulator-lineage PIO drivers, and
  `drivers/audio_i2s.pio` beside them is BSD-3-Clause Raspberry Pi code.
  Relicensing BSD work into GPL-3 is permitted, but the original copyright notice
  must be retained. Worth confirming provenance.

## Finding 9: GPL version compatibility is sound, and the README understates it

Severity: low, but recorded because the opposite conclusion is easy to reach and
would be alarming.

A GPLv3 project vendoring GPL-2.0 code would be a genuine conflict. That is not
the situation. Every vendored component that states its terms states them as
version 2 **or later**: `src/bem/scsi.c:8-9`, `src/bem/fdi2raw.c:18-19`,
`src/bem/music5000.c:11-12`, and all 26 files in `src/bem/resid-fp/`. The "or
later" clause is what allows the combination to be distributed under GPLv3.

The README's attribution table labels these `GPL-2.0`. Read literally, that
label describes an incompatible combination.

**Recommendation.** Change the label to `GPL-2.0-or-later`. Note that this
concerns source licence compatibility only; the redistribution problem in
finding 6 is separate and unaffected.

## Finding 10: `tools/micro_console.py` belongs to a different project and speaks a dead protocol

Severity: medium, because it is the only tool in the repository and it cannot
work.

The file is frank-cpc's console script, committed unchanged. Its docstring says
"frank-cpc serial console", the class is `CPCConsole`, the port finder is
`find_cpc_port()` and raises "No CPC USB serial port found", error strings read
"CPC error:", example paths are `/cpc/disk/game.dsk`, and it exposes
`cart_insert` and `cart_eject` methods for an Amstrad cartridge slot that a BBC
Micro does not have.

It is dead twice over. Its wire protocol (`PING`, `DISK A INSERT`, `TYPE`, `KEY`,
`CAT`, `CD`) matches `src/Pico/micro_serial_console.c:9-18`, which is not
compiled (finding 3). The live console, `src/frank/frank_console.c`, accepts
single bytes only and would read `PING\r\n` as the keystrokes P, I, N, G, Return.

It is referenced by nothing: not `README.md`, not `CMakeLists.txt`, not
`build.sh`, `release.sh` or `flash.sh`. It carries no licence header, unlike
every other first-party file.

**Recommendation.** Delete it, or rewrite it against the live single-byte
protocol.

## Finding 11: dead code inside the live tree

Severity: medium, because unlike `src/Pico/` these files sit among working code.

- **`frank_disc_preload()` is never called.** Declared
  `src/frank/frank_disc.h:19`, defined `src/frank/frank_disc.c:209-221`. It still
  hardcodes `/micro/disk/PrinceOfPersia.ssd` at `src/frank/frank_disc.c:30`.
  Residue of commit `04ce6cb`, which stopped auto-loading that demo disc.
  `CMakeLists.txt:488` carries the matching stale comment.
- **`drivers/HDMI_vga_hstx.c` is never compiled.** It also defines a second
  `vga_diag()` at `:226` and a conflicting `SELECT_VGA` at `:74`, so it could not
  be added without a duplicate-symbol error.
- **`drivers/sdcard/pio_spi.c` and `spi.pio` are compiled but unreachable.** Every
  call site is inside `#ifdef SDCARD_PIO`, and `SDCARD_PIO` is defined nowhere.
- **`drivers/tv/CMakeLists.txt` is never included.** No
  `add_subdirectory(drivers/tv)` exists. It declares a second `tv_driver` target
  that lacks the include shims the working one has.
- **`src/bem/pico/CMakeLists.txt` and `src/bem/thumb_cpu/CMakeLists.txt` are
  never included.** Consequently `src/bem/pico/memmap_b-em.ld` and its
  `PICO_STACK_SIZE` and `PICO_CORE1_STACK_SIZE` settings are **not applied**; the
  build uses the SDK default linker script and default 2 KB stacks.
- **The `config_values[]` table at `src/bem/pico/stub_allegro5/al_stub.cpp:314-402`
  is dead**, replaced by the per-model tables at `:286-311`. It compiles silently
  because `-Wno-unused-variable` is set (`CMakeLists.txt:527`) and carries 60
  lines of commented-out demo disc paths.
- **`board_m2.h` advertises a video mode this build does not have.** Its header
  comment lists HSTX HDMI as the default and `:21` defines `HAS_HSTX 1`. No HSTX
  driver is in any source list. `HAS_HSTX`, `HAS_TV`, `HAS_I2S`, `HAS_PWM`,
  `HAS_HDMI_AUDIO`, `HAS_PS2_MOUSE` and `TAPE_IN_PIN` are all defined in the board
  headers and read by nothing, so the README's tape-input pin has no code behind
  it.
- **`FRANK_PERF_TIMING_HACKS=1`** (`CMakeLists.txt:454`), **`HDMI_PIN`**
  (`:83`, `:92`, `:101`) and **`BOARD_DEF`** (`:167`, `:241`, `:281`, `:450`) are
  defined and tested nowhere.
- **`SINGLE_CORE`** (`CMakeLists.txt:471`) has no effect: its only use,
  `src/bem/pico/video/display.c:2425`, is inside a `DISPLAY_WIRE` block that
  `X_GUI` disables at `src/bem/pico/video/display.h:17-20`.

## Finding 12: no warnings are enabled anywhere

Severity: medium.

There is **no `-Wall` and no `-Wextra` anywhere** in the build. I searched
`CMakeLists.txt` and every sub-`CMakeLists.txt`. What exists instead is thirteen
`-Wno-*` suppressions at `CMakeLists.txt:522-537`, applied to the firmware target
only. Four of them cover categories that routinely indicate real defects in
embedded C:

- `-Wno-incompatible-pointer-types` at `:533`
- `-Wno-pointer-sign` at `:532`
- `-Wno-format` at `:535`, which covers mismatched `printf` arguments
- `-Wno-parentheses` at `:534`

`-Werror` is not set. `tv_driver` adds `-Wno-error` and `-Wno-all` at `:211-213`.

For a 3,400-line application layer full of fixed-size buffers and manual pointer
arithmetic, turning warnings on is the cheapest robustness improvement available.
Several findings below are exactly what `-Wall -Wextra -Wformat-truncation`
reports: the silent path truncations in finding 14, the unused functions in
finding 11, and the narrow table type in `src/frank/frank_keyboard.c:38`.

On assertions, I was wrong in an earlier draft. The Pico SDK supplies
`CMAKE_BUILD_TYPE=Release`, so `NDEBUG` **is** defined and the `assert()` calls
in the emulator core are compiled out (finding 1). Worth knowing which ones would
become live if anyone sets a `Debug` build, because several sit in the hot
rasteriser and CPU paths:
`src/bem/pico/video/display.c:589`, `:619`, `:1680`, `:2245`;
`src/bem/thumb_cpu/src/cpu_mem.c:151`, `:238`;
`src/bem/pico/video/video.c:382`, `:485`; `src/bem/pico/audio/audio.c:577`,
`:592`. On a board with no console attached, a firing assertion is
indistinguishable from a hang.

There are also unreachable-today assertion traps one `#define` away from being
live. `src/bem/pico/video/gui_stub.c:29` and `:33` call `assert(false)`, `:41`
calls `panic_unsupported()`. Every register accessor in
`src/bem/thumb_cpu/src/adapter.c` asserts when `THUMB_CPU_USE_ASM` is set, which
it always is, at `:44`, `:49`, `:54`, `:59`, `:64`, `:69`, `:74`, `:79`, `:84`,
`:90`, `:104`, `:151`. Their only callers are in files excluded from the build,
so re-enabling the debugger or VDFS would make them live.

**Recommendation.** Enable `-Wall -Wextra` on `src/frank/` first, where the
project's own code lives, and keep `src/bem/` permissive. Set
`CMAKE_BUILD_TYPE` deliberately so the assertion question has an answer.

## Finding 13: the linker options line is a no-op

Severity: low on its own, but it gates two latent link failures.

`CMakeLists.txt:579`:

```cmake
target_link_options(frank-micro PRIVATE -Xlinker --print-memory-usage --data-sections --function-sections)
```

`-Xlinker` passes only the **single** following argument to the linker. Only
`--print-memory-usage` reaches `ld`. `--data-sections` and `--function-sections`
go to the GCC driver, which aliases them to `-fdata-sections` and
`-ffunction-sections`; those are compile-time options and do nothing at link
time. `--gc-sections` is not requested at all, so whether unused functions are
dropped depends entirely on the SDK's own link flags.

In practice the intent is achieved anyway: the SDK supplies
`-ffunction-sections -fdata-sections` on the compile line and `-Wl,--gc-sections`
on the link line, both visible in a configured build
(`build/CMakeFiles/frank-micro.dir/link.txt`). So the line is redundant rather
than harmful, and the two objects below are protected today. They would break if
anyone changed the SDK's link flags:

- `drivers/HDMI_audio.c:198-234` defines four `audio_ring_*` functions that
  reference `i2s_ring_push`, `i2s_ring_push_stereo`, `i2s_ring_free`,
  `g_audio_prod` and `g_audio_cons`. Those symbols exist only in
  `src/Pico/platform.c:264-282`, which is not compiled. Nothing calls the four
  functions.
- `drivers/tv/graphics.c:42-67` `draw_window` calls `draw_text` four times, but
  `draw_text` is commented out at `:31-41`. Nothing calls `draw_window`.

Both would become link failures if `--gc-sections` went away, or if someone
added a call to them.

## Finding 14: robustness defects in the application layer

Severity: medium. Grouped because they share a cause, which is unchecked returns
and silent truncation.

**Settings are saved without atomicity or error checking.**
`frank_settings_save()` at `src/frank/frank_settings.c:433-464` opens
`/micro/micro.ini` with `FA_CREATE_ALWAYS`, truncating the existing file before
writing a byte of the replacement. There is no temp-file-and-rename, so a power
loss or card removal during the save loses all settings. None of the four
`f_write` calls at `:444`, `:449`, `:454`, `:459` has its return value or byte
count checked, and neither does `f_close` at `:462`, so the function returns
`true` on a full card. Worse, `frank_settings_step()` calls it unconditionally at
`:281`, so holding Right on Volume performs eleven full truncate-and-rewrite
cycles.

**A Model change is written to the card before the user confirms it.** Pressing
Right on Model sets the value at `src/frank/frank_settings.c:233` and writes
`model=N` to the card at `:281`. If the user then presses ESC
(`src/frank/frank_ui.c:132-134`), the running machine keeps the old model but the
**next power-on silently boots the other one**. `s_pending_restart` is also never
cleared on cancel; it is reset only inside `frank_settings_do_restart()` at
`:344-345` and `:355-356`, so a later "Reset Emulator" reboots the board instead
of soft-resetting.

**Gamepad keys stick when the preset changes mid-press.**
`src/frank/frank_keyboard.c:319` resolves the key code at release time, not press
time. Hold a direction under the `Z X : /` preset, switch to Arrows in the menu,
release: the release is sent for the arrow key and Z stays down in the BBC matrix
permanently. Switching to Off is worse, because
`frank_gamepad_key_for()` returns 0 (`src/frank/frank_settings.c:324-326`) and the
`if (code)` guard at `src/frank/frank_keyboard.c:320` skips the release entirely.

**The overlay drawing primitives do not clip.** `put_pixel` at
`src/Pico/ui_draw.c:32-34` indexes the framebuffer with no bounds test at all.
`ui_fill_rect` at `:36-42` rejects a non-positive `w` or `h`, which is its only
guard, and does not check `x`, `y`, or the right and bottom edges. One path is
already reachable:
`frank_ui_render` computes `tx = (stride - tw) / 2` with
`tw = strlen(s_toast) * 6 + 12` at `src/frank/frank_ui.c:564-565`, and `s_toast`
is 64 bytes, so a 63-character toast yields `tx = -35`. Current callers pass
short literals, so this is latent.

**Screenshots are written synchronously from the video callback.**
`src/frank/frank_gui.c:170-174` calls `frank_screenshot_save()` inside
`x_gui_end_scanline()`. That is an 84 KB SD write issued as about 258 separate
`f_write` calls (`src/frank/frank_screenshot.c:108-113`), on core 0, inside the
per-scanline hook, with no watchdog feed. Core 0 stalls for the whole write, so
the audio producer starves. Every `f_write` return is discarded, so the function
reports success on a full card.

**SD sector reads happen inline in the CPU emulation path.**
`src/frank/frank_disc.c:47-63` does `f_lseek` then `f_read` inside
`fatfs_acquire()`, reached from `sdf_readsector()` on core 0 inside the frame.
The error path is the concerning part: on any read failure it zero-fills the
sector (`:56-57`) rather than reporting a sector error, so a card fault looks to
the BBC like a valid all-zeros sector and produces a corrupt load rather than a
disc fault.

**Silent truncation in three places.** The disc browser stops at 200 entries and
skips any filename of 64 characters or more, with no message
(`src/frank/frank_loader.c:66`, `:74-75`), while FatFs is configured for
`FF_MAX_LFN 255` (`drivers/fatfs/ffconf.h:117`). Disc paths are truncated to 127
characters on load (`src/frank/frank_settings.c:413-414`). `f_readdir` returning
`FR_NOT_ENOUGH_CORE`, which is possible because `FF_USE_LFN 3` heap-allocates the
work buffer, is treated as end-of-directory at `src/frank/frank_loader.c:68`.

**The screenshot counter wraps by overwriting.**
`src/frank/frank_screenshot.c:53-54` resets to 1 past 9999, overwriting
`BBC_0001.BMP`. The directory scan at `:35-51` also stops at the first
non-`FR_OK` `f_readdir`, so a partial scan yields a low counter and the next save
overwrites an existing file, since `:70` opens with `FA_CREATE_ALWAYS`.

**Serial console shift chords never hold shift.**
`src/frank/frank_console.c:89-92` and `:101-103` do
`key_down(AK_LSHIFT); start_tap(code); key_up(AK_LSHIFT);` in one call.
`start_tap` holds the key for 4 frames but shift is released immediately, so the
BBC keyboard scan never sees them together. The documented Prince of Persia
"careful move" chords do not work.

## Finding 15: driver-level concurrency and resource issues

Severity: medium. **Needs confirmation** on hardware for the first two.

**The PS/2 ring buffers can drop bytes.** `kbd_pio_drain()` at
`drivers/ps2/ps2.c:122-139` is called both from the `PIO1_IRQ_0` handler at
`:141-143` and from `ps2_kbd_get_byte()` at `:1097`, with no interrupt masking.
The sequence at `:129-132` is test, store, then advance head. If the interrupt
fires between the store and the advance, both paths write the same slot and the
head advances once. `mouse_pio_drain()` at `:169-193` has the same structure, and
its comment at `:170-173` says the poll path is primary and the interrupt a
backup, which is the arrangement that makes the race most likely.

**PIO1 is exactly full, and one state machine is wasted.** PIO1 carries the PS/2
keyboard and mouse state machines (`drivers/ps2/ps2.c:810`, `:817`), the NES pad
(`drivers/nespad/nespad.c:106`) and I2S (`drivers/audio.c:84`, `:109`), which is
four of four. On M1 and Z0 there is no PS/2 mouse, so
`src/board_config.h:72-75` aliases `PS2_MOUSE_CLK` to `PS2_PIN_CLK` and
`ps2_init` then initialises the mouse state machine on the **same clock pin** as
the keyboard, consuming a state machine and filling the mouse ring with
duplicated keyboard bytes. On M2 the mouse machine is claimed even though
`NO_USE_MOUSE` is set at `CMakeLists.txt:477`. Any further PIO1 consumer will
fail, and `i2s_init` calls `pio_claim_unused_sm(pio, true)`, which panics.
Instruction memory is also near the limit at about 27 of 32 words.

**`ps2_init` failure is silent.** It returns `bool`
(`drivers/ps2/ps2.c:791`) and can fail at `:783`, `:812` or `:819`. The return is
discarded at `drivers/ps2/ps2kbd_wrapper.c:274`. Given that PIO1 is exactly full,
a failure here would leave the keyboard silently dead.

**I2S unclaims DMA channels it never claimed.** `drivers/audio.c:145-146` calls
`dma_channel_unclaim(10)` and `(11)` before claiming them. The SDK's
`hw_claim_clear()` asserts that the bit was already set. In a default build this
is harmless, because `NDEBUG` is defined and the assertion is compiled out
(finding 1). It would fire on a `Debug` build. `drivers/pwm_audio/pwm_audio.c:167-168`
does not do this, so the two audio drivers disagree about the pattern.

**Blocking audio writes can starve the watchdog.** `i2s_dma_write` spins at
`drivers/audio.c:204-227` until a DMA buffer frees, up to about 28 ms per call,
and `i2s_quiet()` makes two such calls (`src/frank/frank_audio.c:109-110`). That
is up to about 56 ms inside a single keypress handler, roughly three dropped
frames. The watchdog is fed only once per frame from `frank_perf_tick()`
(`src/frank/frank_platform.c:107`).

**The abandoned audio backend is never stopped.** Neither `i2s_quiet()` nor
`pwm_quiet()` (`src/frank/frank_audio.c:103-121`) disables the PIO state machine,
the PWM slices or the DMA chain; they only push silence. After a switch the idle
backend keeps chaining DMA and taking its completion interrupt about every 28 ms
indefinitely.

**The HDMI resampler keeps stale state across switches.** `mu` and `prev` are
function-scope statics at `src/frank/frank_audio.c:236-237`, never reset by
`frank_audio_set_driver()`, so switching away from HDMI and back resumes from a
stale sample and clicks.

**The I2S DMA handler runs from flash.** `drivers/audio.c:280` is not
`__not_in_flash_func`, unlike its PWM counterpart at
`drivers/pwm_audio/pwm_audio.c:84`. Given the XIP reasoning applied elsewhere in
the same tree, this looks like an oversight.

## Finding 16: README claims the code does not support

Severity: medium. Each is small; together they make the README unreliable.

1. **"Select + Start on a gamepad: open the settings menu"** appears twice in the
   README. No such handler exists. `frank_gamepad_poll` maps
   `FRANK_PAD_START` to Return and `FRANK_PAD_SELECT` to Escape and nothing else
   (`src/frank/frank_settings.c:306-307`, `:320-321`). There is no gamepad path
   into the UI at all, and gamepad polling is not suppressed while the overlay is
   open (`src/frank/frank_keyboard.c:345`), so gamepad presses type into the BBC
   underneath the menu.
2. **The engine bullet claims an Intel 8271 floppy controller.**
   `CMakeLists.txt:478` defines `NO_USE_I8271`, and `src/bem/i8271.c:13` wraps the
   whole file in that guard. Only the WD1770 is live, which agrees with the
   README's own description of Model B as using the Acorn 1770 DFS.
   `src/bem/thumb_cpu/src/cpu_mem.c:419-446` also silently routes an `FDC_I8271`
   selection to the WD1770 rather than failing.
3. **"Use UART for console output"** when USB HID is enabled. UART stdio is
   disabled unconditionally at `CMakeLists.txt:562`, and USB CDC is disabled when
   USB HID is on at `:567-571`. `release.sh:117` sets USB HID for every variant,
   so **released firmware has no console output at all**.
4. **The file format list omits `.ADM`**, which `frank_is_disk_file` accepts
   (`src/frank/frank_loader.c:41`) and `geo_from_file` treats as ADFS
   (`src/frank/frank_disc.c:107`).
5. **The platform table lists HDMI audio for all three boards.** HDMI-embedded
   audio exists only in the `HDMI_PIO_AUDIO` build; the other two remove it from
   the menu entirely (`src/frank/frank_settings.c:68-74`).
6. **"Model changes reboot the board; the other settings apply live"** is wrong
   for Startup Mode, which returns restart kind 1
   (`src/frank/frank_settings.c:162`) and whose own header comment says it needs a
   reset (`src/frank/frank_settings.h:31`).
7. **The release matrix is eight variants, not nine.** Composite TV is rejected on
   z0 at `CMakeLists.txt:44-46`, matching `release.sh:34-43`.
8. **The Z0 HDMI pin rows are wrong.** The README lists Z0 clock-first like M1
   and M2. `drivers/HDMI.h:34-42` puts the data lanes first on Z0
   (`beginHDMI_PIN_data = HDMI_BASE_PIN`, `beginHDMI_PIN_clk = HDMI_BASE_PIN + 6`),
   giving data on GPIO 32 to 37 and clock on 38 to 39. The same block sets
   `HDMI_PIN_RGB_notBGR` and `HDMI_PIN_invert_diffpairs` to 0 for Z0, so the
   channel order is BGR and the minus and plus labels are inverted relative to
   the other two boards. Three attributes of those rows are wrong.
9. **The shared-pin note names only M2.** I2S and PWM also collide on M1
   (GPIO 26 and 27, `src/board_m1.h:52-57`) and on Z0 (GPIO 10 and 11,
   `src/board_z0.h:50-55`).

### Behaviour the README omits

- **All disc images are read-only.** `CMakeLists.txt:486` defines
  `NO_USE_DISC_WRITE`, and `src/bem/sdf-acc.c:419-421` answers every write-sector
  command with `fdc_writeprotect()`. `src/frank/frank_disc.c:141` also opens with
  `FA_READ` only. `*SAVE`, `*SPOOL`, save slots and high-score tables all fail,
  and nothing in the UI says so. This is the most significant omission.
- **MODE 0 and MODE 3 lose half their horizontal resolution.**
  `src/frank/frank_gui.c:134-135` point-samples 640 engine pixels into a 320-wide
  framebuffer. That is lossless for the 40-column modes, where the engine doubles
  each pixel, and lossy for the genuinely 80-column MODE 0 and MODE 3, where every
  second pixel column is discarded and 80-column text aliases badly. The comment
  at `:122-126` defends the approach by counting colours, saying the row holds at
  most 320 distinct ones. That is true and beside the point: what is lost is pixel
  detail, not colour.
- **Only 8 physical colours.** `rgb555_to_bbc()` at `src/frank/frank_gui.c:59-64`
  thresholds each channel at 16 and returns an index 0 to 7. Correct for a stock
  BBC, but it means Video NuLA palettes are snapped to the nearest primary.
- **The default model is Master 128**, not BBC B
  (`src/frank/frank_settings.c:38`, `MODEL_MASTER` at `CMakeLists.txt:473`).
- **Auto-mount fallback filenames** `/micro/disk/drivea.ssd` and `driveb.ssd`
  (`src/frank/frank_loader.c:133`).
- **`disk_a` and `disk_b` are valid ini keys**, and a bare filename resolves
  against `/micro/disk/` (`src/frank/frank_settings.c:410-415`). No ini keys are
  documented at all.
- **Mounting into drive 0 triggers an automatic SHIFT-BREAK**
  (`src/frank/frank_ui.c:294-296`); drive 1 does not.
- **The browser can navigate above `/micro/disk`** to the card root
  (`src/frank/frank_loader.c:103-110`).
- **A single-byte USB-serial console exists** in non-USB-HID builds
  (`src/frank/frank_console.c`, gated at `src/frank/frank_platform.c:102-104`).
- **`FRANK_NO_AUTOBOOT` build option** (`CMakeLists.txt:489`), and
  **`FRANK_PERF_LOG`** telemetry (`src/frank/frank_platform.c:140-164`), which is
  defined nowhere and so off by default.
- **No tube coprocessors, no SID, no save states, no VDFS, no tape loaders.**
  `src/bem/tape.c` is compiled with tape enabled but both loaders disabled
  (`NO_USE_UEF`, `NO_USE_CSW` at `CMakeLists.txt:477`), leaving `loaders[]` at
  `src/bem/tape.c:21-30` holding only its sentinel. `tape_close()` at `:59-64`
  would call a null function pointer if `tape_loaded` were ever true, which it
  cannot be. The clean fix is to define `NO_USE_TAPE`.

## Finding 17: about 31,000 lines of vendored source are never compiled

Severity: low as a defect, medium as a source of confusion.

Four whole subdirectories are absent from the engine source list at
`CMakeLists.txt:341-395`:

| Directory | Lines | What it is | Why it is off |
|---|---|---|---|
| `src/bem/NS32016/` | 12,390 | 32016 second processor | `NO_USE_TUBE` |
| `src/bem/resid-fp/` | 7,625 | SID sound chip | `NO_USE_SID` |
| `src/bem/darm/` | 6,721 | ARM disassembler | `NO_USE_DEBUGGER`, `NO_USE_TUBE` |
| `src/bem/mc6809nc/` | 4,232 | 6809 second processor | tube disabled |

A further roughly 33,000 lines at the `src/bem/` top level are not compiled,
including `z80.c`, `x86.c`, `65816.c`, `arm.c`, `vdfs.c`, `debugger.c`,
`savestate.c`, `gui-allegro.c` and the root `video.c`. Under `src/bem/pico/`,
the entire `x_gui/` implementation is dead: frank-micro uses `x_gui.h` purely as
an interface contract and supplies its own hooks in `src/frank/frank_gui.c` and
`frank_audio.c`.

**The more actively misleading point concerns files that *are* compiled.**
`CMakeLists.txt:476-486` disables more than thirty features via `NO_USE_*` while
their `.c` files stay in the build and reduce to stubs. Sixteen files are in this
state, including `adc.c`, `i8271.c`, `ide.c`, `scsi.c`, `csw.c`, `uef.c`,
`fdi.c`, `pal.c`, `joystick.c`, `mouse.c`, `music5000.c` and `ddnoise.c`.
Judging a feature present because its file is in the build gives the wrong answer
sixteen times.

## Finding 18: no CI and no tests

Severity: medium as a process risk.

There is no `.github/` directory, no CI configuration of any kind, and no test
target. `find . -iname '*test*'` yields `drivers/test_pins.c`, which is a runtime
ribbon detector, plus two upstream B-em host utilities that are not built.

The project ships eight firmware binaries per release, each validated only by
hand on hardware.

`release.sh` makes failures hard to diagnose. It sends both CMake and `make`
output to `/dev/null` at `:113-119`, so a failed variant prints one line and
nothing else. It writes `version.txt` at `:91` before any build runs, and nothing
reverts it, so a wholly failed release still consumes a version number. It
deletes the build directory each iteration at `:109-110` and `:136`, so all eight
variants recompile from scratch, including regenerating the 6502 tables.
`build.sh:20` does the same, so neither script supports an incremental build and
the README does not mention `cmake --build build` as the alternative.

**Recommendation.** A GitHub Actions job that configures and compiles all eight
variants needs no hardware and would catch findings 3, 11 and 13. I ran exactly
that matrix and all eight pass today, so adding it locks in a currently-green
state rather than opening a backlog. The whole job is:

```bash
for p in m2 m1 z0; do for d in HDMI_PIO_AUDIO HDMI_PIO COMPOSITE; do
  [ "$p" = z0 ] && [ "$d" = COMPOSITE ] && continue
  cmake -S . -B "b-$p-$d" -DPLATFORM=$p -DHDMI_DRIVER=$d && make -C "b-$p-$d" -j || exit 1
done; done
```

## Finding 19: build prerequisites and repository hygiene

Severity: low to medium.

**Three build prerequisites are undocumented.** The README lists only the Pico
SDK and the ARM GCC toolchain. All three of the following were hit in order while
getting a first build to succeed.

1. **Ruby, and specifically Ruby's `erb`.** `CMakeLists.txt:319` calls
   `find_program(RUBY_EXECUTABLE ruby REQUIRED)` inside the `USE_THUMB_CPU`
   branch, which defaults to `ON` at `:312`. Ruby generates the 6502 dispatch
   tables from `src/bem/thumb_cpu/gen/gen_asm.rb`, and that script does
   `require 'erb'` at its line 7. Installing Ruby is not sufficient on every
   distribution: on Arch the base `ruby` package omits `erb`, and the build
   reaches 56 percent before dying with `cannot load such file -- erb`.
2. **A C library for the ARM toolchain.** On Arch, `arm-none-eabi-newlib` is an
   *optional* dependency of `arm-none-eabi-gcc`. Installing the compiler alone
   produces `cannot find -lg` and `cannot find -lc` at the very first link, the
   SDK's `bs2_default.elf`, which looks like an SDK problem and is not.
3. **The Pico SDK's own submodules.** This is the one worth documenting most,
   because it fails silently. Without `lib/tinyusb` initialised in the SDK
   directory, `pico_enable_stdio_usb(frank-micro 1)` at `CMakeLists.txt:570` is a
   no-op. CMake prints a single warning at configure time, the build **succeeds**,
   and the resulting ELF contains the stdio framework with no output driver
   registered at all. I confirmed this on the binary I built:
   `arm-none-eabi-nm frank-micro.elf | grep -E 'stdio_(usb|uart)'` returns
   nothing, and `LIB_PICO_STDIO_USB` never reaches the compile flags. A default
   `USB_HID=0` build that should have a USB CDC console has no console. Combined
   with UART stdio being disabled unconditionally at `:562`, the firmware is
   mute. See also finding 16.

**Recommendation.** List all three in the README, and add a CMake guard that
fails loudly when `frank-hdmi-sound/src/frank_hdmi.h` or the SDK's TinyUSB is
missing, rather than proceeding to a silently crippled binary.

**The submodule needs care.** `frank-hdmi-sound` was uninitialised at the start
of this investigation and has since been pulled; `git submodule status` now
reports it clean at `388d8798`. Three points remain. The default
`HDMI_PIO_AUDIO` build requires it (`CMakeLists.txt:142`), and the failure mode
is a missing-header error rather than a clear message, so a
`FATAL_ERROR` guard on `frank-hdmi-sound/src/frank_hdmi.h` would help. The
directory is `frank-hdmi-sound` while the repository is `frank-hdmi-audio`, which
the README has to explain. And it tracks a moving branch rather than a tag, so
`git submodule update --remote` can change the build under a released version.

**The commit-msg hook is inert.** `.githooks/commit-msg` rejects AI attribution
trailers, but `git config core.hooksPath` is unset and `.git/hooks/commit-msg`
does not exist, so nothing runs it. It becomes active only if someone runs
`git config core.hooksPath .githooks`, which nothing in the repository or the
README does.

**Stale and contradictory build configuration.**
`CMakeLists.txt:34` annotates `PSRAM_SPEED` as unused, but it is passed as
`PSRAM_MAX_FREQ_MHZ` at `:172`, `:245`, `:286` and `:457`. `JIT_STUBS` at `:397`
is an empty leftover of the beebjit era, still added to the executable at `:418`.
The VS Code block at `:1-14` hardcodes `${USERHOME}/.pico-sdk` and pins the SDK
and toolchain versions, overriding the user's environment if that file exists,
and its "DO NOT EDIT" banner discourages fixing it.

**Clock bring-up ordering.** `src/frank/frank_platform.c:308-313` gates the
voltage and flash-timing block on `CPU_CLOCK_MHZ > 252`, so at exactly the
default 252 MHz neither `vreg_set_voltage` nor `set_flash_timings` runs. Within
the block, `set_flash_timings` is called before `set_sys_clock_khz`, so the QMI
divider is programmed for the target clock while the core is still at boot speed.
That direction is safe but is the reverse of the usual order, and the 100 ms
settling sleep runs at the wrong clock.

**M1 PSRAM chip-select shares a pin with HDMI D0 minus.** On M1,
`HDMI_BASE_PIN 6` (`src/board_m1.h:29`) plus `drivers/HDMI.h:47` puts the TMDS
data pins on GPIO 8 to 13, while `PSRAM_CS_PIN_RP2350A` is 8
(`src/board_m1.h:60`). `src/frank/frank_platform.c:341` calls `psram_init`, which
unconditionally does `gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1)` at
`drivers/psram_init.c:85` before probing, and leaves that function on the pin if
the probe fails (`:149-151`). It survives only because `graphics_init()` runs
later and re-muxes the pin. M2 and Z0 do not have this overlap.

**Committed binaries and history.** Beyond the licensing exposure in finding 6:
`screenshots/*.jpg` total 687 KB, with `screen_2.jpg` alone at 371 KB for a
source frame that is a 320x256 BMP. `src/bem/pico/roms/embedded_roms.c` (640 KB)
and `src/bem/pico/audio/ddnoise_samples*.h` (1.25 MB) are generated files checked
in with no generator script in the tree. History also holds
`build-bem/build.ninja` (2.9 MB) and `src/micro/miniz.c` (316 KB) from the
removed beebjit backend. The pack is 5.58 MB, so none of this is urgent.

**`.gitignore` problems.** The `.vscode` rules at lines 33-38 re-include four
files, then line 55 adds a bare `.vscode/` that excludes the directory and makes
every negation above unreachable. Stale entries match nothing in the tree:
`*_edsk`, `disc_old.c`, `disc.c_edsk`, `capture/`, `scripts/out/`. `build-bem/`
is not ignored despite having been committed once.

**Author email inconsistency.** Every file header gives `xtreme@rh1.tech`; all 27
commits are authored as `xtreme@rh1.ru`.

**`flash.sh` and `PERFORMANCE.md` disagree.** `flash.sh:22` uses
`picotool load -f`; `PERFORMANCE.md:166` insists on `picotool load -x` after a
manual BOOTSEL.

---

## Addendum, 2026-08-21: findings from the Fruit Jam port review

Surfaced while validating `docs/FRUIT-JAM.md` against the tree at `c27ffbf`.
These are repository findings, not Fruit Jam findings; they are recorded here
so they survive independently of that document.

**The two video drivers disagree about PIO GPIO re-basing.**
`drivers/HDMI.c:534-541` selects `pio_set_gpio_base(..., 16)` for any
`HDMI_BASE_PIN` of 16 or above, and its comment states that base 32 "is not a
valid RP2350 PIO base and breaks Z0". `drivers/HDMI_vga.c:432-436` does the
opposite: it passes base 32 for `VGA_BASE_PIN >= 32`. The Z0 is the only board
with `VGA_BASE_PIN 32` (`src/board_z0.h:30`), and it is saved only because
`frank_platform.c:373-375` forces `SELECT_VGA = false` there, so the invalid
call is currently unreachable. Anyone enabling VGA on a high-pin board walks
straight into it.

**The I2S pin-mux maps every non-pio0 block to `GPIO_FUNC_PIO1`.**
`drivers/audio.c:99` computes
`func = (config->pio == pio0) ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1`, and
`src/frank/frank_audio.c:66-68` hardcodes `GPIO_FUNC_PIO1` outright. Both are
wrong for PIO2, which nothing uses today, so the bug is latent; it becomes
live the moment any PIO consumer moves to PIO2, which the Fruit Jam plan does.

**A stale comment contradicts finding 4.** The comment at
`src/frank/frank_platform.c:397-399` says a missing SD card "shows the boot
error screen and halts". It does not halt: the error loop at `:293-298` never
feeds the 60-second watchdog armed by `crash_handler_install()`
(`drivers/crash_handler.c:124`), so the board reboots, exactly as finding 4
describes. Fix the comment along with the behaviour.

**`usbhid_wrapper.c` carries a third project's name.** On top of the
frank-cpc header counted in finding 7, the body comment at
`drivers/usbhid/usbhid_wrapper.c:10` reads "USB HID wrapper for fMSX on
RP2350". One file, three project identities.

**`drivers/HDMI_vga_hstx.c` is internally inconsistent.** The dead file
(finding 11 territory) describes its CPC frame as 320x200 at line 16 and as
320x240 at lines 38 and 45. Noted only so nobody treats the file as a
reference when writing the real HSTX driver.

**`drivers/HDMI_vga_hstx.c` cannot compile, not merely is not compiled.** Stronger
than finding 11 records. It includes `disphstx.h` at line 30, from the
third-party DispHSTX library, and that library is **not vendored anywhere in the
repository**: a `find` for `disphstx` across the tree returns nothing, and no
`CMakeLists.txt`, `.cmake` or `.sh` file mentions either the file or the library.
Adding it to a source list would fail at the preprocessor, before the collision
with `HDMI_vga.c` that `CLAUDE.md` already notes.

**`src/board_m2.h` advertises an HSTX video and audio path that does not exist.**
`HAS_HSTX` is defined at `src/board_m2.h:21` and used **nowhere**: the only two
hits across every `.c`, `.h`, `.S`, `.txt` and `.sh` in the tree are its own
definition and a comment referring to it. There is also no HSTX reference
anywhere in the `frank-hdmi-sound` submodule at `388d879`. Nothing in
frank-micro touches the HSTX peripheral.

The header comment at `src/board_m2.h:12-13` nevertheless reads:

```
 * Supported video: HSTX HDMI (default) / PIO HDMI / PIO VGA / composite TV.
 * Supported audio: HSTX HDMI / I2S / PWM / Disabled.
```

Both lines are false. The default is `HDMI_PIO_AUDIO`, which is PIO TMDS through
the submodule, and its embedded audio comes from PIO-generated HDMI data islands
rather than from HSTX. This is another frank-cpc inheritance, alongside the file
headers counted in finding 7, and it is the more expensive kind: someone reading
`board_m2.h` first would reasonably conclude a working HSTX driver already
exists, which is exactly wrong and directly relevant to the phase 3 estimate in
`docs/FRUIT-JAM.md`. Fix the comment; delete `HAS_HSTX` or leave it with an
explicit note that no code consumes it.

For contrast, the *pin* claims in the same headers are correct and useful:
`src/board_m1.h:13` "No HSTX (HDMI_BASE_PIN != 12)" and `src/board_z0.h:22` "No
HSTX" are both accurate, since HSTX is hardwired to GPIO 12-19 while M1 puts
HDMI on GPIO 6-13 (`board_m1.h:29`) and Z0 on GPIO 32-39 (`board_z0.h:29`). Only
M2 has the connector on pins HSTX could reach. That, plus the RP2040 lineage of
these drivers and the shared HDMI/VGA driver, is why the PIO approach was taken;
it was not a choice against HSTX.

---

## Measured on real builds

**All eight release variants compile and link successfully**, and z0 with
`COMPOSITE` is correctly rejected at configure time by `CMakeLists.txt:44-46`.
Nothing in this report describes a build that is currently broken.

| Platform | Driver | Flash | of 4 MB | RAM | of 512 KB |
|---|---|---|---|---|---|
| m2 | `HDMI_PIO_AUDIO` | 353,340 | 8.4% | 334,276 | 63.8% |
| m2 | `HDMI_PIO` | 348,916 | 8.3% | 297,268 | 56.7% |
| m2 | `COMPOSITE` | 361,288 | 8.6% | 389,172 | 74.2% |
| m1 | `HDMI_PIO_AUDIO` | 353,340 | 8.4% | 334,276 | 63.8% |
| m1 | `HDMI_PIO` | 348,908 | 8.3% | 297,268 | 56.7% |
| m1 | `COMPOSITE` | 373,740 | 8.9% | 393,000 | 75.0% |
| z0 | `HDMI_PIO_AUDIO` | 366,128 | 8.7% | 336,136 | 64.1% |
| z0 | `HDMI_PIO` | 361,208 | 8.6% | 299,128 | 57.1% |

The default m2 `HDMI_PIO_AUDIO` build also reports:

```
SCRATCH_X:   2,404 B of 4 KB     58.7%
SCRATCH_Y:   2,688 B of 4 KB     65.6%
XIP_RAM:         0 B of 16 KB     0.0%
```

Three things follow.

**Flash is barely touched, under 9 percent in every variant.** That matters for
finding 6: the 2.6 MB of unused ROM and disc binaries in the tree cost nothing at
runtime, so removing them is purely a licensing and repository-size decision with
no technical downside.

**Composite is the tight configuration.** It runs at 75.0 percent of RAM on m1
against 56.7 percent for `HDMI_PIO`, a difference of about 96 KB, which is the
76.8 KB `tv_frame` buffer at `drivers/HDMI_tv.c:70` plus its overhead. Composite
also runs at 378 MHz and 1.60 V, confirmed in the configure output
(`Overriding CPU_SPEED to 378 MHz for composite TV`), so it is simultaneously the
hottest and the most memory-constrained variant. New static allocations should be
budgeted against it, not against the default.

**The two 4 KB scratch banks are the real constraint.** Both are already about
two thirds full, several drivers place data there deliberately, and the SDK also
uses them for stacks. There is far less headroom there than the RAM figure
suggests.

## Examined and not confirmed

Recorded so the same ground is not re-covered.

**The HDMI framebuffer index is not a bug.** One sweep reported that
`drivers/HDMI.c:441` reads `SCREEN[!current_buffer]` while
`src/frank/frank_gui.c:179` presents `SCREEN[current_buffer]`, and called it a
tearing defect. Tracing the sequence, `frank_gui.c:149` flips `current_buffer` at
the **start** of each frame, so during rendering `current_buffer` is the buffer
being written and `!current_buffer` is the completed one. Reading
`SCREEN[!current_buffer]` is correct double buffering. The ISR does ignore
`graphics_buffer` and the deferred `pending_vga_fb` swap, which is redundancy
rather than a defect.

**The HDMI_PIO path scans 240 of the 256 framebuffer rows.**
`drivers/HDMI.c:398` sets `CONTENT_SCANLINES (240*2)` and `:439-441` maps
source rows 0 to 239 only, while the framebuffer is 320x256
(`src/frank/frank_gui.c:28-29`). On the face of it the bottom 16 rows, where the
UI status area sits, are not displayed in `HDMI_PIO` builds. I could not
establish whether the vertical margins compensate. **Needs confirmation** on
hardware: build with `HDMI_DRIVER=HDMI_PIO` and check whether the bottom of a
MODE 7 screen and the overlay status line are visible.

---

## Recommended order of work

Nothing here has been done.

### Cheap, no risk to behaviour

1. Delete `PERFORMANCE.md`, having moved its hardware guidance into `CLAUDE.md`.
   Finding 2.
2. Correct the 27 wrong-project headers under `drivers/`. Finding 7.
3. Add licence headers and README rows for `drivers/tv/` and `drivers/ps2/`, and
   restore B-em's `COPYING`. Finding 8.
4. Add Ruby to the README prerequisites. Finding 19.
5. Fix the README: the Select+Start claim, the 8271 bullet, the UART sentence,
   `.ADM`, the read-only discs, the Z0 pin rows, the shared-pin note, the licence
   labels, the matrix count. Findings 9, 16.
6. Fix the stale comments for `PSRAM_SPEED`, `JIT_STUBS`, `FRANK_NO_AUTOBOOT` and
   `board_m2.h`. Findings 11, 19.
7. Delete `tools/micro_console.py` or rewrite it for the live protocol.
   Finding 10.

### Cheap, needs one build to confirm

8. Fix `src/bem/acia.c:28` to pass `event->user_data`. Finding 5.
   **Do this first of the code changes.**
9. Feed the watchdog in `boot_error_screen`, and correct the watchdog period.
   Finding 4.
10. Delete the 32 dead files in `src/Pico/`, move the four live ones into
    `src/frank/`. Finding 3.
11. Delete `frank_disc_preload()`, `drivers/HDMI_vga_hstx.c`,
    `drivers/sdcard/pio_spi.c` and the dead `config_values[]` table. Finding 11.
12. Tidy the `-Xlinker` line, which is redundant rather than broken. Finding 13.
13. Decide the `-O2` against `-O3` split explicitly. Finding 1.

### Worth doing, needs judgement

14. Make `frank_settings_save()` atomic, check its writes, and stop saving on
    every keypress. Finding 14.
15. Do not persist a Model change until confirmed, and clear `s_pending_restart`
    on cancel. Finding 14.
16. Resolve the gamepad key code at press time. Finding 14.
17. Add clipping to `ui_fill_rect` and `put_pixel`. Finding 14.
18. Move the screenshot write and the SD sector read off the frame-critical path.
    Finding 14.
19. Enable `-Wall -Wextra` on `src/frank/` and fix what it surfaces. Finding 12.
20. Add a compile-only CI job for all eight variants. Finding 18.
21. Mask interrupts in the PS/2 drain paths, and reclaim the wasted PIO1 state
    machine on M1 and Z0. Finding 15.

### Decide and record, policy rather than code

22. The ROM and disc binaries, and whether the repository can be distributed as
    it stands. Finding 6.
23. Whether to keep the four uncompiled vendored subdirectories and the 35 unused
    ROM images. Findings 6, 17.
24. Document the known limitations: read-only discs, MODE 0 and MODE 3 halved, 8
    colours, no tube, no tape, no save states. Finding 16.
