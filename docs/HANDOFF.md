# frank-micro: handoff log

What this file is: a running record of what has been established, what has been
decided, and what has been ruled out, so that none of it gets worked out twice.

It is a log, not a report. Append to it; do not rewrite history in it. If a later
session overturns an earlier entry, add a new entry that says so and leave the
old one in place with a pointer forward. The value of the file is that it shows
what was believed when, and why it changed.

## The four documents and what each is for

| File | Role |
|---|---|
| `CLAUDE.md` | Orientation. Read first. What is live, what is dead, how to build, where to change what. |
| `docs/INVESTIGATION.md` | Evidence. Twenty numbered findings about the repository as it stands, every claim cited to a file and line. |
| `docs/FRUIT-JAM.md` | Plan. What has to change to run on the Adafruit Fruit Jam, phased, with a provenance section recording how strong each claim is. |
| `docs/HANDOFF.md` | This file. Decisions, dead ends, and the register of what still needs hardware. |

None of the four is committed yet. `git status` shows `CLAUDE.md` and `docs/` as
untracked at the time of writing.

## How to add an entry

Each entry gets a date, a one-line statement of what was settled, and where the
detail lives. Absolute dates, never "last week". Say plainly whether something
was read from a file, derived by arithmetic, or assumed, and if it was assumed,
say what would confirm it. If a session tested something on hardware, that is
the most valuable kind of entry there is, so record the board, the build and the
result.

---

## Decisions

### 2026-08-21: the port targets B-em, and `PERFORMANCE.md` is not to be followed

`PERFORMANCE.md` describes the abandoned beebjit architecture. Anything under
`src/micro/`, and 32 of the 34 files in `src/Pico/`, are dead and in several
cases unbuildable. Recorded in `CLAUDE.md` sections 2 and 3, with the evidence in
`docs/INVESTIGATION.md` findings 2 and 3.

### 2026-08-21: new work goes in `src/frank/`, not in `src/bem/`

Local modification of the vendored B-em tree is about 104 lines across six files.
Keeping it that small is what keeps an upstream rebase feasible. `CLAUDE.md`
section 10.

### 2026-08-21: the Fruit Jam port is phased, and phase 1 does not touch the clock tree

Phase 1 boots and displays with a USB-C host adapter, phase 2 adds the codec,
phase 3 deals with the onboard USB-A ports. The reason for the split is that an
HSTX driver and a new board bring-up can each produce a black screen, and a board
with no serial console is a bad place to debug both at once. `docs/FRUIT-JAM.md`
section 8.

### 2026-08-22: phase 3 tests option C before writing any video driver

Changed from the earlier plan, which went straight to HSTX. Option C keeps
252 MHz and the existing PIO DVI driver and runs PIO-USB on the same clock. It
costs a one-line clock change, and if it holds, the HSTX driver and the clock
rework both disappear, and HDMI-embedded audio is kept. The reason this changed
is the finding below about PIO-USB's real clock requirement.
`docs/FRUIT-JAM.md` section 6, option C.

### 2026-08-22: if an HSTX driver is needed, it is vendored, not written

`adafruit/fruitjam-doom`'s `Framebuffer_RP2350.c` supplies the driver and the
scaling; `adafruit/pico-mac`'s `clocking.c` supplies the clock plan and the PLL
search. Both are MIT. Neither is vendored into this repository yet and neither
should be until a phase needs it. `docs/FRUIT-JAM.md` section 6, costs 1 and 2.

### 2026-08-22: under HSTX the framebuffer holds RGB332 bytes, not palette indices

This is the decision that keeps the video path free of a per-line CPU pass. It
moves the palette lookup into `rgb555_to_bbc()`, where a lookup already happens,
instead of adding one to the scanline path that `CLAUDE.md` constraint 7 says not
to add to. `docs/FRUIT-JAM.md` section 6, cost 2.

### 2026-08-22: the Fruit Jam is the only hardware, so the other three boards are frozen

Stated by the user: there is no Murmulator 1, Murmulator 2 or Waveshare Z0 here,
and there will not be. The Fruit Jam is the only board this project can test on.

Three consequences, all of them about verification rather than about what to
build.

**The `m1`, `m2` and `z0` platforms are now permanently unverifiable.** They are
frozen at "believed working, last tested by whoever had the hardware", which for
the tree as it stands is nobody in this project's history. The compile-only sweep
in `CLAUDE.md` section 9 is the only check that remains, and it catches broken
source lists and missing dependencies, not behaviour. So:

- Prefer additive, platform-gated changes to shared code over changes in place.
- Where shared code must change, argue the case for why existing boards are
  unaffected, and write that argument down next to the change.
- `release.sh` can keep building the other seven variants as a compile check, but
  they should not be published as tested.

**The first hardware session has no known-good baseline of our own.** Normally
you flash a build you trust to prove the board, the cable, the monitor and the
card before trusting a new port. That is not available. Use somebody else's
firmware instead: Adafruit ship CircuitPython for this board, and both
`pico-mac` and `fruitjam-doom` publish prebuilt UF2s. Between them they exercise
DVI output, the SD card, the codec and USB host input. Prove the hardware with
those before flashing frank-micro, so that a black screen means our code rather
than a monitor that will not accept a resistor-DVI signal.

CircuitPython is also the cheapest instrument for the small unknowns left in
`docs/FRUIT-JAM.md` section 10: which physical button is GPIO 4 and which is
GPIO 5, the polarity of card detect on GPIO 33, and an I2C scan to confirm the
codec really answers at 0x18.

**SWD matters more than it did.** Release builds have no console at all, and
there is now one board rather than four. The debug connector is confirmed from
the schematic: `DEBUG` is a JST SH 3-pin part with pin 1 `SWCLK`, pin 2 `GND` and
pin 3 `SWDIO`. A probe and that cable are close to essential.

One reassurance from the same source: bricking is not a risk. `BTN0` pulls the
`USBBOOT/BUTTON1` net to ground, and that net is the cathode of `D3` whose anode
reaches `QSPI_CS` through the 1K `R26`. That is the standard BOOTSEL arrangement,
so holding the button while plugging in USB always recovers the board, whatever
the firmware did.

---

### 2026-08-25: the board arrived, and phases 1a and 1b were written

First code for this port. `src/board_fj.h` is new; `CMakeLists.txt`,
`src/board_config.h`, `src/frank/frank_platform.c`, `frank_keyboard.c`,
`frank_settings.c`, `frank_audio.c`, `drivers/audio.c`,
`drivers/ps2/ps2kbd_wrapper.c`, `drivers/usbhid/CMakeLists.txt`,
`drivers/usbhid/tusb_config.h`, `drivers/usbhid/hid_app.c`, `build.sh` and
`README.md` were changed. Nothing has been run on hardware yet.

Every variant compiles and links, with `USB_HID` both off and on. The eight
pre-existing variants are unchanged in behaviour: `m2 HDMI_PIO_AUDIO` with
`USB_HID=0` comes out at the same RAM figure as before the port and 8 bytes
more flash, from the three-way funcsel fix described below.

### 2026-08-25: PS/2 and the NES pad are gated by capability macros, not by pin numbers

`HAS_PS2` and `HAS_NESPAD` are now defined in `board_m1.h`, `board_m2.h` and
`board_z0.h` and deliberately absent from `board_fj.h`, which declares no PS/2
or pad pin numbers at all. Five sites are guarded on them: the `PS2_MOUSE_CLK`
aliasing block in `board_config.h`, the `ps2_init()` call in
`ps2kbd_wrapper.c`, the `ps2kbd_init()` and `frank_gamepad_init()` calls in
`frank_platform.c`, the PS/2 drain in `frank_keyboard.c`, and the body of
`frank_gamepad_init()`.

This is the first working use of the `HAS_*` macros, which until now were
declared in three board headers and read nowhere. Every guard is additive and
the macro is defined for all three existing boards, so those boards come out of
the preprocessor identical to before.

### 2026-08-25: the Fruit Jam console is on UART1, not USB

`CMakeLists.txt` turned UART stdio off unconditionally, and turns USB CDC off
whenever `USB_HID_ENABLED` is set. On the other three boards that coupling is
right, because USB HID owns the native controller. On the Fruit Jam it is not:
the host is on PIO and the native controller is unused.

Rather than run a TinyUSB device stack and a host stack in one binary, UART
stdio is now enabled for `fj` only. The SDK board header puts UART1 on GPIO 8
and 9, both free on the 2x16 header, so a USB-serial adapter there gives a
console in every build. Verified with `arm-none-eabi-nm`: the `fj USB_HID=1`
image contains `stdio_uart` and no `stdio_usb`, where before this change it
would have contained neither and every `printf` would have gone nowhere.

Attaching a CDC device to the native port as well remains possible and is
recorded as a finding below, but was not attempted.

### 2026-08-25: `tools/setup.sh` exists because four things were missing

A build on this machine failed before it started. The `frank-hdmi-sound`
submodule was never initialised, and `CMakeLists.txt` does
`add_subdirectory(frank-hdmi-sound/src)`, so configure died rather than the
link. `PICO_SDK_PATH` was not exported and `build.sh` never set it. `picotool`
and `openocd` were both absent.

`tools/setup.sh` now initialises the submodules, locates the SDK and writes the
export to `tools/env.sh`, which `build.sh` sources, and reports the rest with
Arch package names. It exits non-zero on a blocker, so it works as a gate.

### 2026-08-25: phase 2 done, the codec works, and its levels are driven from the F12 volume

`drivers/tlv320dac3100.c` and `.h` are new. `hardware_i2c` was added to all three
`drivers` link lines and the source to all three source lists; the whole file is
behind `#ifdef CODEC_I2C_ADDR`, so it compiles to four no-op stubs on the boards
that have no codec. `frank_platform.c` calls `tlv320_init(31250)` after the SD
mount and before b-em starts, so a codec that does not answer reports itself on a
screen that already works and cannot stop the machine booting.

**Confirmed on hardware:** sound from the headphone jack with `Audio Out` set to
I2S in F12. This is also the first hardware exercise of I2S on PIO2 and therefore
of the three-way funcsel fix, since a wrong pad function there gives silence.

The clock constants are `P 1, R 2, J 52, NDAC 13, MDAC 2, DOSR 128` from
`tools/tlv320_clocks.py`, putting the PLL at 104.000 MHz and DAC_MOD_CLK at
4.000 MHz at 31,250 Hz. No resampler: the rate is native, so the I2S path in
`frank_audio.c` is unchanged.

Three deliberate departures from pico-mac's sequence, all marked DEPARTURE in the
source:

- **Register 0x33 is not written.** It configures the codec's GPIO1 as an
  output, and on this board GPIO 23 joins that pin to the ESP32-C6's
  `IO9/BOOT9`. Register 0x30, the INT1 routing that goes with it, is also
  skipped. frank-micro has no use for either.
- **NADC and MADC, registers 0x12 and 0x13, are not written.** This part is a
  DAC and has no ADC to divide for.
- **The 1000 ms post-reset sleep is replaced with the datasheet's figures**, 2 ms
  after reset and 10 ms after the PLL powers up.

### 2026-08-25: the F12 volume drives the codec's analogue stage, not a software divide

pico-mac hardcodes the analogue output registers at 50 for the headphones and 40
for the speaker, roughly -26 and -20 dB, and that is what the first working
version used. On hardware it was clearly audible but too quiet even with
frank's software volume at 100 percent, which confirmed the analogue stage was
the whole of the loss.

`tlv320_set_volume()` now maps the 0 to 100 setting onto those registers, with
100 percent at the codec's 0 dB and 0 percent at about -60 dB. `frank_audio.c`
hands the codec full-scale samples and skips the software divide whenever a codec
is present.

Worth understanding rather than just recording: dividing 16-bit samples by up to
100 in software throws away most of the resolution of an already coarse sound
source, and the attenuation then happens *before* a heavily attenuated amplifier.
Doing it in the analogue domain after the DAC costs no resolution at all. The
software divide is kept for boards with no codec, where I2S feeds a bare DAC with
no volume control of its own.

## Findings that changed the plan

### 2026-08-25: do not do I2C from the audio producer path

Recorded because it was introduced, shipped, heard, and removed inside one
session, and the reasoning error is easy to repeat.

The first codec version had an automute: count consecutive all-silent buffers,
and mute the output amplifiers after about half a second of silence. Muting the
amps rather than feeding them zeroes is the right way to kill idle hiss, and the
check itself is cheap. The mistake was where it ran.

Unmuting takes three register read-modify-writes, so six I2C transfers, and at
100 kHz that is over a millisecond of blocking. It ran inside
`give_audio_buffer()`, on **every transition from silence to sound**, which on
this machine is every note onset. On hardware the I2S backend was audibly worse
than the HDMI one because of it.

This is the same failure mode as commit `82469e5`, where USB-CDC telemetry
stalling core 0 caused audio dropouts, and it is what `CLAUDE.md` constraint 6
is about. The error in reasoning was checking that the *steady state* was cheap
and not that the *transition* was.

The amps are now unmuted once when the I2S backend is selected and muted once
when it is left, so steady-state I2C traffic is zero and a volume change costs
three writes on a keypress. If idle hiss ever justifies an automute, drive it
from a per-frame tick with a long timeout, never from the producer.

**Confirmed on hardware:** with the I2C removed from the producer path, the I2S
backend was reported as sounding good. So this was the whole of the complaint,
and the I2S ring rework described in the next finding was **not** needed. No idle
hiss was reported either, which is why nothing replaced the automute.

### 2026-08-25: the I2S backend has had none of the dropout work the HDMI path has had

Not a Fruit Jam issue and not new, but it sets expectations for how good I2S can
sound, so it belongs here.

The HDMI audio path has been through four rounds of dropout engineering: a bigger
ring with adaptive fill control (`36649a1`), consumer DC-hold on ring underflow
(`fc1666c`), locking the producer rate to the HDMI audio clock (`f8a4b97`), and
the move to a standard 32,000 Hz sink rate (`ba215c4`).

The I2S path has none of it. `drivers/audio.c` is a **two**-buffer ping-pong of
882 frames each, so:

- `i2s_dma_write()` at `:219-244` **spins with interrupts toggling** until a
  buffer frees, which stalls core 0 and therefore the emulator.
- The two DMA channels chain to each other unconditionally, so if the producer
  is late the DMA **replays the stale contents of the buffer it lands on**. The
  granularity is a whole 882-frame buffer, about 28 ms, which is long enough to
  hear as a stutter rather than a click.

Fixing it properly means a ring of more, smaller slots with the IRQ advancing
through them and filling an unwritten slot with a DC hold. That is a
several-hundred-line change to code shared with three boards that cannot be
tested, so it needs the additive, platform-gated treatment prescribed above.

**It has not been done, and as of 2026-08-25 there is no evidence it is needed.**
The one complaint about I2S quality turned out to be the automute above. Do not
start this speculatively; wait for a symptom that survives the two cheaper
explanations below.

**Two cheaper explanations to rule out first**, if I2S ever sounds worse than
HDMI:

1. **Clipping.** At 100 percent the analogue stage now sits at 0 dB with
   full-scale digital going into it. A few dB of headroom may be all that is
   wanted; try 85 or 90 percent and listen.
2. **Different transducers.** HDMI audio comes out of the monitor's speakers and
   I2S out of the headphone jack or the onboard speaker. Comparing the two is
   not comparing backends, it is comparing loudspeakers.

### 2026-08-25: correction, the SDK on this machine is 2.2.0, not 2.3.0

The table at the end of this file said Pico SDK tag 2.3.0, commit `98a542c1`.
`/home/adq/dev/pico-sdk` is at tag `2.2.0`, commit `a1438df`. That is also what
`CMakeLists.txt:7` pins as `sdkVersion`. Nothing was blocked: the Fruit Jam
board header is present at 2.2.0, which is the release it arrived in.

### 2026-08-25: correction, phase 1a is not silent

`docs/FRUIT-JAM.md:97` describes phase 1a as having no sound, and the effort
table repeats it. That is wrong. The default video driver is `HDMI_PIO_AUDIO`
and `FRANK_AUDIO_DEFAULT` is `FRANK_AUDIO_HDMI`
(`src/frank/frank_settings.h:51-63`), so audio is embedded in the DVI stream and
needs nothing on the board. The codec matters only for the headphone jack and
the onboard speaker.

This has a consequence for phase 2 worth stating plainly: **sound is not a
reason to write the codec driver.** The reason is the 3.5 mm jack and the
onboard speaker.

### 2026-08-25: do not use `tannewt/Pico-PIO-USB`; use upstream at the commit TinyUSB pins

The table at the end of this file records reading `tannewt/Pico-PIO-USB` at
`f3f9d11`, and `docs/FRUIT-JAM.md` builds its PIO analysis on that fork. That
fork cannot be used here, for two independent reasons.

It has **no TinyUSB integration at all**: `PIO_USB_USE_TINYUSB` appears nowhere
in it. And its file layout is flat, with no `src/` directory, no
`pio_usb_host.c`, no `pio_usb_device.c` and no `pio_usb_ll.h`. The Pico SDK's
TinyUSB glue requires all four of those paths, and `hcd_pio_usb.c` includes
`pio_usb_ll.h` directly. It is an old fork that predates the split.

The submodule is therefore `sekigon-gonnoc/Pico-PIO-USB` at `lib/Pico-PIO-USB`,
pinned to `fe9133fc513b82cc3dc62c67cb51f2339cf29ef7`, tagged 0.6.1. That is the
exact commit TinyUSB 0.18.0 lists for it in `tools/get_deps.py:61-63`, which is
the version its `hcd_pio_usb.c` is written against. **Do not float this to the
upstream default branch:** HEAD has since changed the PIO index mapping, so a
newer commit changes behaviour rather than only fixing bugs.

### 2026-08-25: PIO-USB needs one PIO block and three state machines, and it must be PIO0 or PIO1

This is the question phase 1b was told to settle before any code was written.
Read from `lib/Pico-PIO-USB/src/pio_usb_configuration.h` and `src/pio_usb.c` at
the pinned commit.

`pio_usb_configuration_t` carries `pio_tx_num` and `pio_rx_num` as **separate
fields**, which is what raised the worry that host mode needed two blocks. It
does not. The defaults are `PIO_USB_TX_DEFAULT 0` and `PIO_USB_RX_DEFAULT 0`
with `sm_tx` 0, `sm_rx` 1 and `sm_eop` 2, so one block and three state machines,
and `pio_sm_claim` is called exactly three times.

Two constraints come with it. `pio_usb.c:284` and `:287` map the index with
`c->pio_tx_num == 0 ? pio0 : pio1`, so **PIO2 is not addressable** at this
commit. And `pio_usb.c:241` does `pio_add_program_at_offset(pio_usb_tx,
fs_tx_program, 0)`, so the transmit program must sit at instruction offset 0,
which means the block has to be one nothing else has programmed.

So the allocation is forced, and it is the one `docs/FRUIT-JAM.md:986-990`
predicted:

| Block | Claimant | State machines |
|---|---|---|
| PIO0 | video, three TMDS serialisers | 3 |
| PIO1 | PIO-USB | 3, and instruction offset 0 |
| PIO2 | I2S, once the codec driver exists | 1 |

PIO1 is free for this only because the Fruit Jam has no PS/2 socket and no NES
pad connector. On the other three boards PIO1 is exactly full.

Note for later: upstream HEAD, `5a37a66`, replaces the one-bit test with
`pio_get_instance(c->pio_tx_num)` and can reach PIO2. If PIO2 is ever needed for
USB, that is the change to look for, and it comes with a configuration API that
TinyUSB 0.18.0's `hcd_pio_usb.c` does not match.

### 2026-08-25: PIO-USB's other resource claims, and why none of them collide here

- **One DMA channel**, `dma_claim_mask(1 << c->tx_ch)` at `pio_usb.c:325`, with
  a default of channel 0. That default is unusable here, because the video
  driver claims channels dynamically from the bottom. It is pinned to 7 instead,
  the highest channel that collides with neither the hardcoded I2S pair at 10
  and 11 (`drivers/audio.c:47-48`) nor the hardcoded PWM pair at 8 and 9
  (`drivers/pwm_audio/pwm_audio.c:67-68`). `dma_claim_mask` panics on an
  already-claimed channel, so this had to be explicit rather than left to
  initialisation order.
- **Hardware alarm 2**, from `alarm_pool_create(2, 1)` at `pio_usb_host.c:89`,
  driving a 1 ms repeating SOF timer. That is `TIMER0_IRQ_2`, which the
  composite driver also uses; composite is not built on this board, so it is
  free.
- The video driver in the `frank-hdmi-sound` submodule claims its DMA with
  `dma_claim_unused_channel(true)` (`frank_dvi.c:92-93`), so it routes around
  whatever PIO-USB took regardless of ordering.

### 2026-08-25: the SDK's own TinyUSB supplies every bit of PIO-USB build glue

None of the three earlier revisions of `docs/FRUIT-JAM.md` mentions this, and it
removes most of what phase 1b was expected to cost.

`pico-sdk/lib/tinyusb/hw/bsp/rp2040/family.cmake:269-315` defines a
`tinyusb_pico_pio_usb` INTERFACE target that compiles `pio_usb.c`,
`pio_usb_host.c`, `pio_usb_device.c` and `usb_crc.c`, generates the `usb_tx.pio`
and `usb_rx.pio` headers, adds `hcd_pio_usb.c` to `tinyusb_host_base`, defines
`PIO_USB_USE_TINYUSB`, and links `hardware_dma`, `hardware_pio` and
`pico_multicore`. It takes the library path from `PICO_PIO_USB_PATH`, defaulting
to a TinyUSB-internal directory that does not exist in the SDK checkout, because
TinyUSB 0.18.0 declares no submodules and fetches its dependencies with
`tools/get_deps.py` instead.

So the library needs pinning and pointing at, not vendoring. `PICO_PIO_USB_PATH`
has to be set **before `pico_sdk_init()`**, because that is when the SDK
processes its tinyusb directory and `check_and_add_pico_pio_usb_support()` runs.

One more thing worth knowing about the RP2350: `hcd_pio_usb.c:29` guards on
`CFG_TUSB_MCU == OPT_MCU_RP2040`, which looks like it would exclude this chip. It
does not. The SDK builds TinyUSB with `FAMILY rp2040`, and `family.cmake:68`
defines `CFG_TUSB_MCU=OPT_MCU_RP2040` for the whole family, RP2350 included.

### 2026-08-25: with PIO-USB enabled, the native USB controller is left entirely free

`hcd_rp2040.c:30` guards on `!CFG_TUH_RPI_PIO_USB`, so setting that macro
compiles the native host controller out of the build completely. The PIO port
becomes the only host, and `hcd_pio_usb.c:43` sets `RHPORT_OFFSET 1`, so TinyUSB
root port 1 is PIO port 0 and `tuh_init(1)` is the only valid call. The pin,
PIO block and DMA channel reach the driver through
`tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)`, which must
happen before `tuh_init()` because `hcd_init()` is what calls
`pio_usb_host_init()`.

Confirmed on the linked `fj USB_HID=1` image: 32 `pio_usb*` symbols present,
zero native `hw_endpoint_*` or `rp2040_usb_init` symbols.

The consequence is that nothing structural stops a TinyUSB CDC device on the
native USB-C port alongside the PIO host: there is no duplicate `hcd_`
definition to trip over. It needs `CFG_TUD_ENABLED` and `CFG_TUH_ENABLED`
together and both task loops serviced, which is what Adafruit's CircuitPython
does on this board. Not attempted. The UART1 console is the answer instead, and
this is the route back to a USB console if the UART proves inconvenient.

### 2026-08-25: the two-way PIO funcsel is now a three-way test

`drivers/audio.c:99` and `src/frank/frank_audio.c:66-68` both mapped any
non-`pio0` block to `GPIO_FUNC_PIO1`. `docs/FRUIT-JAM.md` called this latent;
moving I2S to PIO2 makes it live, so both are fixed. The `drivers/audio.c` fix
costs 8 bytes of flash on the other three boards and is behaviourally identical
there, since `pio1` still resolves to `GPIO_FUNC_PIO1`.

### 2026-08-22: PIO-USB does not require a 120 or 240 MHz system clock

Two earlier revisions of `docs/FRUIT-JAM.md` stated that it does, and built the
whole clock conflict on it. The assertion came from Pico-PIO-USB's Arduino
example. The library itself contains no clock check: it computes four dividers
from `clk_sys` at run time at `pio_usb.c:1399-1411`, full-speed transmit at
`clk_sys / 48 MHz` and receive at `clk_sys / 96 MHz`. Its C examples say
"Sysclock should be multiple of 12MHz"
(`example/usb_device/usb_device.c:125`), which is what the arithmetic supports.

252 MHz is 21 times 12 MHz and both its full-speed dividers land on exact
multiples of one 256th, the resolution of the PIO's fractional divider field. Its
jitter quantum, one system-clock period, is 4.0 ns against 7.6 ns for the 132 MHz
configuration Adafruit ship and that works.

Status: the arithmetic is verified, the conclusion is not. It needs a hardware
soak. This is now the question that decides the size of phase 3.

### 2026-08-22: HSTX needs no CPU pass and no line buffer for this framebuffer

Also stated wrongly in earlier revisions, which said an index-to-RGB565 pass was
unavoidable. `expand_tmds` is a per-lane bitfield extractor: each lane
right-rotates the shifter by its own amount and takes `NBITS+1` bits from bit 7
down. Read from the field descriptions in
`pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/hstx_ctrl.h`.

`fruitjam-doom/Framebuffer_RP2350.c` then does the rest in shipping code: an
RGB332 lane configuration at `:397-404`, horizontal doubling by one 8-bit DMA
transfer per pixel replicated across the bus at `:300-311` and `:356-364`, and
arbitrary vertical scaling by row-index arithmetic in the DMA command list at
`:362`. Cost to the CPU: nothing.

Consequence beyond the video path: the 16-line clip recorded in
`docs/FRUIT-JAM.md` section 3 is a property of the PIO video path only. Under
HSTX the vertical scale is arbitrary, so it does not arise at any mode.

### 2026-08-22: DMA byte-lane replication is documented, so the pixel doubling is sound

This was the one mechanism in the finding above that rested on another project's
code comment rather than on a specification, so it was checked against the RP2350
datasheet. It is stated three times.

Section 2.1.5, "Narrow IO register writes", page 27: memory-mapped IO registers
"ignore the width of bus read/write accesses" and treat every write as 32 bits,
and "upon a 8-bit or 16-bit write ... the narrow value is replicated multiple
times across the 32-bit data bus, so that it is broadcast to all 8-bit or 16-bit
segments of the destination register". The DMA chapter introduction, section
12.6: "The DMA performs byte lane replication on narrow writes, so byte data is
available in all 4 bytes of the databus, and halfword data in both halfwords."
And the WS2812 PIO example, section 11.6: "each byte the DMA transfers will
appear replicated four times when written to a 32-bit IO register". There is an
official example at `pico-examples/system/narrow_io_write/narrow_io_write.c`.

Three consequences worth keeping:

- Framebuffer alignment does not matter. The read side picks the correct byte
  lane from the transfer size and the address LSBs, so a row can start at any
  byte offset.
- The behaviour can be switched off, and that is a hazard here rather than a
  feature. Setting address bit 14, by addressing the peripheral at +0x4000,
  drives the invalid byte lanes to zero instead. Never use an aliased FIFO
  address in an HSTX driver. The SDK defines no constant for that alias, so it
  cannot be hit by accident.
- One inference remains. The datasheet says "the majority of" IO registers ignore
  access width and does not list them, so that the HSTX FIFO is one of them rests
  on the general rule plus the fact that the Doom port works.

The datasheet was downloaded to a scratch directory for this check and is not
kept in the repository. Re-fetch document RP-008373-DS from
`https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf`. Note
that no PDF tooling is installed on this machine: the text was extracted by
inflating the PDF's content streams with a short Python script.

### 2026-08-22: the codec PLL runs from the bit clock, so MCLK need not be driven

This was the weakest claim in `docs/FRUIT-JAM.md` section 5 and it is now
primary. `pico-mac/src/main.c:640-650` writes register 0x04 to select PLL_CLKIN
as BCLK and CODEC_CLKIN as the PLL, and never touches GPIO 25.

### 2026-08-22: pico-mac's clock plan is better than the one first proposed

Pin `pll_usb` at 528 MHz and feed `clk_sys`, `clk_peri`, `clk_usb` and `clk_adc`
from it with integer dividers, which leaves `pll_sys` free to be reprogrammed for
whatever pixel clock the video mode needs. 528 over 11 is exactly 48 for USB, and
528 over 2 and over 4 give 264 and 132, both exact multiples of 12 MHz.
`clocking.c:190-214` brute-forces `pll_sys` for a requested bit clock. The
earlier plan had this the other way round and pinned the system to 240 MHz, which
would have cost the frame pacer 5% of its margin.

### 2026-08-22: the schematic is public, and it removed phase 1's input path

Adafruit publish the Fruit Jam schematic as Eagle 9.6.2 XML in
`adafruit/Adafruit-Fruit-Jam-PCB`, commit `252dc09`. Netlists come out of
`Adafruit Fruit Jam.sch` with a short Python script over its `<nets>` elements.
That settled every "needs confirmation from the schematic" in
`docs/FRUIT-JAM.md`. Two results matter and four are worth knowing.

**The USB-C port cannot be a host.** `R19` and `R20` put 5.1K from each CC pin to
ground, which is sink termination and not switchable. VBUS on that connector is
an input, reaching the 5 V rail through `Q3`, a P-channel MOSFET gated by the
mechanical power switch. The only firmware-controlled 5 V switch is GPIO 11,
which drives `Q1`, which pulls the gate of `T1`, which feeds the `VBUS` net
through a 1 A polyfuse to the two USB-A sockets and the hub. It never reaches the
USB-C connector. So a USB-C-to-A adapter cannot make that port a host, option A
in section 6 is dead, and PIO-USB is the only input path the board has.

**The DVI wiring is exactly as hoped.** Each of GPIO 12 to 19 reaches the
connector through one resistor of a 220 ohm four-pack, `R14` for 12 to 15 and
`R12` for 16 to 19, and through nothing else. Tracing the resistor gates gives
the polarity too: even GPIOs are the negative halves, odd are the positive, which
matches `board_m2.h:35-42` exactly. The existing pin definitions carry across
unchanged.

Four things nobody had asked about:

- `PERIPH_RST` on GPIO 22 has a 10K pull-up to 3.3 V, so the codec and the
  ESP32-C6 both leave reset with no firmware action. The hazard is asserting that
  line, not forgetting to release it.
- GPIO 23 is a three-way net: the codec's `GPIO1`, the ESP32-C6's `IO9/BOOT9`,
  and the `ESP_BOOT` header. The borrowed codec sequence configures the codec's
  `GPIO1` as an output at register 0x33, which on this board can drive the
  ESP32's boot strap. Decide about that write rather than copying it.
- Card detect on GPIO 33 and both buttons on GPIO 4 and 5 are bare contacts to
  ground with no external resistors. They need internal pull-ups.
- There is no hot-plug detect. The connector's `HOTPLUG` pin goes through a 100K
  over 100K divider to a net that reaches no GPIO, so firmware cannot tell whether
  a monitor is attached.

The hub is a CH334F with its own 12 MHz crystal, no reset or configuration pin
wired, and its 5 V from the switched rail. So it needs GPIO 11 and a settling
delay and nothing else. It has three downstream ports: the two sockets and one on
the 2x16 header at `JP3` pins 14 and 16. Its two front LEDs are driven by the hub
itself.

### 2026-08-22: the codec clock constants are computed, and both Adafruit projects are out of spec

`tools/tlv320_clocks.py` now holds the arithmetic, with every constraint written
next to the clause of TI document SLAS671C it comes from, a solver, an audit mode
and a self-test. Run `python3 tools/tlv320_clocks.py` for the answer and the
working, and `--self-test` after changing it.

The answer for frank-micro, valid at both 31,250 and 32,000 Hz: P 1, R 2, J 52,
NDAC 13, MDAC 2, DOSR 128. 1,348 configurations are valid at both rates; this is
the one the datasheet's own ranking prefers, being the largest NDAC that still
satisfies `MDAC x DOSR / 32 >= RC`.

**This corrects an earlier entry.** The third revision of `docs/FRUIT-JAM.md`
said the two Adafruit projects could not both be right because their sample rates
differ by a factor of 2.2 while their register values are identical. That
reasoning was wrong. Those values encode a ratio, not a frequency:
`NDAC x MDAC x DOSR` is 2048 and so is `32 x R x J / P`, and since the PLL runs
from the bit clock the whole chain scales with the sample rate. The divider chain
is exact at both rates.

What is actually wrong with them is the absolute limits, which do not scale:

- pico-mac at 22,256 Hz puts the PLL at 45.580 MHz, against a specified minimum
  of 80 MHz.
- fruitjam-doom at 49,716 Hz puts DAC_MOD_CLK at 6.364 MHz, against a specified
  maximum of 6.2 MHz.
- The same values at frank-micro's 31,250 Hz would put the PLL at 64 MHz, again
  below the minimum.

Both produce sound regardless, which is worth remembering when judging borrowed
register sequences: working is not the same as correct.

A second correction from the same pass. The third revision said pico-mac's I2S
puts 64 bit clocks in a stereo frame. It does not. `pico-extras`'s
`audio_i2s.pio` is the same program as `drivers/audio_i2s.pio`, and its
`update_pio_frequency()` uses the same `system_clock_frequency * 4 /
sample_freq` divider as `drivers/audio.c:121-123`. All three projects have 32
bit clocks per frame, so BCLK is 32 times the sample rate everywhere.

---

### 2026-08-25: correction, pin Pico-PIO-USB to upstream main, not to the commit TinyUSB names

The entry above says to pin `fe9133f`, tag 0.6.1, because that is the commit
TinyUSB 0.18.0 lists in `tools/get_deps.py`. That was the right instinct for API
compatibility and the wrong one for silicon support, and it cost a hardware
session.

**0.6.1 predates RP2350 support entirely.** It is 58 commits behind upstream
main, and those commits include:

| Commit | What it adds |
|---|---|
| `08a9b06` | workaround for RP2350-E9, which stops PIO-USB host detecting a device at all |
| `a810cb4` | keeps that workaround's code in SRAM |
| `b559b3e` | RP2350 PIO2 support, via `pio_get_instance` and `PIO_IRQ_NUM` |
| `a7d2b61` | pins above 32 on the RP2350B |
| `2f17cec`, `3f80202` | correct `PICO_RP2350` testing, which is always defined and may be 0 |

The E9 one is the one that matters here. The workaround sits in
`pio_usb_bus_get_line_state()` at `src/pio_usb_ll.h:144-160`, and that function
is what `hcd_port_connect_status()` calls. On affected silicon the pad leaks
current while input-enable is held on, so the line-state read is wrong and the
host never sees a device on the port. The fix clears `IE` on both D+ and D-,
waits eight nops for the leak to drain, sets `IE` again, then reads.

It is guarded on `chip_version <= 2`, and the Fruit Jam is A2, so it applies to
this board. Note also its comment that the drain delay was "tested with
264Mhz"; frank-micro runs 252 MHz, so it is inside what upstream has tried.

**Observed on hardware, 2026-08-25.** At `fe9133f` the board booted, displayed
the Master 128 MOS screen correctly and mounted the SD card, and a USB keyboard
in an onboard socket did nothing. Video, SD, PSRAM and the core 1 encoder were
all unaffected, which is what pointed at device detection rather than at the PIO
or DMA allocation.

The submodule is therefore pinned at `5a37a66`. It compiles clean against SDK
2.2.0's TinyUSB 0.18.0 `hcd_pio_usb.c`, so the API worry that motivated pinning
0.6.1 does not arise in practice. **Do not move it back.** If it ever has to
move again, the constraint to check is whether `hcd_pio_usb.c` still builds, not
what `get_deps.py` names.

One consequence worth knowing: at `5a37a66` the PIO index goes through
`pio_get_instance()`, so PIO2 **is** addressable and the one-bit-test constraint
recorded above no longer holds. The allocation is unchanged regardless, because
PIO1 is free on this board and the transmit program still needs instruction
offset 0.

### 2026-08-25: first hardware results, and option C holds

Board: Adafruit Fruit Jam. Build: `PLATFORM=fj`, `HDMI_DRIVER=HDMI_PIO_AUDIO`,
252 MHz, flashed as a UF2 over BOOTSEL. SD card: FAT32 with the `micro` tree
from `sdcard/micro.zip`, no disc images.

**Works, observed directly:**

- Cold boot to the BBC Master 128 MOS screen on a DVI monitor. Display looked
  correct.
- SD card mounted. `SD card mount: OK`.
- PSRAM detected, 7680 kB dlmalloc heap.
- Core 1 HDMI encoder started.
- b-em reached `Using Pico Thumb CPU`.
- USB keyboard in an onboard USB-A socket, behind the CH334F hub, **enumerates
  and types**, at 252 MHz, with the PIO DVI driver running.

**This settles the question that decided the size of the port.** `docs/FRUIT-JAM.md`
section 6 made phase 3, an HSTX video driver plus a clock-tree rework, contingent
on PIO-USB not working at 252 MHz alongside PIO video. It works. Option C holds,
so **phase 3 is not needed**, the PIO DVI driver stays, and HDMI-embedded audio
is kept.

What is proven is enumeration, not stability. The soak is still outstanding, and
the failure mode of a marginal clock is intermittent dropped packets rather than
a clean refusal, so hours of running with the keyboard attached is still the test
that matters.

**Not yet tested on hardware:** MODE 7 versus a bitmap mode, mounting a disc and
SHIFT-BREAK autoboot, HDMI audio, screenshots, `micro.ini` persistence, the F11
and F12 overlays, and the no-SD-card path.

One boot-log observation, pre-existing and not Fruit-Jam-specific:
`src/bem/model.c:46-55` declares `rom_setups[NUM_ROM_SETUP]` with
`NUM_ROM_SETUP` fixed at 5 while `#ifndef PICO_BUILD` removes two of the five
initialisers. So the BBC B model's `swram` lookup fails, the warning then claims
a fallback to `swram` when `rom_setups[0]` is actually `std`, and the search
loop calls `strcmp(NULL, name)` on the two zero-filled entries. It survives only
because there is no MMU and address 0 is readable on this part. Harmless in
practice, identical on all four boards, and out of scope while the rule about
keeping `src/bem/` divergence small stands.

## Dead ends, so nobody retries them

### `drivers/HDMI_vga_hstx.c` is not a starting point for an HSTX driver

It includes `disphstx.h` from the DispHSTX library, which is not vendored
anywhere in this repository, so it cannot compile rather than merely being
uncompiled. It targets analogue VGA for a 360x240 Amstrad CPC framebuffer and
carries frank-cpc headers. `docs/INVESTIGATION.md` finding 11.

### `HAS_HSTX` and the claim at `src/board_m2.h:12-13` mean nothing

`HAS_HSTX` is defined and used nowhere. Nothing in frank-micro or in the
`frank-hdmi-sound` submodule touches the HSTX peripheral. The comment claiming
HSTX HDMI is the default video and audio path is inherited from frank-cpc and is
false.

### Option A, a USB-C host adapter for input, is not possible

The USB-C port is wired as a device: 5.1K on both CC pins to ground, VBUS an
input through a P-channel MOSFET gated by the mechanical power switch, and no
firmware-controlled way to source 5 V on that connector. A conducting P-FET is
bidirectional, so a board powered from the 2x16 header with the switch closed
would back-feed roughly 5 V onto the USB-C VBUS, and a keyboard does not care
about CC. Do not build on that: it is off-specification, depends on a switch
position, and back-powers a connector designed as an input.

### `tools/micro_console.py` cannot work

It is frank-cpc's script and speaks the line-based protocol from the dead
`src/Pico/micro_serial_console.c`. The live console takes single bytes.
`docs/INVESTIGATION.md` finding 10.

### Do not take `gtf.c` from pico-mac

It computes VESA GTF timings, which are not the CEA-861 timings a television
expects. The `frank-hdmi-sound` submodule already carries a hand-tabulated table
including `dvi_timing_720x576p_50hz`.

### Do not take fruitjam-doom's clocking

It derives `clk_hstx` from `clk_sys` on a fractional divider and lands on
125.875 MHz instead of 126, which its own comment calls nonsense
(`Framebuffer_RP2350.c:201-205`). Use pico-mac's PLL search instead.

### Do not trust the codec PLL constants in either Adafruit project

`pico-mac/src/main.c` and `fruitjam-doom/src/i_main.c:59-190` contain the same
register block: P 1, R 2, J 32, NDAC 8, MDAC 2, and DOSR left at its reset 128.
It is a valid ratio at any sample rate, which is why it appears twice, but it
puts the PLL below its specified minimum at pico-mac's rate and DAC_MOD_CLK above
its specified maximum at Doom's. See the finding above, and use
`tools/tlv320_clocks.py` rather than copying either. The sequence around the
constants, the reset, the routing and the mute handling, is still worth taking.

### `csr.N_SHIFTS` alone cannot pixel-double

The data-repeat behaviour in the field description repeats the whole shifter
word, so 8bpp gives p0 p1 p2 p3 p0 p1 p2 p3. It doubles pixels only in
combination with a one-pixel-per-word DMA transfer.

### `CSR.COUPLED_MODE` is not a shortcut

It connects eight PIO output bits into HSTX bit selects 24 to 31, which would let
a PIO do a palette lookup feeding the hardware TMDS encoder. It requires
`clk_hstx` to be driven directly from `clk_sys`, which throws away the clock
decoupling that is the whole reason for using HSTX.

---

## Needs hardware

Nothing in any of these four documents has been tested on a Fruit Jam. No Fruit
Jam code has been written. The full list of open questions is
`docs/FRUIT-JAM.md` section 10; these are the ones that block a decision rather
than a detail.

| Question | Blocks | Cheapest way to settle it |
|---|---|---|
| **Answered 2026-08-25: yes.** Does the `fj` build boot to the MOS screen on a DVI monitor? | — | Done. See the first-hardware-results entry |
| **Answered 2026-08-25: yes, it enumerates.** Does PIO-USB work at 252 MHz with a keyboard behind the hub? | Settled the size of the port: phase 3 is not needed | Done, once the library was moved off 0.6.1 |
| Does that keyboard stay up over hours? | Nothing structural, but a marginal clock shows as intermittent dropped packets | Leave it running with the keyboard attached and type into it periodically |
| Do MODE 7 and a bitmap mode both render correctly? | Nothing. They take different blit paths in `frank_gui.c` | `MODE 7` then `MODE 1` from BASIC |
| Does a disc mount and SHIFT-BREAK autoboot? | Nothing | Put an `.ssd` in `/micro/disk/` and use F11 |
| **Answered 2026-08-25: yes.** Does the codec PLL lock at the computed constants? | — | Done; sound from the headphone jack |
| Does the onboard speaker work? | Nothing. Same driver, different output | Attach a speaker to the JST connector |
| Is the analogue stage clipping at 100 percent volume? | Nothing now: I2S was reported sounding good once the automute was removed. Left here only as the first thing to check if distortion is ever reported | Listen at 85 percent and compare |
| Which physical button is GPIO 4 and which is GPIO 5? | Nothing. Cosmetic | The board layout or a multimeter. The schematic's net and part names are crossed |

Settled since the last revision of this table, both by reading source rather
than by testing: how many PIO blocks and state machines PIO-USB needs, and which
blocks it can address. See the 2026-08-25 findings.

Resolved since the first version of this table: whether the USB-C port can be a
host (no), and whether the HSTX pins are directly driven (yes). Both from the
schematic, both recorded above.

## External sources consulted, with commits

Re-clone at these commits rather than at a branch tip. None is vendored into this
repository and none should be until a phase needs it.

| Repository | Commit | What it was read for |
|---|---|---|
| `adafruit/pico-mac` | `59c910b` | HSTX driver structure, the 528 MHz clock plan and PLL search, TLV320DAC3100 bring-up |
| `adafruit/fruitjam-doom` | `f2ded0b` | RGB332 and 4bpp lane values, DMA pixel doubling, command-list vertical scaling, a second copy of the codec sequence |
| `tannewt/Pico-PIO-USB` | `f3f9d11` | The divider computation, the absence of any clock check, the `pio0`-or-`pio1` restriction. **Not usable in the build**, see the 2026-08-25 finding: no TinyUSB integration and the wrong file layout |
| `sekigon-gonnoc/Pico-PIO-USB` | `5a37a66`, vendored as the submodule `lib/Pico-PIO-USB` | The PIO block and state-machine count, the DMA and alarm claims, the instruction-offset-0 constraint, and the RP2350-E9 line-state workaround. Pinned here rather than at the 0.6.1 that TinyUSB names, because 0.6.1 predates RP2350 support; see the 2026-08-25 correction |
| Pico SDK | tag `2.2.0`, commit `a1438df`, at `/home/adq/dev/pico-sdk` | Fruit Jam pin numbers, HSTX register semantics, clock aux sources, PLL VCO range, and the TinyUSB PIO-USB build glue in `lib/tinyusb/hw/bsp/rp2040/family.cmake`. Earlier revisions of this table said tag 2.3.0, commit `98a542c1`; that was wrong |
| RP2350 datasheet | document RP-008373-DS, fetched 2026-08-22 | Narrow IO register writes at section 2.1.5, DMA byte-lane replication at section 12.6 |
| TLV320DAC3100 datasheet | document SLAS671C, fetched 2026-08-22 | Every constraint in `tools/tlv320_clocks.py`, cited there by clause |
| `adafruit/pico-extras` | default branch, fetched 2026-08-22 | `audio_i2s.c` and `audio_i2s.pio`, to establish that BCLK is 32 times the sample rate in the Adafruit projects too |
| `adafruit/Adafruit-Fruit-Jam-PCB` | commit `252dc09` | The schematic, as Eagle XML. Cited by net name and part designator |

`PICO_SDK_PATH` was not set in the environment while these documents were
written. SDK paths are relative to `/home/adq/dev/pico-sdk`, found by searching.
