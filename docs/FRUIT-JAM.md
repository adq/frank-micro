# Adding the Adafruit Fruit Jam as a target

Date: 2026-08-21
Scope: what has to change in `frank-micro` to build and run on the Adafruit
Fruit Jam (product 6200, RP2350B).
Method: the Fruit Jam pinout was read from the board header the Pico SDK
already ships, `pico-sdk/src/boards/include/boards/adafruit_fruit_jam.h`, cross
checked against Adafruit's published pinout page and the CircuitPython board
definition. The frank-micro side was read statically at commit `c27ffbf`. No
Fruit Jam hardware was tested, and nothing in the repository was changed.

Claims that need a board or a schematic are marked **needs confirmation** and
say what to check.

Third revision, 2026-08-22: sections 5 and 6 were rewritten against two working
Fruit Jam ports, `adafruit/pico-mac` and `adafruit/fruitjam-doom`, and against
Pico-PIO-USB's own source. Three claims in the first two revisions turned out to
be wrong: that PIO-USB requires a 120 or 240 MHz system clock, that the palette
expansion pass has to produce RGB565, and that a CPU pass is needed at all.

Fourth revision, 2026-08-22: **the schematic was read.** Adafruit publish it as
Eagle XML in `adafruit/Adafruit-Fruit-Jam-PCB`, so every "needs confirmation from
the schematic" in the first three revisions is now settled. Two results matter.
The DVI wiring is exactly as hoped, so phase 1's video is safe. And **the USB-C
port cannot be a host**, so option A does not exist and phase 1 has no input path
as it was written. The phasing in section 8 changed accordingly. See section 12.

---

## 1. Summary

The Fruit Jam is a closer fit to frank-micro than the Waveshare Z0 already is,
with one exception that dominates the whole design.

Four of the five hardware interfaces map onto the existing board abstraction
with no new driver code at all. The DVI pins are on GPIO 12 to 19 in exactly the
order `board_m2.h` already declares. The SD card is on hardware SPI0 with the
same three-pin-plus-chip-select shape the driver expects. The PSRAM chip select
is GPIO 47, which the existing RP2350B autodetect already returns. The I2S bit
clock and word select are on adjacent pins, which is the layout `drivers/audio.c`
requires.

The exception is input. **The Fruit Jam has no PS/2 connector and no NES pad
connector.** Its two USB-A ports sit behind a CH334F hub whose upstream pair is
wired to GPIO 1 and 2, the PIO-USB pins, not to the RP2350's native USB
controller. That was an inference from pin naming in earlier revisions and is now
read from the schematic: net `USBH_D+` joins `X4` GPIO 1 to `X6` pin `D+U`, and
`USBH_D-` joins GPIO 2 to `D-U`. frank-micro's USB HID support is
native-controller only; the PIO-USB scaffolding in
`drivers/usbhid/CMakeLists.txt:46-54` was never finished and the library is not
vendored.

**And the native controller cannot be used instead.** Both CC pins of the USB-C
connector carry a 5.1K resistor to ground, `R19` and `R20`, which is sink
termination: the port is wired as a device. Its VBUS is an input, reaching the
board's 5 V rail through `Q3`, a P-channel MOSFET gated by the mechanical power
switch, and nothing on the board can source 5 V onto that connector under
firmware control. GPIO 11 switches 5 V onto the USB-A side only, through `T1` and
a 1 A polyfuse. So a USB-C-to-A adapter will not turn that port into a host, and
PIO-USB is not one of several options but the only input path the board has.

There is a design reason to expect that inference to hold, set out in section 6.
The RP2350 has one USB controller. It is dual-role but not simultaneously so, and
its D+ and D- reach one connector, so a board gets exactly one native USB port
and has to choose what that port is for. Adafruit spent it on device mode at the
USB-C connector, because that is how the board is flashed and how CircuitPython
presents its drive and REPL. Once that is committed, a host port can only come
from PIO. This is reasoning, not a schematic, so it does not promote the
inference to a verified fact, but it does mean the pin naming is what a
deliberate design would look like rather than a coincidence.

That matters because PIO-USB needs a system clock its PIO dividers can hit
cleanly against USB bit timing, while the default PIO DVI driver requires
252 MHz. Whether those two requirements actually conflict is the central question
in this port, and since the USB-C port cannot be a host it is now a phase 1
question rather than a phase 3 one. Section 6 gives grounds for optimism:
252 MHz divides exactly and has a smaller jitter quantum than a configuration
Adafruit ship. If that holds, no new video driver is needed anywhere in the
plan. If it does not, HSTX has to be written before there is any keyboard.

Note where the conflict comes from. It is a property of frank-micro's video
driver, not of the Fruit Jam. Adafruit's own stack drives the DVI connector from
the HSTX peripheral, whose clock is independent of the system clock, so their
240 MHz system clock for PIO-USB costs them nothing. frank-micro drives DVI from
PIO instead, which is what pins it to 252 MHz. Option B in section 6 is
therefore the configuration the board was designed for, and options A and C are
the deviations.

Audio needs one genuinely new driver. The Fruit Jam has no PWM audio jack and no
bare I2S DAC. All sound goes through a TLV320DAC3100 codec that outputs nothing
until it is configured over I2C, and frank-micro has no I2C code anywhere.

Effort estimate, assuming the phased plan in section 8:

| Phase | Deliverable | New or changed code |
|---|---|---|
| 1a | Boots, DVI video, SD card, serial console over USB-C, no input and no sound | ~250 lines, mostly one board header and CMake |
| 1b | A USB keyboard in an onboard USB-A port | Vendor Pico-PIO-USB, move I2S to PIO2, drive GPIO 11. The open risk in the whole port |
| 2 | Sound through the onboard codec and speaker | ~150 lines of new I2C codec driver |
| 3 | Contingent, only if 1b will not run at 252 MHz | An HSTX video driver vendored from `fruitjam-doom` and `pico-mac`, an RGB332 framebuffer, and the loss of HDMI-embedded audio |

Phase 1a is routine. Phase 1b is where the port either works or turns into a
larger project, and it cannot be deferred, because the USB-C port cannot be a
host and there is no other way to attach a keyboard. Phase 3 may never be needed;
if it is, the two Adafruit ports supply most of it.

---

## 2. The board

RP2350B in the 80-pin package, so GPIO 0 to 47 and six ADC channels. 16 MB
flash, 8 MB PSRAM, 520 KB on-chip SRAM. Board size 85.7 by 54.0 mm.

Peripherals relevant to this port:

| Peripheral | Detail |
|---|---|
| Video | DVI connector on the HSTX pins, GPIO 12 to 19 |
| Storage | microSD on SPI0, with a card-detect line |
| Audio | TLV320DAC3100 codec, I2S in, stereo headphone out plus mono speaker out |
| Input | Two USB-A ports behind a CH334F hub, fed from PIO-USB on GPIO 1 and 2. Confirmed from the schematic. A third downstream port is on the 2x16 header |
| Device USB | USB-C on the native controller, for bootloading and USB client. Sink-only, so it cannot be a host |
| Other | ESP32-C6 for WiFi, five NeoPixels, three tactile switches, STEMMA QT, a 2x16 GPIO header |

The RP2350B fitted is A2 silicon, and the SDK board header sets
`PICO_RP2350_A2_SUPPORTED 1`. A secondary source says Adafruit note the part is
affected by erratum E9, the GPIO input latch behaviour when a pad is left
floating with only the internal pull-down. That attribution is **unconfirmed
against the datasheet**; see section 12.
frank-micro's only pull-dependent input is the NES pad data line, which uses a
pull-up (`drivers/nespad/nespad.c:83`), and that peripheral is not present on
this board, so E9 should not bite. **Needs confirmation** if anything is later
wired to the 2x16 header as an input.

### Verified pin assignments

Every number below comes from
`pico-sdk/src/boards/include/boards/adafruit_fruit_jam.h`, SDK 2.3.0.

| Function | GPIO |
|---|---|
| DVI CKN, CKP | 12, 13 |
| DVI D0N, D0P | 14, 15 |
| DVI D1N, D1P | 16, 17 |
| DVI D2N, D2P | 18, 19 |
| SD SCK, MOSI, MISO, CS | 34, 35, 36, 39 |
| SD card detect | 33 |
| SDIO DAT1, DAT2 | 37, 38 |
| I2S DIN, MCLK, BCLK, WS | 24, 25, 26, 27 |
| I2S ESP IRQ | 23 |
| Codec and ESP32 shared reset | 22 |
| I2C0 SDA, SCL (STEMMA QT and codec control) | 20, 21 |
| USB host D+, D-, 5V enable | 1, 2, 11 |
| PSRAM chip select | 47 |
| NeoPixel | 32 |
| LED | 29 |
| Buttons 1 (BOOTSEL), 2, 3 | 0, 4, 5 |
| UART1 TX, RX | 8, 9 |
| ESP32-C6 SCK, MOSI, MISO, CS, ACK | 30, 31, 28, 46, 3 |
| A0 to A5 | 40 to 45 |
| D6, D7, D8, D9, D10 | 6, 7, 8, 9, 10 |

The SPI function mapping for the SD pins was checked against the register
headers rather than inferred: `IO_BANK0_GPIO34_CTRL_FUNCSEL_VALUE_SPI0_SCLK`,
`GPIO35` is `SPI0_TX`, `GPIO36` is `SPI0_RX`. GPIO 39 is also `SPI0_TX`, but
chip select is driven as a plain GPIO by `drivers/sdcard/sdcard.c:135-139`, so
that overlap is harmless.

---

## 3. What needs no new code

### DVI pin order matches `board_m2.h` exactly

`board_m2.h:35-42` declares `HDMI_PIN_CLKN 12` through `HDMI_PIN_D2P 19`. The
Fruit Jam header declares the same eight pins in the same order. `HDMI_BASE_PIN
12` and `VGA_BASE_PIN 12` carry across unchanged.

**Confirmed from the schematic, including polarity.** Each of the eight pins
reaches the connector through one resistor of a 220 ohm four-pack, `R14` for
GPIO 12 to 15 and `R12` for GPIO 16 to 19, and through nothing else. No buffer,
no level shifter, no reordering and no inversion, which is the case the PIO DVI
driver in `drivers/HDMI.c` needs. It is the same passive resistor-DVI scheme the
PicoDVI adapter uses.

Tracing each net through its resistor gate gives the polarity as well:

| GPIO | Net | Connector pin | `board_m2.h` |
|---|---|---|---|
| 12 | `HSTX12` | `TXC-` | `HDMI_PIN_CLKN 12` |
| 13 | `HSTX13` | `TXC+` | `HDMI_PIN_CLKP 13` |
| 14 | `HSTX14` | `TX0-` | `HDMI_PIN_D0N 14` |
| 15 | `HSTX15` | `TX0+` | `HDMI_PIN_D0P 15` |
| 16 | `HSTX16` | `TX1-` | `HDMI_PIN_D1N 16` |
| 17 | `HSTX17` | `TX1+` | `HDMI_PIN_D1P 17` |
| 18 | `HSTX18` | `TX2-` | `HDMI_PIN_D2N 18` |
| 19 | `HSTX19` | `TX2+` | `HDMI_PIN_D2P 19` |

So the match with `board_m2.h:35-42` is exact down to which half of each pair is
positive, and the existing pin definitions carry across with no change at all.

One thing to carry into an HSTX driver if one is ever written: the positive pins
are the odd-numbered ones. pico-mac's CMake defaults agree, `HSTX_CKP 13` and so
on, but `fruitjam-build.sh:68` overrides them to 12, 14, 16 and 18 in one of its
two configurations, which inverts every pair. Use the odd pins.

There is no hot-plug detect. The connector's `HOTPLUG` pin goes through a 100K
over 100K divider to a net that reaches no GPIO, so firmware cannot tell whether
a monitor is attached. The connector's +5V pin is tied to the board's 5 V rail.

Both PIO blocks that could drive video use the default GPIO base of 0. The
Fruit Jam's base pin of 12 is below the 16 threshold at `drivers/HDMI.c:539`,
so none of the re-basing added for the Z0's high pins fires. In passing: the
two video drivers disagree about how to re-base. `HDMI.c:539-541` selects base
16 for any base pin of 16 or above, and its comment says base 32 is not a
valid RP2350 PIO base, while `HDMI_vga.c:432-436` passes base 32 for pins of
32 and up, exactly what the other driver calls broken. Neither branch is
reached on this board, but it is a latent inconsistency worth a ticket.

### SD card works on the existing SPI path

`drivers/sdcard/sdcard.h:7-8` defaults `SDCARD_SPI_BUS` to `spi0`, which is
correct for the Fruit Jam, so no override like the Z0's
`CMakeLists.txt:518` is required. The four pin macros just take new values.

One caveat: the SD pin numbers are declared twice, once in `board_*.h` and again
as CMake variables `SD_CS`, `SD_SCK`, `SD_MOSI`, `SD_MISO` pushed in as compile
definitions at `CMakeLists.txt:461-464`. Both copies must be set, and they must
agree. This duplication is pre-existing and worth removing while adding a fourth
board, but that is a cleanup, not a requirement.

### PSRAM autodetect already returns the right pin

`board_config.h:60-68` reads `SYSINFO_PACKAGE_SEL` and returns
`PSRAM_CS_PIN_RP2350B` on a B-package part. Setting both
`PSRAM_CS_PIN_RP2350A` and `PSRAM_CS_PIN_RP2350B` to 47, as `board_z0.h:59-60`
does, is sufficient. `psram_init()` at `drivers/psram_init.c:82` then muxes
GPIO 47 to `XIP_CS1` and probes.

Unlike the M1, where the PSRAM chip select doubles as HDMI D0 minus (constraint
5 in `CLAUDE.md`), GPIO 47 on the Fruit Jam has no other use, so the
mux-and-leave-it behaviour on probe failure is safe here.

Fruit Jam PSRAM is 8 MB rather than the M2's optional module. Today PSRAM is used
only for FatFs long-filename work buffers via `drivers/fatfs/ffsystem.c:24`. The
extra capacity is an opportunity, not a requirement, and `CLAUDE.md` constraint 5
still applies: keep emulation hot paths out of it.

### I2S pin geometry fits the driver

`drivers/audio.c:100-102` sets the PIO function on `data_pin`,
`clock_pin_base` and `clock_pin_base + 1`, so it needs the bit clock and word
select on adjacent pins. Fruit Jam has BCLK on 26 and WS on 27. So
`I2S_DATA_PIN 24` and `I2S_CLOCK_PIN_BASE 26` are correct and the PIO program
needs no change. All three pins are below 32, so again no GPIO base juggling.

What the driver does *not* do is make the codec produce sound. See section 5.

### Inherited limitation: 16 scanlines are already being dropped

Applies to the PIO video path only. `HDMI_DRIVER=HSTX` shows all 256 lines;
see the note at the end of this section.

The default video path carries across to the Fruit Jam unchanged, and so does a
limitation that is not recorded in `CLAUDE.md` section 4 and is not
Fruit-Jam-specific. It matters here because it constrains what phase 3's HSTX
driver should target.

`frank_platform.c:384` calls `graphics_set_res(320, 256)`, and
`drivers/HDMI_audio.c:100`, inside `graphics_set_res()`, passes that height
straight into `frank_hdmi_set_buffer()`. That function clamps it:

```c
if (h > LOGICAL_H) h = LOGICAL_H;      /* frank_hdmi.c:163, LOGICAL_H = 240 */
...
fb_y_offset = (LOGICAL_H - h) / 2;     /* becomes 0 */
```

So rows 0 to 239 of the framebuffer are displayed and **rows 240 to 255 are
silently dropped**. The BBC's bottom 16 scanlines never reach the monitor.

This is not sloppiness. The mode is 640x480p60 with `v_active_lines = 480`
(`frank_dvi_timing.c:40`) and each logical scanline drives two raster lines
(`frank_hdmi.h:109-115`), so the logical canvas is 240 lines and 256 doubled
lines would need 512. It does not fit.

The fix already exists in the tree but is not wired up. The vendored timing
table includes `dvi_timing_720x576p_50hz` with `v_active_lines = 576`
(`frank_dvi_timing.c:92`), which doubles to a 288-line canvas and holds all 256
BBC lines with room to spare. The submodule README lists 50 Hz and 720x576 among
the modes "the vendored libdvi build can do" but that "this driver only wires
up 640x480p60". 50 Hz is also the rate the emulator already runs at:
`al_wait_for_event()` returns one timer event per frame
(`src/bem/pico/stub_allegro5/al_stub.cpp:784-787`).

**Resolved 2026-08-26 for the HSTX path.** `HDMI_DRIVER=HSTX` runs 720x576p50
and maps the 256 rows onto 512 of the 576 active lines, two raster lines each,
with a 32-line border top and bottom. Nothing is clipped. The PIO builds still
drop the 16 lines and still will.

Two consequences for this port. First, do not describe phase 1 as displaying the
full BBC screen, because it will not. Second, this constrains only the PIO video
path. An HSTX driver scales vertically by arbitrary ratio in its DMA command
list, so the clip does not arise there at any mode. See cost 2 under option B in
section 6. 720x576p50 is still the mode that matches the machine, because the
emulator runs at 50 Hz.

---

## 4. What has no equivalent on this board

### PS/2 keyboard and mouse

There is no PS/2 connector. `frank_platform.c:344` calls `ps2kbd_init()`
unconditionally, and `frank_keyboard.c:338-340` drains the PS/2 queue every
frame. Both need to become conditional on a board capability.

This is the first real use for the `HAS_*` capability macros. Note that they
are scattered and **referenced nowhere in the tree**: `board_m2.h` declares
`HAS_HSTX`, `HAS_TV`, `HAS_I2S`, `HAS_PWM` and `HAS_HDMI_AUDIO`; `board_m1.h`
declares `HAS_TV`, `HAS_I2S` and `HAS_PWM`; `board_z0.h` declares only
`HAS_I2S` and `HAS_PWM`; and `HAS_PS2_MOUSE` is derived in `board_config.h:71`,
not a board header. A grep across `src/`, `drivers/` and `CMakeLists.txt`
outside those definition sites returns nothing. They are documentation, not
working conditionals. Adding the Fruit Jam is the point at which they have to start
meaning something, or be replaced by something that does.

Wiring a PS/2 socket to the 2x16 header is possible and would keep the existing
driver in play, but it is a user modification, not a board feature. Do not make
the build depend on it.

### NES and SNES gamepad

No connector. `frank_platform.c:355` calls `frank_gamepad_init()`
unconditionally and `frank_keyboard.c:290-293` polls it every frame. Same
treatment as PS/2: gate on a capability macro.

Removing PS/2 and the NES pad frees the whole of PIO1, which section 6 spends.

### Composite TV

There is no video DAC on the DVI pins and no composite connector. The
`COMPOSITE` driver must be rejected at configure time for this platform, the
same way `CMakeLists.txt:44-46` already rejects it for the Z0.

That is also a memory saving worth noting: `CLAUDE.md` says to budget new
allocations against `COMPOSITE` at 75.0% of RAM because of the 76.8 KB
`tv_frame` buffer. With `COMPOSITE` unavailable on this platform, the tight
variant is `HDMI_PIO_AUDIO` at 63.8%.

### VGA

No VGA ribbon, so no runtime HDMI-versus-VGA detection. Follow the Z0 and force
`SELECT_VGA = false`, per the existing branch at `frank_platform.c:373-375`.

### PWM audio

There is no PWM audio output. The onboard speaker and headphone jack are both
downstream of the codec. `PWM_PIN0` and `PWM_PIN1` still have to be defined for
`drivers/pwm_audio/pwm_audio.c` and `frank_audio.c:71-74` to compile, but the
PWM backend should be removed from the F12 audio menu on this platform rather
than pointed at arbitrary header pins where it would emit square waves into a
user's breadboard.

Note the stale comment at `frank_audio.c:62-64`, which says I2S and PWM share
GPIO 10 and 11. Those pin numbers hold only on the M2 and Z0; on the M1 the
PWM pins are 26 and 27 (`board_m1.h:56-57`). The two backends do collide on
all three current boards, and do not on the Fruit Jam. If the PWM backend is
kept in any form, the pin re-mux logic in
`i2s_claim_pins()` and `pwm_claim_pins()` is unnecessary here, because the two
backends no longer collide.

---

## 5. The audio codec is new work

Sound on the Fruit Jam goes I2S into a TI TLV320DAC3100, then out to a 3.5 mm
stereo headphone jack and a mono speaker connector. The codec is an I2C slave at
7-bit address 0x18. It is silent until configured, and frank-micro has no I2C
code at all.

What the new driver has to do:

1. Leave the shared peripheral reset on GPIO 22 alone, or drive it high. Net
   `PERIPH_RST` reaches the codec's `!RESET` pin, the ESP32-C6's `EN` pin, the
   2x16 header, and a 10K pull-up to 3.3 V. So both parts come out of reset at
   power-on with no firmware action, and the hazard is asserting the line, not
   forgetting to release it. pico-mac drives it high explicitly, which is
   harmless.
2. Bring up I2C0 on GPIO 20 and 21. Note the same bus carries the STEMMA QT
   connector and the 2x16 header, so anything a user plugs in shares it.
3. Software-reset the codec, then wait. TI's datasheet gives roughly 1 ms for
   internal memory initialisation after reset, and a further roughly 10 ms
   start-up delay after the PLL is powered up before clocks are available.
4. Configure the PLL and the clock dividers for the sample rate in use.
5. Route the DAC to the headphone and speaker outputs and set output levels.
   Headphone detect is the codec's own `VOL/MICDET` pin on the jack's ring, not a
   GPIO, which is why the working sequence writes register 0x43 for it. The
   speaker amp runs from the 5 V rail, the analogue and headphone supplies from
   3.3 V, and the digital core from a dedicated 1.8 V regulator.

**One hazard the schematic exposes.** GPIO 23 is a three-way net. `I2S_ESP_IRQ`
joins it to the codec's `GPIO1` pin, to the ESP32-C6's `IO9/BOOT9` pin, and to
the `ESP_BOOT` header. The working register sequence configures the codec's
`GPIO1` as an output for interrupt duty, at register 0x33. On this board that
means the codec can drive the ESP32-C6's boot strap, and since the ESP32 is out
of reset by default, a low there at the wrong moment puts it into download mode.
Either leave the codec's `GPIO1` unconfigured, which frank-micro has no use for,
or hold the ESP32 in reset by driving GPIO 22 low and accept that this also
resets the codec. Do not copy that register write without deciding which.

### There is a working register sequence to copy

`pico-mac/src/main.c:580-745` is a complete bring-up of this codec on this
board, MIT licensed, in about 90 lines of register writes. It does all five steps
above and settles the question this document's first revision could not: the PLL
runs from BCLK. Register 0x04 is written so that PLL_CLKIN is BCLK and
CODEC_CLKIN is the PLL output, and GPIO 25, the MCLK pin, is never driven at all.
So open question 3 is no longer a design risk, only an arithmetic exercise.

Take from it: the reset and I2C sequence, the routing writes for the headphone
driver and the class-D speaker amp, and `set_mute_state()` with its 500 ms
automute. That last one mutes both output amps when the source has been silent,
which is the standard fix for idle hiss and for the click when a backend is
switched, and `CLAUDE.md` section 9 already lists backend switching as a rough
edge.

Do **not** take its PLL and divider constants. Both projects write P = 1,
R = 2, J = 32, NDAC = 8 and MDAC = 2, and neither writes registers 13 and 14, so
both leave DOSR at its reset value of 128. `fruitjam-doom` has the block copied
almost verbatim into `src/i_main.c:59-190`.

An earlier revision of this document said the identical constants could not be
right for both, since one project runs at 22,256 Hz and the other at 49,716 Hz.
That reasoning was wrong, and the reason is worth understanding before touching
this. Those values encode a **ratio**, not a frequency: NDAC times MDAC times
DOSR is 2048, and 32 times R times J over P is also 2048. Since the PLL runs from
the bit clock, and the bit clock is a fixed multiple of the sample rate, the whole
chain scales together and the divider chain comes out exact at any rate. That is
a real property and it is why one register block can appear in two projects.

What does not scale is the absolute limits, and that is where both projects fall
outside the datasheet:

| Project | Rate | PLL output | DAC_MOD_CLK | Verdict |
|---|---|---|---|---|
| pico-mac | 22,256 Hz | 45.580 MHz | 2.849 MHz | PLL is below the 80 MHz minimum |
| fruitjam-doom | 49,716 Hz | 101.818 MHz | 6.364 MHz | DAC_MOD_CLK is above the 6.2 MHz maximum |
| the same values at | 31,250 Hz | 64.000 MHz | 4.000 MHz | PLL is below the 80 MHz minimum |

Both evidently produce sound anyway. Neither is a configuration to copy, and at
frank-micro's own rate the PLL would be at 64 MHz against a specified minimum of
80.

One correction to the same earlier revision: it said pico-mac's I2S puts 64 bit
clocks in a stereo frame. It does not. `pico-extras`'s `audio_i2s.pio` is the
same program as `drivers/audio_i2s.pio` and its `update_pio_frequency()` uses
the same `system_clock_frequency * 4 / sample_freq` divider as
`drivers/audio.c:121-123`. So all three projects have 64 PIO cycles and
therefore **32 bit clocks per stereo frame**, and BCLK is 32 times the sample
rate everywhere.

### The sample rate decision

The SN76489 in b-em produces 31,250 Hz and `frank_audio.c:204-220` feeds the I2S
backend at that rate natively. Two options:

- **Configure the codec for 31,250 Hz.** This keeps `frank_audio.c` untouched.
- **Configure the codec for 32,000 Hz** and reuse the exact fixed-point
  resampler already written for the HDMI path at `frank_audio.c:235-253`.

Because the PLL is fed from BCLK, the whole clock chain is proportional to the
sample rate, so one set of register values can cover both. 1,348 sets do. The
one the datasheet's own ranking prefers, which is the largest NDAC the
`MDAC x DOSR / 32 >= RC` constraint allows:

| | Value |
|---|---|
| P, R, J | 1, 2, 52 |
| NDAC, MDAC, DOSR | 13, 2, 128 |
| PLL output | 104.000 MHz at 31,250 Hz, 106.496 MHz at 32,000 Hz |
| DAC_MOD_CLK | 4.000 MHz at 31,250 Hz, 4.096 MHz at 32,000 Hz |
| Register writes | `0x04=0x07`, `0x06=0x34`, `0x07=0x00`, `0x08=0x00`, `0x05=0x12` then `0x05=0x92`, `0x0B=0x8D`, `0x0C=0x82`, `0x0D=0x00`, `0x0E=0x80` |

Both rates come out exact. Start with 31,250 Hz and leave the resampler out of
the path.

**Do not hand-check that table, run the tool.** `tools/tlv320_clocks.py` holds
the arithmetic, with each constraint written next to the datasheet clause it
comes from, and it prints the working. It also audits the two Adafruit
configurations, which is where the table above the sample-rate heading comes
from, and it has a self-test that fails if the encoding quirks or the known-bad
cases stop behaving. Two things to know about the register writes, both from the
register tables in section 6.6 and both encoded in the tool: register 5 is
written twice, once to set P and R and again to power the PLL up, because the
multiplier has to be valid before the PLL starts; and register 14 must follow
register 13 immediately, because the DOSR value does not take effect until the
LSB write completes.

The constraints the tool encodes, all read from TI document SLAS671C:

| Constraint | Clause |
|---|---|
| PLL input 512 kHz to 20 MHz | Section 6.3.11.1 |
| `80 MHz <= PLL_CLKIN x J.D x R / P <= 110 MHz` | Equations 7 and 8 |
| `4 <= R x J <= 259` when D is zero, and R must be 1 when it is not | Equation 7, equation 8 |
| `CODEC_CLKIN = NDAC x MDAC x DOSR x DAC_fS` | Section 6.3.10.14 |
| `MDAC x DOSR / 32 >= RC`, and make NDAC as large as that allows | Section 6.3.10.14 |
| `2.8 MHz < DOSR x DAC_fS < 6.2 MHz` | Section 6.3.10.14 step 1 |
| DOSR a multiple of 8, 4 or 2 for filter A, B or C | Section 6.3.10.14 step 1 |
| `CODEC_CLKIN <= 110 MHz`, `DAC_CLK <= 49.152 MHz`, `DAC_MOD_CLK <= 6.758 MHz`, `DAC_fS <= 192 kHz` | Table 6-27, the DVDD at or above 1.65 V column, which is the applicable one since the Fruit Jam runs the codec's DVDD at 1.8 V |
| R 1 to 16, J 1 to 63, D 0 to 9999, P 1 to 8, NDAC and MDAC 1 to 128, DOSR 1 to 1024 | Section 6.3.11.1 and the register tables |
| Resource class 8 and interpolation filter A, since register 60 resets to PRB_P1 | Table 6-11, table for register 60 |

What is still unverified is only whether the PLL locks in practice. The
arithmetic is now primary.

Scope: one new file in `drivers/` alongside the other platform drivers, plus
`hardware_i2c` added to the link line. Call it 150 lines rather than the 400 the
first revision estimated, since the sequence is being adapted rather than
derived. It does not belong in `src/frank/`, which is application glue, and
certainly not in `src/bem/`.

---

## 6. The clock conflict

This is the decision that shapes everything else.

### Why the board is wired this way

Worth understanding before choosing between the options, because it explains why
the conflict exists at all and why it is frank-micro's rather than the board's.

The RP2350 has a single USB 2.0 full-speed controller. It is dual-role, so it can
be host or device, but not both at once, and its D+ and D- go to one connector.
A board therefore gets one native USB port and must decide what that port is for.

Adafruit had to spend theirs on device mode. The USB-C port is how the board is
flashed by drag-and-drop UF2 and how CircuitPython exposes its `CIRCUITPY` drive
and REPL. Making it a host port would leave SWD as the only way to reprogram the
board. But the Fruit Jam is pitched as a small desktop computer, DVI out and
keyboard in, so it needs a host port at the same time as the device port. Once
the native controller is committed to device mode, PIO is the only remaining way
to get one, which is why the type-A side goes to Pico-PIO-USB.

Two type-A sockets from one PIO-USB root port then need an external hub, because
extra root ports cost more state machines and instruction memory. See open
question 4: hub support is historically the weaker part of the TinyUSB plus
PIO-USB host path, so a keyboard enumerating *behind the hub* is phase 1b's
whole deliverable rather than a detail.

Nothing is lost in speed. PIO-USB is full speed only, 12 Mbps, which is the same
rate the native controller offers for full-speed devices, and HID keyboards and
gamepads are full speed.

The cost Adafruit accepted is the system clock requirement below, and on their
software stack it costs nothing, because they drive DVI from HSTX and `clk_hstx`
is independent of `clk_sys`. frank-micro drives DVI from PIO, and that is the
whole of the conflict.

### The two constraints

**PIO DVI needs 252 MHz.** The default `HDMI_PIO_AUDIO` path uses the
`frank-hdmi-sound` submodule, a libdvi and PicoDVI derivative. Its README states
the design point: the TMDS bit clock must be 252 MHz, which is ten times the
25.2 MHz pixel clock for 640x480p60. `CMakeLists.txt:136-141` sets
`FRANK_HDMI_SM_CLKDIV` to 1 at or below 252 MHz and 2 above, so the supported
system clocks are 252 MHz and 504 MHz. `CLAUDE.md` constraint 3 says not to
change this, and `drivers/HDMI.c:637-638` calibrates against 252 MHz as well.

**PIO-USB's clock requirement is softer than the first revision said.** That
revision quoted Pico-PIO-USB's Arduino example:

```c
uint32_t cpu_hz = clock_get_hz(clk_sys);
if ( cpu_hz != 120000000UL && cpu_hz != 240000000UL ) {
  ...
  Serial.printf("Error: CPU Clock = %u, PIO USB require CPU clock must be multiple of 120 Mhz\r\n", cpu_hz);
```

The library itself contains no such check, and its C examples state the
requirement differently, at `example/usb_device/usb_device.c:125`:

```c
// default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
set_sys_clock_khz(120000, true);
```

The C comment is the one the code supports. `pio_usb.c:1399-1411` computes four
dividers from `clk_sys` at run time, one per program: full-speed transmit at
`clk_sys / 48 MHz`, full-speed receive at `clk_sys / 96 MHz`, and the low-speed
equivalents at 6 and 12 MHz. Nothing rejects a system clock. The divider is
whatever comes out, rounded into the PIO's 16.8 fixed-point register.

So the question is not whether `clk_sys` is a multiple of 120 MHz. It is whether
`clk_sys / 96 MHz` lands on an exact multiple of one 256th. Working the
candidates through both full-speed dividers:

| `clk_sys` | fs transmit, `/48 MHz` | fs receive, `/96 MHz` | Exact in 16.8 |
|---|---|---|---|
| 120 MHz, sanctioned | 2.5 | 1.25 | yes |
| 240 MHz, sanctioned | 5.0 | 2.5 | yes |
| 132 MHz, pico-mac default | 2.75 | 1.375 | yes |
| 264 MHz, pico-mac overclocked | 5.5 | 2.75 | yes |
| **252 MHz, frank-micro today** | **5.25** | **2.625** | **yes** |

252 MHz is 21 times 12 MHz and both its full-speed dividers are exactly
representable. On that measure it is no worse than the two values the Arduino
example sanctions.

That does not make option C safe, but it changes what the risk is. Every
candidate except 240 MHz transmits on a fractional divider, so the state machine
stalls on an uneven cadence and bit edges move by up to one system-clock period.
That period is 8.3 ns at 120 MHz, 7.6 ns at pico-mac's shipping default of
132 MHz, and 4.0 ns at 252 MHz. The configuration frank-micro already runs would
have less divider jitter than one Adafruit ship and that works. Note also that
none of this is bit-banging: the PIO state machines handle line signalling in
hardware and the CPU does packet-level work above them off a 1 ms repeating
timer.

Three ways out, and the order to try them in has changed since the first
revision.

### Option A is ruled out: the USB-C port cannot be a host

Kept here because the first three revisions of this document were built on it,
and because ruling it out is the most consequential thing the schematic settled.

The plan was to keep 252 MHz and the existing PIO DVI driver, ignore the onboard
USB-A ports, and plug a USB-C-to-USB-A host adapter into the USB-C port so that
the existing native-controller `drivers/usbhid` stack could own it, exactly as on
the Murmulator. That needed two things from the board: CC pins in a state that
permits host mode, and something able to source 5 V on that connector. It has
neither.

- **CC termination is sink-only.** `R19` and `R20` are 5.1K from `CC2` and `CC1`
  to ground. That is Rd on both pins, which advertises a device. There is nothing
  switchable about it, so the port cannot present Rp and an adapter cannot make it.
- **VBUS on that connector is an input.** Net `VUSB` runs from `X3` pin `VBUS`
  through `Q3`, a DMP3098L P-channel MOSFET whose gate is held by the mechanical
  power switch `SW2`, into the board's 5 V rail. Firmware has no say in it.
- **The only firmware-controlled 5 V switch feeds the other side.** GPIO 11,
  net `USBH_PWR`, drives `Q1` which pulls the gate of `T1`, another P-channel
  MOSFET, whose drain reaches the `VBUS` net through `PTC1`, a 1 A polyfuse. That
  `VBUS` net supplies `X2` and `X5`, the two USB-A sockets, and `X6` pin `V5`,
  the hub. It never reaches the USB-C connector.

A P-channel MOSFET conducts in both directions once it is on, so if the board
were powered from the 5 V pins of the 2x16 header with the switch closed, `Q3`
would back-feed roughly 5 V onto the USB-C connector's VBUS, and a keyboard does
not care about CC. That is off-specification, depends on a switch position, and
back-powers a connector designed as an input. It is not a thing to build phase 1
on.

So PIO-USB is not one option among three. It is the only input path the board
has, and the clock question below has to be answered in phase 1.

### Option B: HSTX video, so the clocks decouple

The RP2350's HSTX peripheral has its own clock domain, and `clk_hstx` can be
sourced independently of the system clock. Verified from the register header:

```
CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS       0x0
CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS 0x1
CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB 0x2
CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_GPIN0   0x3
CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_GPIN1   0x4
```

`adafruit/pico-mac` does exactly this on this board, and its arrangement is
better than the one this document proposed in its first revision. That revision
kept the system on `pll_sys` and took the pixel clock from `pll_usb`. pico-mac
does the reverse: `pll_usb` is pinned at 528 MHz and feeds everything else with
integer dividers, and `pll_sys` is left free to be reprogrammed for whatever
pixel clock the video mode needs.

From `pico-mac/src/clocking.c:100-160`:

| Clock | Frequency | Source |
|---|---|---|
| `clk_sys` | 264, 176 or 132 MHz | `pll_usb` divided by 2, 3 or 4 |
| `clk_peri` | 132 MHz | `pll_usb` divided by 4 |
| `clk_usb` | 48 MHz | `pll_usb` divided by 11 |
| `clk_adc` | 48 MHz | `pll_usb` divided by 11 |
| `clk_hstx` | whatever the mode needs | `pll_sys`, reprogrammed per mode |

528 is the number that makes this work. 528 over 11 is exactly 48 for USB, and
528 over 2 and over 4 give 264 and 132, both exact multiples of 12 MHz for
PIO-USB. `pll_usb` runs at VCO 1584 MHz with post-dividers 3 and 1, and 1584 is
inside the 750 to 1600 MHz range the SDK asserts at
`hardware_pll/include/hardware/pll.h:40-48`.

`hstx_clock_hz()` at `clocking.c:190-214` then brute-forces `pll_sys` for the
requested bit clock: it sweeps fbdiv from 320 down to 16 and both post-dividers,
rejects any combination that does not divide exactly, and keeps the closest
result. That function is worth taking verbatim. It means a video mode can be
chosen without hand-deriving a PLL setting for it, and changing the mode later
cannot disturb the system clock.

For frank-micro the plan becomes:

| Clock | Frequency | Source |
|---|---|---|
| `clk_sys` | 264 MHz | `pll_usb` 528 divided by 2 |
| `clk_hstx` | 135 MHz for 720x576p50, or 126 MHz for 640x480p60 | `pll_sys`, chosen by the search |
| `clk_usb` | 48 MHz | `pll_usb` 528 divided by 11 |

264 MHz is 12 MHz above what the emulator runs at today, so the frame pacer
gains margin instead of losing the 5% the first revision's 240 MHz plan would
have cost. If more is wanted, `pll_usb` at 576 MHz gives `clk_sys` 288 MHz with
576 over 12 for USB and 288 over 12 for PIO-USB, all still integer. Above
300 MHz the voltage and flash-timing block at `frank_platform.c:308-313` starts
to matter; note that pico-mac runs 264 MHz at `VREG_VOLTAGE_1_20`, well below
the 1.50 V `CMakeLists.txt:68-74` would select.

#### How HSTX is fed, since it is not PIO

HSTX is a peripheral with its own FIFO, so the data path is DMA to FIFO to pins
with no CPU and no PIO in it. Verified from the SDK headers:

- `hstx_fifo_hw->fifo` is a 32-bit write-only FIFO port
  (`rp2350/hardware_structs/.../hstx_fifo.h:38`), and `DREQ_HSTX` is 52
  (`rp2350/hardware_regs/.../dreq.h:68`). A DMA channel writing to that address
  with that DREQ paces itself off FIFO space and streams a line out unattended.
- Behind the FIFO is a shift register. `csr.SHIFT` and `csr.N_SHIFTS`
  (`hstx_ctrl.h:36`) set how many bits rotate out per cycle and how many cycles
  each FIFO word lasts.
- `bit[0..7]` (`hstx_ctrl.h:45`), one register per output lane, selects which
  shift-register bit that lane takes. Two selects per lane, `SEL_P` and `SEL_N`,
  because the output is DDR. `INV` inverts a lane, which is how the negative
  half of each TMDS pair is produced, and `CLK` makes a lane emit the generated
  clock instead of data, divided by `csr.CLKDIV`. So the TMDS clock pair costs
  no encoding work at all.
- The command expander, `csr.EXPAND_EN`, is what makes DVI practical. Its
  `expand_tmds` register (`hstx_ctrl.h:63`) holds a per-lane rotate and bit count,
  so **the hardware performs the TMDS 8b/10b encoding**. Raw pixel words go in
  and TMDS symbols come out. `expand_shift` (`:53`) additionally provides a raw
  path for pre-encoded symbols, which is how sync and blanking periods are sent.

The usual shape, and what the pico-examples DVI encoder does, is two chained DMA
channels: one streaming the active pixel run, one replaying a small command list
for front porch, sync and back porch. Neither core is in the loop.

> **Built 2026-08-26, and not the way this section expected.** Option B was
> implemented as a fourth `HDMI_DRIVER`, not because option C failed but for
> the two picture faults option C carries. Two premises below turned out to be
> wrong. Neither `pico-mac` nor `Framebuffer_RP2350.c` is on the build machine,
> so neither could be vendored; what was vendored instead is
> `fhoedemakers/pico_shared`'s `drivers/pico_hdmi`, GPL-3.0, from the copy
> inside `fruitjam-doom`. And cost 3 below is wrong: that driver implements
> HDMI data islands, so the `HSTX` build keeps HDMI-embedded audio. The
> clock plan also changed; see `docs/HANDOFF.md`. The rest of this section is
> the reasoning that led there and is left as written.

#### Cost 1: the driver has to be written, but not from scratch

`drivers/HDMI_vga_hstx.c` is not a starting point. That file targets VGA through
the DispHSTX library, which is **not vendored anywhere in this repository**, so
its `#include "disphstx.h"` cannot resolve and the file would not compile if
added. It is written for a 360x240 CPC framebuffer, carries frank-cpc headers,
and is in no source list. `CLAUDE.md` notes it would collide with `HDMI_vga.c`.

`pico-mac/src/video_hstx.c` is. It is 325 lines, MIT licensed, derived from the
`pico-examples-rp2350` HSTX encoder, and it runs on this board. Its shape:

- Three small command-list arrays in SRAM: a blanking line with vsync asserted,
  one without, and an active line. Each holds `HSTX_CMD_RAW_REPEAT` words and
  pre-encoded sync symbols, patched at init with the porch and sync widths of
  the chosen mode.
- One DMA command list built per frame, two words per entry, a transfer count
  and a read address. A blanking line takes one entry naming a command array; an
  active line takes two, one for the sync run and one naming the framebuffer row.
- A command channel configured with `channel_config_set_ring` so that it writes
  into the pixel channel's `al3_transfer_count` and `read_addr` and wraps every
  two words.
- One interrupt per **frame**, not per line, which only re-triggers the command
  channel at the head of the list.

Vertical doubling is free in that structure, because two entries can name the
same row address. A 256-line framebuffer on a 576-line raster costs nothing
extra.

What to change when vendoring it. Drop `gtf.c`: it computes VESA GTF timings,
which are not the CEA-861 timings a television expects, and the
`frank-hdmi-sound` submodule already carries a hand-tabulated table including
`dvi_timing_720x576p_50hz` (`frank_dvi_timing.c:81`). Drive the driver from that
table instead, and keep `clocking.c`'s PLL search.

`adafruit/fruitjam-doom`'s `Framebuffer_RP2350.c` is the same driver from a
different branch of the same lineage, 534 lines, and it is the better base of the
two. It keeps hardcoded standard timings rather than GTF, supports 4, 8, 16 and
32 bits per pixel, and does horizontal and vertical scaling in the DMA command
list. Cost 2 below is mostly about that file. Take its framebuffer and scaling
logic and pico-mac's clocking, and do not take Doom's clocking: it derives
`clk_hstx` from `clk_sys` with a fractional divider and lands on 125.875 MHz
instead of 126, which its own comments describe as nonsense
(`Framebuffer_RP2350.c:201-205`).

#### Cost 2: HSTX has no palette lookup

The first revision said the pass has to produce RGB565. The mechanism was right
and the format was wrong, and the difference changes the design.

The expander's semantics, read from
`pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/hstx_ctrl.h`: each of
the three lanes applies **its own** right-rotate to the current shifter value,
then takes `NBITS+1` bits starting at bit 7 of the rotated value, masking the
remaining low bits to zero. The rotates are independent, so the three lanes can
read three arbitrary bit positions of the same pixel. It is a per-lane bitfield
extractor, not a fixed RGB565 layout.

That has a consequence specific to this emulator. `rgb555_to_bbc()` at
`src/frank/frank_gui.c:59-64` already returns a 3-bit value, bit 0 red, bit 1
green, bit 2 blue, because the BBC has only eight physical colours. A 4bpp
framebuffer holding those values would feed HSTX with **no conversion pass at
all**: `csr.SHIFT` 4, `csr.N_SHIFTS` 8, `NBITS` 0 on every lane, and rotates of
25, 26 and 27 to bring bits 0, 1 and 2 up to bit 7.

The catch is intensity. One valid bit produces 0x80, not 0xFF, so every colour
comes out at half level and white is #808080. pico-mac's monochrome
configuration has exactly this property. No arrangement of rotates fixes it,
because the extra bits a wider window would pick up belong to the neighbouring
pixel.

So the answer is RGB332 at 8bpp rather than RGB565 at 16bpp. And on the evidence
of the second Fruit Jam port there is no per-line CPU pass either.

#### The Doom port has solved this, and the DMA does the scaling

`adafruit/fruitjam-doom` carries `Framebuffer_RP2350.c`, 534 lines, MIT licensed,
Scott Shawcroft for Adafruit. It is CircuitPython's picodvi framebuffer
generalised to colour depths of 4, 8, 16 and 32 bits, and Doom's own framebuffer
is 8bpp, which is the same problem frank-micro has.

Three things in it matter here.

**RGB332 is a supported configuration, with the lane values written out.**
`Framebuffer_RP2350.c:397-404`: lane 2 `NBITS` 2 `ROT` 0, lane 1 `NBITS` 2 `ROT`
29, lane 0 `NBITS` 1 `ROT` 26. Applying the rotate-then-take-the-top-bits rule,
that reads bits 7:5, 4:2 and 1:0 of the pixel byte, so the byte layout is
`RRRGGGBB` and the eight BBC colours become 0x00, 0xE0, 0x1C, 0xFC, 0x03, 0xE3,
0x1F and 0xFF. White is full scale, a primary is 224 of 255.

The same file also confirms the 4bpp path and its cost. `:406-413` configures
what it calls RGBD with `NBITS` 0 on all three lanes and rotates 28, 27 and 26,
which is the eight-colour mode described above, at half intensity for the reason
given above.

**Horizontal doubling is done by the DMA, not by a CPU pass.** `:300-311` sets
the pixel channel's transfer size to `DMA_SIZE_8` when scaling, with the comment
"the memory bus will duplicate the bytes read to produce 32 bits for the HSTX".
`:356-364` then issues one transfer per source pixel rather than one per four,
and `:426-441` sets `expand_shift`'s `ENC_N_SHIFTS` to the pixel count times the
scaling factor, so each replicated word is shifted twice and each source pixel
emerges as two output pixels. Cost to the CPU: nothing.

That replication is documented behaviour, not a happy accident, and the
datasheet states it three times. Section 2.1.5, "Narrow IO register writes", page
27: memory-mapped IO registers "ignore the width of bus read/write accesses" and
treat every write as 32 bits, and "upon a 8-bit or 16-bit write ... the narrow
value is replicated multiple times across the 32-bit data bus, so that it is
broadcast to all 8-bit or 16-bit segments of the destination register". Section
12.6, the DMA chapter introduction: "The DMA performs byte lane replication on
narrow writes, so byte data is available in all 4 bytes of the databus, and
halfword data in both halfwords." And section 11.6, in the WS2812 PIO example:
"each byte the DMA transfers will appear replicated four times when written to a
32-bit IO register".

Two details that follow from the same section and matter when writing the driver.
Framebuffer alignment is irrelevant: the read side picks the correct byte lane
from the transfer size and the address LSBs, and the write side replicates
whatever it read, so a row may start at any byte offset. And the behaviour can be
switched off, which is a hazard rather than a feature here: setting address bit
14, by writing to the peripheral at an offset of +0x4000, drives the invalid byte
lanes to zero instead. Do not use an aliased FIFO address in this driver. The SDK
defines no constant for that alias, so it can only be reached deliberately.

One inference remains. The datasheet says "the majority of" IO registers ignore
access width, and does not enumerate them, so that the HSTX FIFO is one of them
rests on the general rule plus the fact that the Doom port works. There is an
official example of the same mechanism at
`pico-examples/system/narrow_io_write/narrow_io_write.c`.

The price is in the command list. When scaling, each line's entry grows from two
words to four, because the control register and the write address have to be
rewritten per line to switch between 32-bit sync words and 8-bit pixel
transfers (`:235-238`, `:332-335`). And the active line costs 320 byte-sized DMA
transfers where an unscaled 8bpp line would cost 160 word-sized ones. Both
projects set `bus_ctrl_hw->priority` to favour DMA.

**Vertical scaling is free and arbitrary.** `:362` is `row = row * 200 / 480`.
The row index for each raster line is computed once, at init, while the command
list is built. Any ratio works, not just integer doubling.

That last point matters beyond this section. The 16-line clip described at the
end of section 3 exists because 256 doubled lines need 512 raster lines and
640x480p60 has 480. Under HSTX the clip simply does not arise: 256 source rows
map onto 480 raster lines by `row * 256 / 480`, and 720x576p50 stops being
necessary to show the whole screen. It is still the better mode, because 50 Hz
matches the emulator and 576 over 256 is closer to a clean ratio, but it is no
longer the only way.

#### What this means for the port

- The framebuffer stays 320x256 and 8bpp, `frank_gui.c:30`, and 160 KB
  double-buffered as today. It holds RGB332 bytes instead of palette indices 0
  to 7.
- No line buffer, no per-line expansion pass, and one interrupt per frame rather
  than per line. The "small per-line job" earlier revisions predicted does not
  exist, which is why the comparison table below now shows HSTX costing a core
  nothing.
- The change on the frank-micro side is confined to the places that write a
  colour byte: `rgb555_to_bbc()` at `frank_gui.c:59-64` returns the RGB332 code,
  the teletext blit at `:94` follows, the overlay colour constants in
  `frank_ui.c` and `ui_draw.c` become RGB332 values, and
  `frank_screenshot.c`'s BMP palette is written from the same eight values. That
  is smaller and less risky than a per-line expander in the video path, which
  `CLAUDE.md` constraint 7 warns against adding to anyway.
- The follow-on that would fix the MODE 0 and MODE 3 horizontal loss recorded in
  `CLAUDE.md` section 4 is still available: widen to 640 pixels at 4bpp, 80 KB
  per buffer and so 160 KB double-buffered, exactly what two 320x256 8bpp buffers
  cost today. It trades that fix against half-intensity colour, so it is a
  judgement call rather than an improvement. Not part of phase 3.

This answers open question 8. `csr.N_SHIFTS` alone cannot double a pixel, because
the data-repeat behaviour the field description mentions repeats the whole word,
giving p0 p1 p2 p3 p0 p1 p2 p3. `N_SHIFTS` combined with a one-pixel-per-word
DMA transfer can, and that is what the Doom port does.

#### Cost 3: HSTX cannot generate HDMI data islands as it stands

> **Wrong, established 2026-08-26.** The argument below is sound about
> `expand_tmds`, and its own suggestion is what works: pre-encoded TERC4
> symbols go out through the raw shift path with the packet timing interleaved
> into the command list. `fhoedemakers/pico_shared`'s `pico_hdmi` does exactly
> that, in `hstx_packet.c` and `hstx_data_island_queue.c`, and it is now
> vendored as `drivers/hstx/`. The `HSTX` build carries HDMI-embedded audio and
> the `FRANK_AUDIO_HDMI` backend stays in the F12 menu. "Nobody has done it
> here" was true when written; somebody had done it elsewhere.

HDMI-embedded audio is a property of the `frank-hdmi-sound` PIO driver, not of
HSTX, and the reason is specific rather than incidental.

Audio reaches an HDMI sink inside data islands, small packets inserted into the
horizontal blanking interval. `frank-hdmi-sound/src/frank_data_packet.h:23-26`
gives the budget in TMDS clock periods: `W_PREAMBLE` 8, `W_GUARDBAND` 2 each
side, `W_DATA_PACKET` 32, so `W_DATA_ISLAND` is 36 (`:37`), one island every
`N_LINE_PER_DATA` 2 scanlines. Inside an island the encoding changes from TMDS
8b/10b to TERC4, 4 bits in to 10 bits out per channel per period.

HSTX's `expand_tmds` does 8b/10b only. There is no TERC4 mode, so the expander
cannot generate islands the way it generates video. That is not necessarily
fatal: `expand_shift`'s raw path shifts out pre-encoded symbols, and
`frank_data_packet.c` already builds its islands in software, so precomputed
TERC4 symbols could in principle go out that way with the packet timing
interleaved into the DMA command list. Nobody has done it here and it is
**unverified**, so treat it as plausible work, not a known-good approach.

On this board the point is largely academic, because the TLV320DAC3100 drives a
real speaker and a headphone jack and phase 2 already targets it. The practical
consequence is only that the `FRANK_AUDIO_HDMI` backend disappears from the F12
menu. It would matter much more to anyone contemplating moving the M2 to HSTX,
where HDMI audio is the default sound path today.

#### What each path costs, side by side

The three approaches trade the same two jobs, TMDS encoding and palette
expansion, in opposite directions:

| Path | TMDS encoding | Palette to colour | Core 1 |
|---|---|---|---|
| `HDMI_PIO`, `drivers/HDMI.c` | precomputed table, once per palette change | PIO emits addresses, DMA dereferences them | free |
| `HDMI_PIO_AUDIO`, the submodule | CPU, hand-written asm, per scanline | folded into the pre-encoded palette | fully occupied |
| HSTX | hardware, `expand_tmds` | nothing, if the framebuffer holds RGB332 rather than indices | free |

See `CLAUDE.md` section 6 for how the two existing PIO drivers differ, which is
more than the names suggest.

The compensation is significant. HSTX frees the entire PIO0 block. With PS/2 and
the NES pad gone, PIO1 is free too. The RP2350 has three PIO blocks, verified as
`NUM_PIOS 3` in `pico-sdk/src/rp2350/hardware_regs/include/hardware/platform_defs.h`,
and PIO2 is untouched by this codebase. So the allocation becomes comfortable
rather than exactly full:

| Block | Claimant | State machines |
|---|---|---|
| PIO0 | free | 0 of 4 |
| PIO1 | PIO-USB | 3 of 4, all 32 instruction slots |
| PIO2 | I2S | 1 of 4 |

Compare that with `CLAUDE.md` section 6, where PIO1 is exactly full on every
current board with one state machine wasted on a nonexistent mouse. PIO-USB
needs a whole block to itself because it uses all 32 instruction slots, so it
could not share with I2S even though it leaves a state machine spare. Having
three blocks is what makes this work.

### Option C: PIO DVI at 252 MHz, with PIO-USB on the same clock

Keep 252 MHz, keep the existing PIO DVI driver and its HDMI-embedded audio, and
run PIO-USB alongside it. With option A ruled out this is no longer a shortcut to
try before committing to phase 3. It is the configuration phase 1 has to prove.

The first revision dismissed this as a fractional-divider gamble. On the evidence
in "The two constraints" above, that was too strong. 252 MHz is 21 times 12 MHz,
both full-speed dividers land on exact 16.8 values, and its 4.0 ns jitter quantum
is smaller than the 7.6 ns of the 132 MHz configuration Adafruit ship and that
demonstrably enumerates keyboards. The one thing 252 MHz lacks, and 240 MHz has,
is a whole-integer transmit divider.

If it works it is by far the cheapest phase 3: vendor Pico-PIO-USB, move I2S to
PIO2, write no video driver at all, and keep HDMI-embedded audio. PIO0 keeps
DVI, PIO1 takes PIO-USB, PIO2 takes I2S. Note that the fork pico-mac uses cannot
address PIO2 itself: `pio_usb.c:1339` and `:1342` pick `pio0` or `pio1` from a
one-bit test, so PIO-USB has to be the one on PIO1 and I2S has to be the one
that moves. That makes the `drivers/audio.c:99` funcsel bug a blocker, not a
latent one.

It remains the least certain thing in the port, because divider arithmetic does
not prove that a hub and a keyboard behind it will enumerate reliably for hours.
The mitigation is sequencing rather than analysis: get video and SD working with
no input at all first, so that when the USB work starts there is only one unproven
thing in the build.

### Recommendation

Option C, and there is no longer a choice about it in phase 1, only about how to
sequence the work. Option A does not exist and option B is a fallback.

Split phase 1 in two so that only one unproven thing is in the build at a time.
Phase 1a builds with `USB_HID_ENABLED` off, which puts the USB CDC console on the
USB-C port and gives video, SD and a console with no input. That proves the
board, the DVI wiring, the clock and the SD path against something you can read.
Phase 1b then adds PIO-USB at the same 252 MHz.

If 1b will not hold, go to option B. That is now largely a vendoring job:
`fruitjam-doom` supplies the framebuffer driver and the scaling, `pico-mac`
supplies the clock plan and the PLL search, and cost 2 above supplies the
framebuffer format change. Do not attempt it at the same time as the initial
bring-up: an HSTX driver and a new board are each capable of producing a black
screen.

Note the consequence for section 7's advice that `USB_HID_ENABLED` must never be
off on this platform. That is right for a release build, and wrong for phase 1a,
which depends on it being off. Make it a warning rather than a configure error.

Two things to keep in view while sequencing this. Option B is the configuration
the hardware was designed for, HSTX video alongside PIO-USB, so it has a known
working end state on this board even though the driver has to be written. Option
A is the deviation, and its one open dependency, host mode on the USB-C port, is
the least certain thing in phase 1. If that dependency fails, phase 1 has no
input path at all and option B stops being deferrable, so check it before
committing to the phasing rather than at the end of phase 1.

---

## 7. File-by-file change list

### New files

| File | Purpose |
|---|---|
| `src/board_fj.h` | Fruit Jam pin and capability definitions |
| `drivers/tlv320dac3100.c`, `.h` | I2C codec bring-up. Phase 2 |
| `drivers/HDMI_hstx.c` | HSTX DVI driver, adapted from `fruitjam-doom/Framebuffer_RP2350.c`. Phase 3, and only if option C fails |
| `drivers/hstx_clocking.c`, `.h` | `pll_usb` at 528 MHz, plus the `pll_sys` search for a pixel clock, from `pico-mac/src/clocking.c`. Same condition |

### Changed files

**`CMakeLists.txt`**

- `:26-30` add `fj` to the `PLATFORM` cache variable, its `STRINGS` property and
  the validation guard.
- `:44-46` extend the `COMPOSITE` rejection to cover `fj`.
- `:56-67` add an `fj` branch setting `PICO_BOARD "adafruit_fruit_jam"`. Note this
  needs no `PICO_BOARD_HEADER_DIRS` override, unlike the Z0 at `:57`, because the
  SDK already ships the header. The Z0 branch also forces the clock down when
  `CPU_SPEED` exceeds 300; decide the equivalent policy for `fj` from section 6.
- `:76-100` add an `fj` branch setting `PLATFORM_DEF`, `BOARD_DEF` and the four
  `SD_*` variables to 39, 34, 35, 36.
- `:134` `target_compile_definitions(ps2_driver ...)` should not be reached at all
  when PS/2 is absent. Either skip `add_subdirectory(drivers/ps2)` or make the
  driver a no-op interface library, matching how `drivers/usbhid` already handles
  being disabled.
- `:131` add `drivers/nespad` to the same treatment.
- There is no single `drivers` link line: each video variant links its own list,
  at `:175-188` for `HDMI_PIO_AUDIO`, `:248-262` for `COMPOSITE` and `:289-301`
  for `HDMI_PIO`. Add `hardware_i2c` to every variant the Fruit Jam can build,
  for phase 2.
- `:517-520` the Z0 SPI-bus override block. No `fj` equivalent needed, since
  SPI0 is the default; `hardware_spi` is already on all three driver link lines.

**`src/board_config.h`**

- `:17-25` add the `PLATFORM_FJ` branch to the dispatcher.
- `:70-75` the `PS2_MOUSE_CLK` fallback aliases the mouse to the keyboard pin
  when no mouse exists. On a board with no PS/2 at all, that fallback is wrong
  in a new way, and it is the same waste `CLAUDE.md` flags as a nonexistent
  mouse consuming a PIO1 state machine. Guard the whole PS/2 section on a
  capability macro instead.

**`src/frank/frank_platform.c`**

- `:344` gate `ps2kbd_init()` on the PS/2 capability.
- `:355` gate `frank_gamepad_init()` on the gamepad capability.
- `:358-381` add the `fj` case to video selection. Force `SELECT_VGA = false`
  as the Z0 does at `:373-375`.
- `:308-313` the voltage and flash-timing block is gated on
  `CPU_CLOCK_MHZ > 252`, so at the default clock neither runs. If the Fruit Jam
  ends up at 240 MHz under option B, that gate still skips them, which is
  correct for 1.50 V. Re-check if the clock ever goes above 300.
- New, phase 2: initialise the codec, before any audio call. GPIO 22 needs no
  action, since `PERIPH_RST` has a 10K pull-up and both the codec and the
  ESP32-C6 leave reset on their own. See the hazard note in section 5 about GPIO
  23 before configuring the codec's `GPIO1`.
- Phase 1a: pull up GPIO 33 for card detect, and GPIO 4 and 5 for the two
  buttons. All three are bare switch or socket contacts to ground with no
  external resistor.
- Opportunity, not a requirement: GPIO 33 is a card-detect line. `boot_halt_if_required()`
  at `:301-303` currently shows an error screen that reboots on the watchdog
  instead of halting, which is finding 4 in `docs/INVESTIGATION.md`. Card detect
  makes a clean "insert a card" state possible on this board.

**`src/frank/frank_keyboard.c`**

- `:290-293` gate the NES pad poll.
- `:338-340` gate the PS/2 drain.
- With both gone, `usbhid_wrapper_tick()` at `:335` is the only input source, so
  a release build with `USB_HID_ENABLED` off has no input at all. Make that a
  configure-time **warning**, not an error: phase 1a depends on building exactly
  that way, to get a console on the USB-C port before PIO-USB exists.

**`src/frank/frank_audio.c`**

- `:64-74` the pin re-mux helpers assume I2S and PWM share pins. Not true here.
- `:123` `frank_audio_set_driver()` needs the PWM backend removed from the
  selectable set on this platform, and `FRANK_AUDIO_HDMI` too under option B.
- `:204-220` unchanged if the codec is configured for 31,250 Hz.

**`src/frank/frank_ui.c`**

- The F12 audio backend list must reflect what the board actually has. There is
  no new backend on this board: the codec is an I2S sink, so it is
  `FRANK_AUDIO_I2S` that drives it, once the codec driver has made it audible.
  So the list is `FRANK_AUDIO_HDMI` plus `FRANK_AUDIO_I2S` on the PIO video path,
  and `FRANK_AUDIO_I2S` alone if phase 3 ever moves video to HSTX.
  `FRANK_AUDIO_PWM` has no output on this board and comes out of the list either
  way.

**`drivers/audio.c`**

- `:84` hardcodes `pio1`. Under option B, I2S moves to PIO2.
- `:99` `func = (config->pio == pio0) ? GPIO_FUNC_PIO0 : GPIO_FUNC_PIO1` returns
  the **wrong** function for `pio2`. This is a latent bug today and becomes a
  live one the moment anything moves to PIO2. Fix it to a three-way test.
- `:47-48` DMA channels 10 and 11 are hardcoded, and
  `drivers/pwm_audio/pwm_audio.c:67-68` hardcodes 8 and 9. Which DMA channels
  Pico-PIO-USB claims is **not confirmed**; check when vendoring it. It also
  wants a repeating 1 ms timer. The composite driver already uses
  `TIMER0_IRQ_2`, but composite is not built on this board, so that is clear.

**`src/frank/frank_audio.c:66-68`** hardcodes `GPIO_FUNC_PIO1` in
`i2s_claim_pins()`. Same problem as `drivers/audio.c:99` and the same fix.

**`build.sh`**

- `:11-13` and `:25` add `fj` to the usage text and the `PLATFORM` default list.
- Force `USB_HID=1` for `fj`, since there is no other input path.

**`release.sh`**

- `:34-43` `BUILD_MATRIX` gains Fruit Jam entries. One variant in phase 1, since
  `COMPOSITE` is unavailable and there is no VGA fallback. Note two existing
  problems that will bite while iterating: all build output goes to `/dev/null`
  at `:113-119`, so a failed variant tells you nothing, and `version.txt` is
  written before building at `:91`, so a failed release still consumes a version
  number.

**`README.md`**

- Add the platform, its build command, and the fact that it needs a USB keyboard
  rather than PS/2.

### Licence headers

`drivers/usbhid/CMakeLists.txt`, `drivers/usbhid/usbhid_wrapper.c`,
`drivers/sdcard/CMakeLists.txt` and `drivers/HDMI_vga_hstx.c` all carry
frank-cpc headers naming the wrong project. `CLAUDE.md` section 10 says to fix
the header of any such file you touch, and this port touches several of them.
Pico-PIO-USB is MIT licensed, which is compatible with the GPLv3 combination;
vendoring it means keeping its own headers intact.

---

## 8. Phased plan

### Phase 1a: boots and displays, no input

Deliverable: cold boot to the Acorn MOS screen on a DVI monitor, disc images
loaded from SD, a USB CDC console on the USB-C port, no keyboard and no sound.
The bottom 16 scanlines will be missing, exactly as they are on the three
existing boards. See the end of section 3.

This split exists so that the riskiest part of the port, PIO-USB, is added to a
build that is already known to boot and display.

1. Write `src/board_fj.h`. The DVI, SD, I2S and I2C pin numbers are all confirmed
   from the schematic, so this is transcription.
2. Add the `fj` platform to `CMakeLists.txt`, `board_config.h` and `build.sh`.
3. Gate PS/2 and the NES pad out of `frank_platform.c` and `frank_keyboard.c`.
4. Build with `USB_HID_ENABLED` off, so `pico_enable_stdio_usb` gives a console
   on the USB-C port. Check the ELF has an stdio driver, per `CLAUDE.md` section 5.
5. Force `SELECT_VGA = false` and reject `COMPOSITE`.
6. Pull up GPIO 33 internally. `SD_DETECT` runs straight from the card socket to
   the pin with no external resistor.
7. Confirm it links, then flash and test.

Nothing here is now blocked on unknowns. The DVI wiring, its polarity and the SD
pins are all read from the schematic.

### Phase 1b: a keyboard

Deliverable: a USB keyboard in an onboard USB-A socket, at 252 MHz, with the
existing PIO DVI driver untouched.

This is the part of the port that can fail. Everything before it is transcription
and everything after it is optional.

1. Fix `drivers/audio.c:99` and `frank_audio.c:66-68`, the two places that map
   any non-`pio0` block to `GPIO_FUNC_PIO1`. I2S has to move to PIO2 and until
   this is fixed it will silently get the wrong pad function.
2. Vendor Pico-PIO-USB. Wire up the scaffolding at
   `drivers/usbhid/CMakeLists.txt:46-54` that was left unfinished, and add the
   library link the comment at `:52-53` anticipates but never makes. The library
   itself is nowhere in the tree. Note the fork cannot address PIO2:
   `pio_usb.c:1339` and `:1342` choose `pio0` or `pio1` from a one-bit test. So
   PIO0 keeps DVI, PIO1 takes PIO-USB, PIO2 takes I2S.
3. Drive GPIO 11 high to power the hub and both sockets, then wait before
   enumerating. The hub is on the switched rail, so it is unpowered until this
   happens and its 12 MHz crystal has to start.
4. Enumerate. A keyboard is always behind the hub on this board, so
   hub support is not optional, and it is historically the weaker part of the
   TinyUSB plus PIO-USB host path.
5. Soak it. Hours, not minutes. The failure mode of a marginal clock is
   intermittent dropped packets, not a clean refusal.

If step 5 does not hold at 252 MHz, phase 3 becomes necessary.

### Phase 2: sound

Deliverable: sound from the onboard speaker and the headphone jack.

1. Write `drivers/tlv320dac3100.c`. I2C0 bring-up, soft reset, PLL configuration
   at 31,250 Hz, output routing. No reset release needed, see section 5.
2. Call it from `frank_platform.c` before the first audio call.
3. Drop the PWM backend from the F12 menu on this platform.
4. Test with all remaining backends, checking that switching away from one does
   not leave it driving pins. `CLAUDE.md` section 9 lists that as a known rough
   edge.

### Phase 3: HSTX video, only if phase 1b will not hold at 252 MHz

Deliverable: the same machine, with the system clock free of the video timing, so
that PIO-USB can have a clock that suits it.

The cost of arriving here is the `FRANK_AUDIO_HDMI` backend, which goes away with
the PIO driver. The onboard speaker and headphone jack are unaffected.

1. Adapt `fruitjam-doom/Framebuffer_RP2350.c` into `drivers/HDMI_hstx.c`, in its
   8bpp RGB332 configuration with horizontal doubling and a `row * 256 / N`
   vertical scale, driven from the submodule's timing table. Change
   `rgb555_to_bbc()` and the other four places listed in cost 2 to write RGB332
   bytes. 720x576p50 is still the mode to prefer, because it matches the
   emulator's 50 Hz, but 640x480p60 now also shows all 256 lines, so the mode
   choice is no longer forced. Prove the driver at 252 MHz before touching the
   clock tree, so that video and clocking fail independently.
2. Adapt `pico-mac/src/clocking.c`: `pll_usb` to 528 MHz, `clk_sys` to 264,
   `clk_peri` to 132, `clk_usb` and `clk_adc` to 48, and `pll_sys` reprogrammed
   by the search for whatever `clk_hstx` the chosen mode needs, 135 MHz for
   720x576p50.
3. Re-verify the frame pacer. `frank_perf_tick()` at `frank_platform.c:120-133`
   busy-waits to 50 Hz on the assumption that the asm 6502 core is faster than
   real time at 252 MHz. At 264 MHz the margin grows rather than shrinks, but
   this is still the one place a clock change shows up as an emulation-speed
   change, so measure it rather than assume it.

---

## 9. Resource budget

The measured figures in `CLAUDE.md` section 6 are the baseline. For the Fruit
Jam under phase 1, expect the `HDMI_PIO_AUDIO` numbers for m2, 353,340 bytes of
flash and 334,276 bytes of RAM, plus or minus the board-specific glue.

What changes:

- **Flash goes from tight-looking to irrelevant.** 16 MB rather than 4 MB, so
  the current 8.4% becomes about 2.1%.
- **Main SRAM is unchanged at 512 KB**, the figure `CLAUDE.md`'s percentages
  are computed against; the chip's 520 KB total includes the two 4 KB scratch
  banks, budgeted separately below. `COMPOSITE`, the variant `CLAUDE.md` tells
  you to budget against, does not exist here. The tight variant becomes
  `HDMI_PIO_AUDIO` at 63.8%.
- **The 4 KB scratch banks remain the real constraint.** `SCRATCH_X` at 58.7%
  and `SCRATCH_Y` at 65.6% on the default m2 build. Nothing about the Fruit Jam
  helps with this, and phase 3 adds pressure: PIO-USB's own figures are 15 KB of
  ROM and RAM, and `CLAUDE.md` constraint 4 requires core 1 to stay resident in
  RAM.
- **8 MB of PSRAM** is available where the M2's is optional. Useful for FatFs
  buffers and potentially for caching disc images. Not for anything on the
  emulation hot path.

Measure after each phase with the existing link options at
`CMakeLists.txt:579`, which already print memory usage.

---

## 10. Open questions

These need a schematic or a board. Each is called out at the point it matters
above; collected here so they can be resolved in one pass.

1. **Answered: yes, through a 220 ohm series resistor per line and nothing
   else.** Polarity matches `board_m2.h` exactly. See section 3.
2. **Answered: no, the USB-C port cannot be a host.** Sink-only CC termination
   and no firmware-controlled VBUS source. This removed option A and rewrote the
   phasing. See section 6.
3. **Resolved. The codec PLL runs from BCLK with no MCLK, and the constants are
   worked out.** `pico-mac/src/main.c:640-650` proves the BCLK routing on this
   board, and `tools/tlv320_clocks.py` derives the dividers from the TI
   datasheet's own constraints and prints the working. What is left needs
   hardware: whether the PLL actually locks.
4. **Half answered: the hub needs nothing but power and time.** `X6` is a CH334F
   with its own 12 MHz crystal at `Y2`, no reset or configuration pin wired, and
   `V5` on the switched `VBUS` rail. So GPIO 11 plus a settling delay is the whole
   of it. Two details worth knowing: there is a **third** downstream port on the
   2x16 header at `JP3` pins 14 and 16, and the two front LEDs are driven by the
   hub's own `LED2` and `LED4` pins, not by firmware. What is still open is
   whether a keyboard enumerates behind it reliably, which is phase 1b step 5 and
   needs hardware.
5. **What are buttons 2 and 3 on GPIO 4 and 5 for?** Still a product decision.
   Electrically they are plain switches to ground needing internal pull-ups.
   Candidates: BREAK, and the F11 or F12 overlays, which would make the machine
   usable without reaching for the keyboard. Note the schematic's own naming is
   crossed: net `BUTTON2` is on GPIO 4 but reaches part `BTN3`, and net `BUTTON3`
   is on GPIO 5 but reaches `BTN2`. Which physical button is which needs the board
   layout or a multimeter, not the netlist.
6. **Should the ESP32-C6 be held in reset?** Now sharper rather than resolved.
   `PERIPH_RST` is pulled up, so the ESP32 runs from power-on unless firmware
   drives GPIO 22 low, and that would also reset the codec. The reason to care is
   the GPIO 23 hazard in section 5, not the SPI lines.
7. **Is the 16-line clip in section 3 acceptable for phase 1, or a blocker?**
   This is a product decision, not a hardware one, and it applies equally to the
   three existing boards. If it is a blocker, wiring up `720x576p50` in the
   submodule is a smaller job than the HSTX driver and could be pulled forward
   ahead of phase 3. Note that under HSTX the clip does not arise at all, at any
   mode, because the vertical scale is arbitrary. See cost 2 under option B.
8. **Answered: the DMA does the 2x horizontal scale.** One 8-bit transfer per
   pixel, replicated across the bus, plus `expand_shift`'s `ENC_N_SHIFTS`. No CPU
   pass. See cost 2 under option B.
9. **Does PIO-USB enumerate reliably at 252 MHz?** This is now the question that
   decides the size of phase 3. The divider arithmetic says 252 MHz is exactly
   representable and has a smaller jitter quantum than a shipping Adafruit
   configuration, but that is not proof. Needs hardware and a long soak. If the
   answer is yes, the HSTX driver and the clock rework both disappear.
10. **Does anything else on the board depend on `clk_peri` or `clk_adc`?** The
   pico-mac clock plan stops and reconfigures both. frank-micro's SD card sits on
   SPI0, which is clocked from `clk_peri`, so an SD timing check belongs in any
   test of the option B clock plan.

---

## 11. Testing the port on hardware

There is no CI and no test suite, so every check is manual. Follow
`CLAUDE.md` section 9 and extend the compile-only sweep to the new platform:

```bash
for p in m2 m1 z0 fj; do for d in HDMI_PIO_AUDIO HDMI_PIO COMPOSITE; do
  [ "$p" = z0 ] && [ "$d" = COMPOSITE ] && continue
  [ "$p" = fj ] && [ "$d" != HDMI_PIO_AUDIO ] && continue
  PLATFORM=$p HDMI_DRIVER=$d ./build.sh || echo "FAILED: $p $d"
done; done
```

All eight existing variants pass at commit `c27ffbf`, so a failure among them
means the platform addition broke something shared.

On hardware, the paths that cross driver boundaries:

- Cold boot to the Acorn MOS screen.
- A MODE 7 teletext screen and at least one bitmap mode. They take different
  blit paths in `frank_gui.c`.
- Mount a disc from the F11 browser and SHIFT-BREAK autoboot it.
- Every audio backend the platform offers, switched live from F12, checking that
  the one you left is not still driving its pins.
- A USB keyboard, including F11, F12, Print Screen and Ctrl+Alt+Del.
- A screenshot, then confirm the BMP on the card.
- Boot with no SD card. Confirm the error screen stays up rather than rebooting
  every few seconds, and use GPIO 33 card detect if it makes that easier.
- Several minutes of running, to catch a watchdog reset or lockup.

Two process points from `CLAUDE.md` section 8 apply with extra force on a new
board. Ask the user to enter BOOTSEL mode before every flash: bad firmware
reliably drives the RP2350 into a Cortex-M33 lockup at `PC=0xEFFFFFFE` that
survives reset and flash erase. And diagnose over SWD rather than serial,
because release builds have no console at all.

---

## 12. What this document's claims rest on

Read this before running a validation pass. It says what state the citations were
taken against, how to resolve them, and which claims are weaker than the prose
around them might suggest.

### Provenance

| Thing | Identifier |
|---|---|
| frank-micro | commit `c27ffbf`, dated 2026-06-19 |
| `frank-hdmi-sound` submodule | commit `388d879`, dated 2026-06-19 |
| Pico SDK | tag `2.3.0`, commit `98a542c1`, at `/home/adq/dev/pico-sdk` |
| `adafruit/pico-mac` | commit `59c910b`, shallow clone, third revision only |
| `adafruit/fruitjam-doom` | commit `f2ded0b`, shallow clone, third revision only |
| `tannewt/Pico-PIO-USB` | commit `f3f9d11`, shallow clone, third revision only |
| `adafruit/Adafruit-Fruit-Jam-PCB` | commit `252dc09`, dated 2025-08-07, shallow clone, fourth revision only |

The three clones were made into a scratch directory, not into this repository, so
they are **not** present for a later validator to re-read. Re-clone them at those
commits, or resolve the citations by symbol name. None is vendored and none
should be until a phase actually needs it.

Two traps for a validator here.

`PICO_SDK_PATH` was **not** set in the environment when this document was
written. Every `pico-sdk/...` citation is relative to `/home/adq/dev/pico-sdk`,
found by searching rather than from the variable. Export it, or resolve the paths
by hand.

The submodule tracks a moving branch, not a tag: `.gitmodules` gives
`branch = frank-micro`, and `CLAUDE.md` section 5 already warns about this. The
five `frank-hdmi-sound/...` citations are against commit `388d879`. If the branch
has moved, those line numbers will have drifted and must be re-resolved by symbol
name rather than treated as failures.

### Validation record

A validation pass was run on 2026-08-21 against the same three commits. Every
Pico SDK and submodule citation resolved as written, as did the arithmetic.
The frank-micro citations that had drifted or were wrong were corrected in
place, so the line numbers in this revision are as-verified. Two claims were
upgraded from asserted to verified during the pass: the `clk_usb` divide-by-5
in section 6, checked against the register header, and the single-PIO-block
figure for Pico-PIO-USB, whose README resource list reads "1 PIO, 3 state
machines, 32 instructions" alongside the 15 KB ROM and RAM figures. The
secondary claims and inferences listed below remain unresolved, no build was
run, and no hardware was tested.

A second pass on 2026-08-21 added the HSTX data-path material to option B, from
the SDK headers on this machine: `hstx_fifo.h:38`, `hstx_ctrl.h:36`, `:45`,
`:53`, `:63`, and `DREQ_HSTX 52` at `dreq.h:68`. All primary, all local. The same
pass established three things by reading the tree rather than by inference:

- The DispHSTX library that `drivers/HDMI_vga_hstx.c` includes is **not vendored**
  anywhere in the repository, so that file cannot compile, not merely is not
  compiled. A `find` for `disphstx` returns nothing.
- **Nothing in frank-micro uses HSTX at all.** `HAS_HSTX` is defined at
  `src/board_m2.h:21` and used nowhere in any `.c`, `.h`, `.S`, `.txt` or `.sh`
  in the tree, and there is no HSTX reference anywhere in the `frank-hdmi-sound`
  submodule. The claim at `src/board_m2.h:12-13` that HSTX HDMI is the default
  video and audio path is false, and is recorded as a finding in
  `docs/INVESTIGATION.md`.
- The two existing PIO video drivers are architecturally different, not two
  spellings of the same design. Recorded in `CLAUDE.md` section 6.

Two new claims in option B are explicitly **unverified** and marked as such in
the prose: that HDMI data islands could be emitted through `expand_shift`'s raw
path using precomputed TERC4 symbols, and that `csr.N_SHIFTS` plus the per-lane
rotate can pixel-double horizontally. Neither is load-bearing for the plan.

A third pass on 2026-08-22 read `adafruit/pico-mac` and `tannewt/Pico-PIO-USB`
and rewrote sections 5 and 6 against them. It corrected two claims that the
first two revisions stated as fact:

- **PIO-USB does not require 120 or 240 MHz.** The library computes its four
  dividers from `clk_sys` at run time at `pio_usb.c:1399-1411` and contains no
  clock check. The 120-or-240 assertion came from an Arduino example; the C
  examples say "Sysclock should be multiple of 12MHz"
  (`example/usb_device/usb_device.c:125`), which is what the divider arithmetic
  supports. This promotes option C from dismissed to the first thing to test in
  phase 3.
- **The palette pass produces RGB332, not RGB565**, and `expand_tmds` is a
  per-lane bitfield extractor rather than a fixed RGB565 layout. Read from the
  field descriptions in `hstx_ctrl.h`, then confirmed against a working RGB332
  configuration at `fruitjam-doom/Framebuffer_RP2350.c:397-404`.
- **There is no per-line CPU pass at all**, if the framebuffer holds RGB332
  bytes. Horizontal doubling comes from an 8-bit DMA transfer per pixel plus
  `expand_shift`'s `ENC_N_SHIFTS`, and vertical scaling from the row index
  arithmetic in the command list. Both read from `Framebuffer_RP2350.c`, both in
  shipping code. This also removes the 16-line clip in section 3 as a constraint
  on the mode choice.

A fourth pass on 2026-08-22 confirmed the load-bearing part of that against the
RP2350 datasheet rather than against working code. Byte-lane replication on
narrow writes is stated in section 2.1.5 (page 27), in the DMA chapter
introduction at section 12.6, and again in the WS2812 example at section 11.6.
The datasheet was downloaded for the purpose and is **not** kept in the
repository; re-fetch it from
`https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf` to
re-check. The one residual inference is that the HSTX FIFO is among the "majority
of" IO registers the section covers, since it does not enumerate them.

The same pass resolved the weakest claim in section 5, that the codec PLL can
run from BCLK with no MCLK, against shipping code rather than the part family.
The 4bpp zero-conversion path, the half-intensity consequence, the jitter-quantum
comparison and the codec PLL constants are all **derived**, listed below. Still
no build was run and no hardware was tested.

A fifth pass on 2026-08-22 read the schematic, which Adafruit publish as Eagle
9.6.2 XML in `adafruit/Adafruit-Fruit-Jam-PCB`. Netlists were extracted from
`Adafruit Fruit Jam.sch` with a short Python script over the `<nets>` elements, so
every schematic claim below is primary and local, and citations are by net name
and part designator rather than by line number.

What it settled, and what changed as a result:

- **All three inferences listed below are resolved.** The USB-A hub is fed from
  the PIO-USB pins, the DVI pins are directly driven, and the USB-C port will not
  do host mode.
- **Option A is dead**, and with it the shape of the plan. Section 6 keeps it as a
  recorded dead end, section 8 splits phase 1 in two, and the effort table in
  section 1 changed.
- Four things nobody had asked about turned out to matter: `PERIPH_RST` is pulled
  up so nothing needs releasing; GPIO 23 is shared with the ESP32-C6's boot strap,
  which makes one register write in the borrowed codec sequence a hazard; the DVI
  pair polarity is confirmed and pico-mac's build script gets it wrong in one
  configuration; and card detect and both buttons need internal pull-ups.

Still no build was run and no hardware was tested.

### Resolving the citations

There are roughly a hundred `file:line` and bare `:NNN` citations. A bare
`:NNN` is relative to the file named immediately above it, which in section 7 is
the bolded heading of the bullet list it appears in. They are not ambiguous, but
they are not self-contained either, so resolve them in document order.

### Claims by strength of evidence

The prose does not everywhere distinguish these, so the table does.

| Class | What it means | Examples |
|---|---|---|
| Primary, local | Read directly from a file on this machine. Fully verifiable. | Every Fruit Jam pin number, from the SDK board header. The SPI0 funcsel values. `clk_hstx` aux sources. `NUM_PIOS 3`. The VCO range. Every frank-micro `file:line`. The 240-line clamp. |
| Primary, external, verbatim | Quoted from the source, not summarised. | The Pico-PIO-USB 120/240 MHz clock check in section 6. Pico-PIO-USB's resource figures. |
| Secondary | Taken from a search or page summary, **not** read from the primary source. Needs a primary check. | Listed below. |
| Inference | Not documented in anything read. Reasoned from other facts. | Listed below. |
| Derived | My arithmetic from cited figures. Checkable, but not a source. | Listed below. |

### Secondary claims to re-source

These are stated in the prose with more confidence than the evidence behind them
warrants. Each needs checking against a primary source before anyone relies on
it.

1. **Section 2: the E9 erratum applies to the A2 part on this board.** Taken from
   a search-result summary. Neither Adafruit's page text nor the RP2350 datasheet
   erratum list was read. Check the datasheet.
2. **Section 5: the codec is at 7-bit I2C address 0x18.** From a summary of the
   TI datasheet, not the PDF itself.
3. **Section 5: roughly 1 ms post-reset initialisation and roughly 10 ms PLL
   start-up.** Same, and the word "roughly" is doing real work. Get the exact
   figures from the datasheet before writing timing code.
4. **Resolved twice over.** The third revision established from
   `pico-mac/src/main.c:640-650` that the codec can take its clock from BCLK
   with no MCLK: register 0x04 selects PLL_CLKIN as BCLK and CODEC_CLKIN as the
   PLL, and GPIO 25 is never touched. The fourth revision then read the TI
   datasheet, document SLAS671C, and replaced the derived PLL constant table with
   one computed against the datasheet's own constraints. Both figures that
   revision flagged as recollection are now primary: the PLL output range is 80
   to 110 MHz, from equations 7 and 8, and the constraint is
   `MDAC x DOSR / 32 >= RC` with RC 8 for the reset-default processing block
   PRB_P1, from section 6.3.10.14 and table 6-11. The arithmetic lives in
   `tools/tlv320_clocks.py` rather than in prose, so it can be re-run.
5. **Section 2: board dimensions, 520 KB SRAM, and the "2-port USB type A hub"
   description.** All from Adafruit page summaries. Low stakes, but not primary.

### Inferences to confirm

**All three were confirmed or refuted by the fifth pass, from the schematic.**
Kept below because they shaped three revisions of this document, and because the
reasoning that led to them was sound even where the conclusion was not. Each now
carries the finding.

Three, in descending order of how much rested on them.

1. **Confirmed. The USB-A hub is fed from the PIO-USB pins rather than the native
   USB controller.** Net `USBH_D+` joins `X4` GPIO 1 to `X6` pin `D+U`, and
   `USBH_D-` joins GPIO 2 to `D-U`. The original wording of the inference
   follows. The evidence is that the SDK board header names GPIO 1 and 2
   `ADAFRUIT_FRUIT_JAM_USB_HOST_DATA_PLUS_PIN` and `..._MINUS_PIN`, separately
   sets `PICO_DEFAULT_PIO_USB_DP_PIN` to GPIO 1, and that Adafruit describe the
   USB-C port as being for "bootloading/USB client". No schematic was read. This
   inference is what creates the clock conflict in section 6, and therefore
   shapes the whole phased plan. **If it is wrong, section 6 is moot and phase 3
   disappears.** Confirm it first.

   Since the first revision, section 6 adds a design argument for why this is
   what a deliberate design would look like: the RP2350 has one USB controller,
   it cannot be host and device at once, and Adafruit needed the USB-C port to be
   the device port for UF2 flashing and the CircuitPython workflow, which leaves
   PIO as the only source of a host port. That argument is reasoning from
   documented facts about the chip, **not** new evidence about this board, so the
   claim stays an inference. It does raise the prior enough that planning around
   it is reasonable while the schematic check is outstanding.
2. **Confirmed. The DVI pins are directly driven**, through one 220 ohm series
   resistor each and nothing else, with the pair polarity matching `board_m2.h`.
   See section 3.
3. **Refuted. The USB-C port will not operate as a host.** `R19` and `R20` put
   5.1K from each CC pin to ground, which is sink termination, and the only
   firmware-controlled 5 V switch, GPIO 11 through `Q1` and `T1`, feeds the USB-A
   side. This was the assumption phase 1 rested on, and losing it is why phase 1
   is now split in two. See option A in section 6.

### Derived figures

My arithmetic, from cited inputs. Recompute rather than look up.

- 25.2 MHz pixel clock times 10 gives the 252 MHz TMDS bit clock.
- 252 and 504 are not multiples of 120, and 240 is. This is the whole of the
  clock conflict.
- 240 divided by 5 is 48, for `clk_usb` from `pll_sys`.
- 126 MHz times 2 bits per HSTX clock gives 252 Mbps. VCO 756 is 12 times 63.
- 256 logical lines doubled is 512, against 480 active raster lines.
- 720x576p50: 27 MHz pixel clock, 270 MHz bit clock, 135 MHz `clk_hstx`,
  VCO 1080 is 12 times 90.
- Flash occupancy falls from 8.4% on 4 MB to about 2.1% on 16 MB.

Added in the third revision:

- The PIO-USB divider table in section 6. Each entry is `clk_sys` divided by
  48 MHz and by 96 MHz, and the exactness test is whether the result is a whole
  multiple of one 256th, the resolution of the PIO's fractional divider field.
- The jitter quanta of 8.3, 7.6 and 4.0 ns are one period of a 120, 132 and
  252 MHz clock. The claim that divider jitter is bounded by one system-clock
  period is reasoning about how a fractional PIO divider works, not a figure read
  from anywhere.
- 528 over 11 is 48, over 2 is 264, over 4 is 132. VCO 1584 is 528 times 3.
- 576 over 12 is 48 and 288 over 12 is 24, for the higher-clock variant.
- The rotates 25, 26 and 27 for the 4bpp path: a right-rotate of 32 minus 7 minus
  the bit index brings bit 0, 1 or 2 to bit 7.
- One valid bit in `expand_tmds` yields 0x80, so a 4bpp framebuffer displays at
  half intensity. Derived from "remaining LSBs are masked to 0" in the field
  description.
- The eight RGB332 values 0x00, 0xE0, 0x1C, 0xFC, 0x03, 0xE3, 0x1F, 0xFF, from
  the bit-0-red, bit-1-green, bit-2-blue ordering in `rgb555_to_bbc()`.
- 640 bytes per line times 256 lines times 50 Hz is about 8.2 million writes a
  second.
- 640x256 at 4bpp is 80 KB, so 160 KB double-buffered, equal to two 320x256 8bpp
  buffers.
- BCLK is 32 times the sample rate on frank-micro's I2S: `drivers/audio.c:121-123`
  gives 64 PIO cycles per frame and `drivers/audio_i2s.pio` spends two cycles per
  bit.
- The whole of section 5's PLL constant table.
- That RGB332's byte layout is `RRRGGGBB`, from applying the rotate rule to the
  lane values in `Framebuffer_RP2350.c:397-404`, and the eight colour byte values
  that follow from it.
- That a scaled active line costs 320 byte-sized DMA transfers against 160
  word-sized ones unscaled.
- That the identical codec register block cannot be correct at both 22,256 and
  49,716 Hz.

### Known-good baseline

`CLAUDE.md` section 9 states that all eight existing release variants build at
commit `c27ffbf`. The memory figures quoted in section 9 of this document are
`CLAUDE.md`'s, measured on linked builds, not re-measured here. **No build of any
kind was run while writing this document, and no Fruit Jam hardware was tested.**

---

## Sources

Fruit Jam hardware:

- [Adafruit-Fruit-Jam-PCB](https://github.com/adafruit/Adafruit-Fruit-Jam-PCB),
  commit `252dc09`. `Adafruit Fruit Jam.sch` is Eagle 9.6.2 XML and is the primary
  source for everything the fourth revision settled: the DVI series resistors and
  pair polarity, the USB-C CC termination and power path, the hub wiring, the
  `PERIPH_RST` pull-up, the GPIO 23 three-way net, and the missing pull-ups on
  card detect and the buttons. Cited by net name and part designator. Not vendored.
- `pico-sdk/src/boards/include/boards/adafruit_fruit_jam.h`, SDK 2.3.0, tag
  `2.3.0`, commit `98a542c`. Primary source for every pin number in section 2,
  and confirmed against the schematic in the fourth revision.
- [Adafruit Fruit Jam pinout](https://learn.adafruit.com/adafruit-fruit-jam/pinout)
- [Adafruit Fruit Jam overview](https://learn.adafruit.com/adafruit-fruit-jam/overview)
- [Adafruit Fruit Jam product page](https://www.adafruit.com/product/6200)
- [Adafruit Fruit Jam HSTX DVI output](https://learn.adafruit.com/adafruit-fruit-jam/hstx-dvi-output)
- [CircuitPython Fruit Jam board definition](https://github.com/adafruit/circuitpython/blob/main/ports/raspberrypi/boards/adafruit_fruit_jam/mpconfigboard.h)

USB:

- [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), resource usage
  and the 120/240 MHz requirement.
- [Pico-PIO-USB device_info example](https://github.com/sekigon-gonnoc/Pico-PIO-USB/blob/main/examples/arduino/device_info/device_info.ino),
  source of the clock-check quote in section 6.
- [Adafruit Fruit Jam USB host guide](https://learn.adafruit.com/adafruit-fruit-jam/arduino-usb-host)

Audio codec:

- [TLV320DAC3100 datasheet](https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf),
  TI document SLAS671C, February 2010, revised January 2017. Primary source for
  every constraint in `tools/tlv320_clocks.py`: section 6.3.11.1 for the PLL
  input range and the divider ranges, equations 7 and 8 for the PLL output range,
  section 6.3.10.14 for the divider identity and the DAC_MOD_CLK window, table
  6-11 for the resource classes, table 6-27 for the maximum clock frequencies,
  and the register tables in section 6.6 for the encodings. Not kept in the
  repository.
- [Adafruit_CircuitPython_TLV320](https://github.com/adafruit/Adafruit_CircuitPython_TLV320)
- [Adafruit TLV320DAC3100 I2S DAC guide](https://learn.adafruit.com/adafruit-tlv320dac3100-i2s-dac)

Fruit Jam reference implementations, third revision:

- [adafruit/pico-mac](https://github.com/adafruit/pico-mac), commit `59c910b`.
  jepler's Fruit Jam fork of Matt Evans' pico-umac. MIT licensed. Primary source
  for the HSTX driver structure (`src/video_hstx.c`), the clock plan and PLL
  search (`src/clocking.c`) and the TLV320DAC3100 bring-up (`src/main.c:580-745`).
- [adafruit/fruitjam-doom](https://github.com/adafruit/fruitjam-doom), commit
  `f2ded0b`. Chocolate Doom on the Fruit Jam. MIT licensed for the file that
  matters here, `Framebuffer_RP2350.c`, which is CircuitPython's picodvi
  framebuffer. Primary source for the RGB332 and 4bpp lane values, DMA pixel
  doubling, command-list vertical scaling, and the second copy of the codec
  sequence. Note the project as a whole is Chocolate Doom and therefore GPLv2,
  which is compatible with this repository; check the header of any file taken
  from it rather than assuming.
- [evansm7/pico-umac](https://github.com/evansm7/pico-umac), the upstream
  pico-mac forks.
- [tannewt/Pico-PIO-USB](https://github.com/tannewt/Pico-PIO-USB), commit
  `f3f9d11`, the fork pico-mac vendors. Primary source for the divider
  computation at `pio_usb.c:1399-1411`, the absence of any clock check, the
  `pio0`-or-`pio1` restriction at `:1339` and `:1342`, and the "multiple of
  12MHz" comment at `example/usb_device/usb_device.c:125`.

Clocks and HSTX:

- `pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/clocks.h:834-838`,
  the `clk_hstx` aux source list.
- `pico-sdk/src/rp2350/hardware_regs/include/hardware/platform_defs.h:27`,
  `NUM_PIOS 3`.
- `pico-sdk/src/rp2_common/hardware_pll/include/hardware/pll.h:40-48`, the
  750 to 1600 MHz VCO range used to check the `clk_hstx` settings.
- `pico-sdk/src/rp2350/hardware_structs/include/hardware/structs/hstx_fifo.h:38`,
  the HSTX FIFO write port.
- `pico-sdk/src/rp2350/hardware_structs/include/hardware/structs/hstx_ctrl.h:36`,
  `:45`, `:53`, `:63`, the HSTX `csr`, per-lane `bit[8]`, `expand_shift` and
  `expand_tmds` registers behind option B's data path.
- `pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/dreq.h:68`,
  `DREQ_HSTX 52`, the DMA pacing signal for the HSTX FIFO.
- `frank-hdmi-sound/src/frank_data_packet.h:23-26` and `:37`, the HDMI data
  island widths behind cost 3.
- `frank-hdmi-sound/src/frank_hdmi.c:160-169` and
  `frank-hdmi-sound/src/frank_dvi_timing.c:29-95`, the framebuffer clamp and the
  vendored timing table behind the 16-line clip in section 3.
- `pico-sdk/src/rp2350/hardware_regs/include/hardware/regs/hstx_ctrl.h`, the
  `EXPAND_TMDS` per-lane `NBITS` and `ROT` field descriptions behind cost 2, and
  the `CSR_COUPLED_MODE` description noted below.
- [RP2350 datasheet](https://pip.raspberrypi.com/documents/RP-008373-DS-rp2350-datasheet.pdf),
  document RP-008373-DS. Section 2.1.5 "Narrow IO register writes" at page 27,
  including the +0x4000 alias that disables replication; the DMA chapter
  introduction at section 12.6; and the WS2812 example at section 11.6. These are
  what confirm the DMA pixel-doubling mechanism in cost 2. Not kept in the
  repository.
- [pico-examples narrow_io_write](https://github.com/raspberrypi/pico-examples/blob/master/system/narrow_io_write/narrow_io_write.c),
  the official demonstration of the same behaviour, quoted in the datasheet
  section above.
- [pico-examples dvi_out_hstx_encoder](https://github.com/raspberrypi/pico-examples/blob/master/hstx/dvi_out_hstx_encoder/dvi_out_hstx_encoder.c),
  source of the two-bits-per-HSTX-clock figure, and the ancestor of
  `pico-mac/src/video_hstx.c`.

One HSTX feature is recorded here only so nobody rediscovers it as a shortcut.
`CSR.COUPLED_MODE` connects eight PIO output bits directly into HSTX bit selects
24 to 31, which would in principle let a PIO do the palette lookup and feed the
hardware TMDS encoder, removing cost 2's expansion pass. It requires `clk_hstx`
to be driven directly from `clk_sys`, which throws away the clock decoupling that
is the entire reason for using HSTX here.
