# PERFORMANCE.md — Prompt for the next autopilot session

> **Role / objective for the next session:** You are continuing the `frank-micro`
> port of the *beebjit* BBC Micro emulator to the RP2350. The firmware already
> boots, renders HDMI video, handles the PS/2 keyboard, loads discs, plays sound,
> and runs Prince of Persia. **The one remaining problem is emulation speed.**
> The committed (`git HEAD`) firmware runs at only **~28% of real time**. Your
> job is to get it to a **solid 100% real-time** BBC Micro.

---

## Hard constraints (do not violate)

1. **No overclocking.** Keep `CPU_SPEED = 252` MHz (HDMI timing depends on it).
   Do **not** raise the system clock to "buy" speed. The fix must come from the
   software pacing / emulation path.
2. **Avoid PSRAM** for any new hot-path data. PSRAM is reserved for cold data
   only (sideways/master RAM, disc structures). Do not move emulation hot paths
   into PSRAM.
3. **Do not regress the working version.** The committed build is the known-good
   reference: video, keyboard, sound, disc load, and POP all work. Every change
   must be re-verified against that baseline.
4. **Flashing protocol (important):** bad firmware reliably drives the RP2350
   into a Cortex-M33 hardware lockup (`PC=0xEFFFFFFE`) that survives `reset`,
   flash erase, and SRST. **Before every flash, ASK THE USER to put the board in
   BOOTSEL mode** (hold BOOTSEL while plugging USB, or BOOTSEL + tap RESET), then
   flash with `picotool load -x build/frank-micro.uf2`. Do not fight the lockup
   over SWD — just ask for BOOTSEL.
5. **Use the RP Debug probe (SWD) to diagnose, not serial polling.** When the
   board misbehaves, attach OpenOCD/GDB and look at the PC / stack directly
   instead of guessing from the serial console.

---

## How the pacing currently works (read this before changing anything)

- **Cores:** Core 0 = BBC CPU emulation + vsync + keyboard + serial console.
  Core 1 = HDMI video + audio (`frank_hdmi_run_core1`). Core 1 must stay
  fully RAM-resident; never block it.
- **CPU driver mode:** Pico uses `mode = 0 = k_cpu_mode_interp` (the
  **interpreter**) — see `src/Pico/main.c:177` (`bbc_create(mode, ...)`,
  `mode` defined at `src/Pico/main.c:136`). The x86 JIT and `inturbo` drivers
  are **not portable** to ARM, so the interpreter is the only option.
- **Accurate mode is forced ON:** `bbc_create` sets `accurate_flag = 1` whenever
  `fast_flag == 0` (`src/micro/bbc.c:2202-2206`). In accurate mode it also forces
  `externally_clocked_via/crtc/adc = 0` and `synchronous_sound = 1`
  (`src/micro/bbc.c:2208-2213`). Accurate mode installs
  `video_advance_for_memory_sync` as a **per-6502-memory-write callback**
  (`bbc_set_fast_mode_callback`, `src/micro/bbc.c:1844-1856`) — this is the
  tick-accurate CRTC sync and it is **expensive**.
- **The pacing loop:** `bbc_cycles_timer_callback` (`src/micro/bbc.c:~2960`)
  runs `cycles_per_run_normal` cycles (= `k_bbc_tick_rate / wakeup_rate`
  = 2,000,000 / 500 = **4000 cycles ≈ 2 ms of BBC time**, see
  `src/micro/bbc.c:3082`), then sleeps `delta_us = 1000000 / wakeup_rate = 2000 µs`
  via `bbc_do_sleep` → `os_time_sleeper_sleep_us`
  (`src/micro/bbc.c:3009-3024`, `2797-2824`).
- **The sleep implementation:** `src/Pico/os_time_pico.c:33-43` currently calls
  the SDK `sleep_us()`.

---

## CRITICAL FIRST STEP — disambiguate the bottleneck (do this before any fix)

Previous profiling of *broken* builds showed **~100% of Core 0 PC samples inside
`sleep_until` / `timer_time_reached` / `spin_lock_unsafe_blocking`** — i.e. all
the time was in the SDK `sleep_us()`. But that does **not** by itself tell us
which of two very different problems we have. You must measure this first:

> **Instrument `bbc_cycles_timer_callback` to time the EMULATION work alone**
> (the part that runs the 4000 cycles), separately from the sleep. Record, over
> ~1 second, the average wall-clock time spent in the *non-sleep* portion of one
> callback and report it via the SPEED serial command (or SWD-readable globals).

Interpretation:

- **Case A — emulation is fast (non-sleep work ≤ ~2 ms per 4000-cycle chunk):**
  The emulator *can* keep up; the 28% is purely a **pacing defect** — the sleep
  is overshooting (sleeping ~7 ms when asked for 2 ms) because the SDK
  `sleep_us()` alarm-pool spinlock contends with Core 1's HDMI timer access at
  500 Hz. → Go to **Track A**.

- **Case B — emulation is slow (non-sleep work > ~2 ms per chunk):**
  The interpreter + per-write CRTC sync simply cannot emulate a 2 MHz 6502 in
  real time at 252 MHz. Sleep tuning will never reach 100%. → Go to **Track B**.

It may be a mix. The measurement decides where to spend effort. **Do not skip
this step** — the last session burned many flash cycles guessing.

---

## Track A — fix the pacing (if emulation is fast enough)

The SDK `sleep_us()` acquires the alarm-pool spinlock shared with Core 1 HDMI.
Replace the blocking sleep in `os_time_sleeper_sleep_us`
(`src/Pico/os_time_pico.c`) with a **lock-free busy-wait** on the hardware timer:

```c
uint64_t now = time_us_64();
if (p_sleeper->next_wake_us > now) {
    while ((int64_t)(p_sleeper->next_wake_us - time_us_64()) > 0) {
        tight_loop_contents();
    }
}
```

Burning Core 0 in a busy-wait is acceptable — there is nothing else to run on
Core 0, and correct *pacing* (100% BBC speed) is the goal, not low host-CPU.

**WARNING — this exact change crashed at boot last session** (immediate HardFault
→ lockup). The crash was **never root-caused** and may have been an artifact of
testing on an already-corrupted board. Before concluding busy-wait is unusable:

1. Flash it from a **clean BOOTSEL boot** (ask the user for BOOTSEL first).
2. If it still faults, attach SWD and capture the exact faulting PC, `CFSR`
   (`0xe000ed28`), `BFAR` (`0xe000ed38`) and the call stack. Determine whether
   the fault is really in the busy-wait or somewhere else (e.g. `time_us_64`
   reentrancy, a watchdog starvation because `crash_handler_feed()` no longer
   runs often enough, or Core 1 starvation). Common real causes to check:
   - **Watchdog starvation:** the 60 s watchdog is fed in the vsync handler and
     other periodic spots. A tight busy-wait that never yields could still be
     fine (vsync runs on its own path), but verify `crash_handler_feed()` is
     reached often enough.
   - **64-bit math / timer rollover** in the subtraction — use the signed
     `(int64_t)(target - now) > 0` form shown above to be rollover-safe.
3. As a safer intermediate, try `busy_wait_until(absolute_time)` from the SDK
   (still spins, but uses vetted timer code), or a **hybrid**: busy-wait for
   short waits (< ~1 ms) and `sleep_us` for long ones.

Verify with the SPEED command — target `speed≈100%`.

---

## Track B — speed up the emulation (if the interpreter can't keep up)

These reduce per-instruction cost. Apply one at a time and re-measure; some
trade emulation accuracy for speed and may glitch raster-timed games, so verify
POP and a MODE 7 screen after each.

1. **Drop per-write CRTC sync (biggest lever).** Accurate mode installs
   `video_advance_for_memory_sync` as a memory-written callback on *every* 6502
   write (`src/micro/bbc.c:1844-1856`). Disabling `do_video_memory_sync` (or not
   forcing `accurate_flag = 1` on Pico) removes that callback and can roughly
   double interpreter throughput. Cost: some split-screen / mid-frame raster
   effects may be slightly off. Gate behind `#ifdef PICO_BUILD`.
2. **Externally-clock the peripherals.** Accurate mode forces
   `externally_clocked_via/crtc/adc = 0` (tick-accurate). Setting them to `1`
   (polled) reduces per-tick work. Gate behind `#ifdef PICO_BUILD`
   (`src/micro/bbc.c:2208-2213`).
3. **Lower `wakeup_rate`.** It is 500 Hz (`k_bbc_default_wakeup_rate`,
   `src/micro/bbc.c:51`). Larger chunks per wake (e.g. 250 Hz → 8000 cycles)
   reduce callback/sleep overhead at the cost of input latency. Keep ≥ 100 Hz
   for responsiveness.
4. **Keep the interpreter hot loop and 6502 RAM in SRAM, not flash/PSRAM.**
   Verify `inturbo`/interp hot code and the 64 KB BBC address space
   (`os_alloc_get_memory_handle`) are SRAM-resident (they currently are — keep
   it that way). XIP flash stalls in the instruction-decode loop are a silent
   tax; consider `__not_in_flash_func` on the hottest interpreter functions.
5. **Profile the interpreter** via SWD PC-sampling once sleep is removed from the
   picture, to find the actual hot instructions and target them.

---

## Tooling & workflow

- **Build:** `cmake --build build -j8` (artifact: `build/frank-micro.uf2`).
- **Flash:** ask user for BOOTSEL, then `picotool load -x build/frank-micro.uf2`.
- **Serial console:** `/dev/cu.usbmodem1424101` (appears when firmware runs;
  disappears on crash). Commands include `PING`, `STATUS`, `SPEED`, `DISK A
  INSERT <file>`, `BOOT`, `MODEL`, `KEY`, `TYPE`. **`SPEED`** measures the CRTC
  advance rate and prints `speed=NN%` — this is your primary success metric
  (`src/Pico/micro_serial_console.c`).
- **SWD diagnosis:** OpenOCD with CMSIS-DAP. Useful configs from last session:
  `/tmp/rescue.cfg` (normal halt/resume), `/tmp/cur2.cfg`
  (`connect_assert_srst`). Key addresses: `g_p_bbc` pointer at `0x20012154`;
  `p_sound` at `p_bbc + 0x1F0`; `CFSR=0xe000ed28`, `BFAR=0xe000ed38`. Watchdog
  scratch[0] at `0x400d800c` (CRASH_MAGIC `0xDEADC0DE`, CRASH_SENT `0xDEAD5E41`).
- **Verify the capture path** (USB Video + USB Digital Audio) to confirm the
  image and sound still look/sound right after a change — speed must not come at
  the cost of broken video/audio.

---

## Known dead ends (don't repeat these)

- **`synchronous_sound` is a red herring.** It was confirmed via SWD that
  `p_sound->synchronous = 0` and `sound_is_synchronous()` returns FALSE, so
  `bbc_do_sleep` (not `sound_tick`) is the path taken. Changing it does **not**
  affect speed. Leave it.
- **Forcing `accurate_flag = 0` alone** did not fix speed in the broken builds
  (still measured 4%) — but those builds were also fighting the lockup, so the
  measurement is suspect. If you reach Track B, re-evaluate it cleanly.
- **Replacing the `os_channel` FIFO push with `__sev()`** did not help; reverted.

---

## Definition of done

- [ ] First step done: emulation-only time per callback measured and reported;
      bottleneck classified as Case A, Case B, or mixed.
- [ ] `SPEED` reports a stable **~100%** (≥ 95%) with no overclock.
- [ ] Video (HDMI), sound, and PS/2 keyboard all still work.
- [ ] Prince of Persia (`PrinceOfPersia.ssd`) loads and plays at full speed on
      Master 128.
- [ ] A MODE 7 (teletext) screen and at least one bitmap mode render correctly.
- [ ] Firmware survives a cold boot and runs for several minutes without lockup.
- [ ] Diagnostic/instrumentation code removed or gated behind a debug flag before
      committing.
