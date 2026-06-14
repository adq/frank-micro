# Conversion Notes

Note (Feb 28 2021) this is a few of my earlier notes; i'll flesh it out with more detail soon; *I did not want to delay releasing the source until I was happy with this document; y'all complain enough as it is!*

Note "Pico" builds here refer to anything other than regular b-em builds.

## 1. Remove (via #ifdef) as much code as possible

Remember we have limited flash and only 264K of RAM totoal, so removing large swathes first, and then putting some features back in is my go to method. Code is disabled by setting compile definition `NO_USE_XXX`  

### Currently enabled features for Pico builds (i.e. these defines are unset)

* `NO_USE_ACIA` -
* `NO_USE_DD_NOISE` -
* `NO_USE_FDI` -
* `NO_USE_KEYCODE_MAP` -
* `NO_USE_SOUND` -

This is unset except on RP2040

* `NO_USE_CMD_LINE` -
  
### Currently disabled for Pico Builds(i.e. these are set to `NO_USE_XXX=1`) for Pico builds

The following are currently all set to 1 (disabling feature) for Pico builds. Just enabling them again won't likely just work as they need fixing up for _HW_EVENT_ (see below) amongst other things.

* `NO_USE_ADC` -
* `NO_USE_ALLEGRO_GUI` -
* `NO_USE_CLOSE` -
* `NO_USE_CMOS_SAVE` -
* `NO_USE_COMPACT` -
* `NO_USE_CSW` -
* `NO_USE_DEBUGGER` -
* `NO_USE_DISC_WRITE` -
* `NO_USE_I8271` -
* `NO_USE_IDE` -
* `NO_USE_JOYSTICK` -
* `NO_USE_KEY_LOOKUP` -
* `NO_USE_MASTER` -
* `NO_USE_MMB` -
* `NO_USE_MOUSE` -
* `NO_USE_MUSIC5000` -
* `NO_USE_NULA_ATTRIBUTE` -
* `NO_USE_NULA_PIXEL_HSCROLL` -
* `NO_USE_PAL` -
* `NO_USE_RAM_ROMS` -
* `NO_USE_SAVE_STATE` -
* `NO_USE_SCSI` -
* `NO_USE_SET_SPEED` -
* `NO_USE_SID` -
* `NO_USE_SOUND_FILTER` -
* `NO_USE_TAPE` -
* `NO_USE_TUBE` -
* `NO_USE_UEF` -
* `NO_USE_VDFS` -
* `NO_USE_WRITABLE_CONFIG` -

## 2. CPU replacement

TODO

## 3. Cycle accuracy of non CPU hardware  

We cannot be having chains of function calls per clock cycle as per the original b-em. This is especially costly on a Cortex-M0 plus where your parameter shuffling is likely going to cause you 20 odd cycles before you've done anything, and this starts to get really costly at emulated CPU speed of 2Mhz.

### Polling vs hw_event

The original b-em code calls polltime() with cycle accuracy during CPU execution
to advance the hardware in sync with the processor... some hardware like VIAs and
video do work every cycle, whereas other slower stuff generally just decrements a counter
until a specific time...

In any case, we cannot afford the time to be jumping in and our of the CPU every cycle (or
even every instruction in general), so we instead use a priority queue (here called
hw_event_queue).

The basic rule is that each piece of hardware needs to figure out when it next needs attention,
which is when (or before if it wants to recalculate - though I think we always do it exactly) it needs
to next do something. It must wake up at exactly the point where interaction between the
hardware and the CPU is occurring. So from the hardware side this is generally when it is about
to signal an interrupt. When the hardware receives and event it "catches" up to the state
it would have as if it had been polltime()ed the whole time. It can generally do this in a much
more efficient fashion (e.g. subtracting N from a timer is easier than substract 1 N times).

Equally whenever certain IO reads or writes to the hardware that interact with its hardware state
are made, it must also "catch up" to the state it should be as of that cycle. How it does
this is up to the specific hardware (as we must be between "events" which wake the hardware
up or there would be nothing to catch up).

### Sub instruction timing

Certain demos (and even Rocket Raid - although arguably that is a bug) rely
on which cycle a read/write happens during an instruction. This is handled
in the original b-em CPUs by calling polltime(1) for every cycle during the instruction
to advance the hardware.

Our cpu.c does not support this, and indeed the only time you care is when reading from
or writing to hardware. Therefore in the FC-FF address space handler, when accessing hardware
we just correct the clock based on the opcode of the currently executing instruction and
where the ead (and/or) write happen within the instruction. Note that nominally, sometimes the
multiple reads or multiple writes happen within an instruction, however I just picked
hat I consider the "real" one based on common sense - it is possible some of these need correcting.

#### More Detail

Note that originally the code would return from the CPU into the interpreter loop whenever a HW_EVENT was needed,
however all the save/restore processor state is too costly for the frequency of events (CRTC needs one every 128 cycles), so the
CPU now calls into the event handler code directly.

#### Issues

Ok, so I think right now we're in a case of an even number of wrongs make a right. The code is obviously wrong, but my few
attempts to fix just end-up wac-a-mole-ing the problems. The code as is runs almost exactly the same as the real b-em.

Particularly wrong are IRQs;  IRQs should be checked the penultimate instruction cycle.. i have put
the cycle stretch after the write which effectively means and IRQ does not
happen on the penultimate instruction cycle.

Note also that IRQs are checked before the hardware is advanced for the remaining
cycle counts.

TODO - this does mean that IRQs can happen an instruction late when the
clocks for a cycle happen in one chunk (they don't do hw read/write), but
in this case that is probably idempotent. Note this also probably makes CLI
delay a cycle?

Anyway, at the time i wasn't focused on being 100% cycle accurate, but i would like to be in the future.

## Video

### Tiling strips

The idea here is to compose the 640 horizontal pixels from pre-generated 8 pixel
wide (4 word) strips. These can be composed via fixed length DMA chain (at the cost
of quite high DMA bandwidth). **Update: These cannot reasonably be used in a DMA chain
after all, because we defer the drawing, so changes to tile strips due to palette changes
may affect tiles that have not been rendered yet. Tracking this in an efficient manner (across scanlines)
seems like it would be more costly than just rendering the 4 words.**

#### Graphic modes

Based on ULA character count 10, 20, 40 or 80 and the clock frequency select
(high/low), we generate the 8 bit tiles for the graphics mode.
Depending on the mode these have 1, 2, 4 or 8 pixels always representing
a single byte of beeb video RAM. This is very much how the actual ULA
works.

In high frequency modes the screen is 80 tiles wide and in low frequency
modes it is 40 tiles wide (and we use a different scanline program
command to grow everything x2 horizontally).

We have a simple mapping of screen memory bytes to tiles, which we update
accordingly if palette/mode/flashing changes

If the CRTC character height is 8 which it usually is, we do 7 double height lines
and the 1 half height line, giving us (15/16) * 32 * 8 = 240, and the short line
in text generally is blank anyway. If the character height is > 8 (i.e. we
have mode 6 style, then we squish those lines as necessary as they are always black)

#### Teletext

Telext has a nominal 6 * 10 pixel width. This is stretched according to beeb
rules to 12 x 20 still binary pixels, and then we stretch each the result
to 16 x 20 with linear only filtering (16 brightness levels). Thus the left and right half of each
character is a 8 pixel wide strip. Because we are dealing with 3 pixel wide source strips (and the odd lines between them)
there are only 37 possible strips for a given foreground color and background color. We can fit all 37 possible 8 pixel strips
into an array of 169 pixels (perhaps it is possible to do slightly better - i didn't do an exhuastive search!).
This array is called "grey_pixels", and we keep a copy at runtime for each of 8x8=64 foreground/background color pairs. 

Teletext is interlaced, so we display characters as a different (except for double height) 16x10
half glyph in each field (unless double height)

## 4. RAM usage

### Shrinking lookup tables

### Moving RAM stuff to flash where possible

## 5. Multi-core

We split the video display work onto the second core.

The basic idea is that the CPU core has the CRTC, and it feeds a stream of CMDs over "the wire"
to the other core which does the rendering. These commands include ULA/CRTC updates, as well as
the data read from memory at the time.

This allows the display to run largely independently, but maintain the CPU/video order of event synchronization which is necessarily
to correctly display BBC output in the presence of CRTC/ULA or video RAM changes.

### Possible Issues

* Right now there is an arbitrary lag, although I think in practice the fact that audio will gate CPU core speed and
  video will gate the other core speed is fine.
