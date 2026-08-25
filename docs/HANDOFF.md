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

## Findings that changed the plan

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
| Does PIO-USB enumerate reliably at 252 MHz, over hours, with a keyboard behind the hub? | Whether the port needs an HSTX driver at all. It is now phase 1b, not phase 3, because there is no other input path | Build phase 1a first so only one thing is unproven, then soak phase 1b for hours |
| Which physical button is GPIO 4 and which is GPIO 5? | Nothing. Cosmetic | The board layout or a multimeter. The schematic's net and part names are crossed |

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
| `tannewt/Pico-PIO-USB` | `f3f9d11` | The divider computation, the absence of any clock check, the `pio0`-or-`pio1` restriction |
| Pico SDK | tag `2.3.0`, commit `98a542c1`, at `/home/adq/dev/pico-sdk` | Fruit Jam pin numbers, HSTX register semantics, clock aux sources, PLL VCO range |
| RP2350 datasheet | document RP-008373-DS, fetched 2026-08-22 | Narrow IO register writes at section 2.1.5, DMA byte-lane replication at section 12.6 |
| TLV320DAC3100 datasheet | document SLAS671C, fetched 2026-08-22 | Every constraint in `tools/tlv320_clocks.py`, cited there by clause |
| `adafruit/pico-extras` | default branch, fetched 2026-08-22 | `audio_i2s.c` and `audio_i2s.pio`, to establish that BCLK is 32 times the sample rate in the Adafruit projects too |
| `adafruit/Adafruit-Fruit-Jam-PCB` | commit `252dc09` | The schematic, as Eagle XML. Cited by net name and part designator |

`PICO_SDK_PATH` was not set in the environment while these documents were
written. SDK paths are relative to `/home/adq/dev/pico-sdk`, found by searching.
