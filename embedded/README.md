# BENCmouth on a microcontroller

What the core actually costs on bare metal. Flash and RAM were measured by
cross-compiling `src/core/*.c` with `arm-none-eabi-gcc -Os -ffreestanding`;
instruction counts and the correctness results come from running the code on
emulated Cortex-M hardware under QEMU, using the harness in `qemu/`.

The one thing still missing is cycles. QEMU is a functional emulator with no
wait states, no flash accelerator, no bus contention and no cycle-accurate
Cortex-M model, so every timing figure below is **instructions retired**. Ratios
between configurations are trustworthy; absolute megahertz requirements are an
estimate built on top of them. Real timing needs a board and the DWT counter.

## It runs, and it is bit-exact

Every configuration produces byte-identical audio to the x86-64 build over a
103,405-sample utterance. `qemu/run.sh` checks this on every run by building the
same `bench.c` for both targets and comparing an FNV-1a hash of the samples.

| Config | Core | Sample loop | Flash | Engine RAM | Ins/sample | Checksum |
|---|---|---|---:|---:|---:|---|
| m3-fixed | Cortex-M3 | Q18 integer | 38,652 | 23,184 | **7,651** | `0xa294a18b` |
| m3-float | Cortex-M3 | soft float | 38,080 | 23,184 | **10,271** | `0xdf1486de` |
| m4f-float | Cortex-M4F | hard float | 35,944 | 23,184 | **616** | `0xdf1486de` |
| m4f-full | Cortex-M4F | hard float, effects on | 39,316 | 81,820 | **628** | `0xdf1486de` |
| m7-float | Cortex-M7 | hard float | 35,868 | 23,184 | **613** | `0xdf1486de` |

Flash here includes the harness - startup, the memcpy/memset shim, and the
benchmark - so it runs about 2 KB above the core-only figures further down.

All four float builds produce the same checksum as each other *and* as the host,
across three different cores and both float ABIs. The fixed-point build produces
its own value and matches its own host build. Nothing in the sample path is
architecture-dependent.

## The FPU is the dividing line

This is the headline, and it is not what `bm_fixed.h` implies.

Soft float costs **16.7x** against hardware float - 10,271 instructions per
sample versus 616. That confirms the header's "roughly an order of magnitude",
if anything understating it.

The Q18 fixed-point path recovers **1.34x** of that. 10,271 down to 7,651, still
12.4x worse than an FPU. The header describes the choice as "between comfortable
and impossible"; measured, fixed point is a 25% dent in the soft-float penalty,
not an escape from it.

**Why**, measured per-sample-path function on M3 at `-O2`:

| Function | Instructions | Soft-float helper calls |
|---|---:|---:|
| `bm_resonator_tick_q` (twelve per sample) | 123 | **0** |
| `bm_glottis_tick` | 304 | **66** |
| `bm_noise_tick` | 25 | 6 |
| `bm_synth_tick` | 810 | 15 |

`bm_fixed.h` converts the resonator cascade, and does it completely - zero
soft-float calls remain in a resonator tick. But the *sources* were never
converted. `bm_synth.c` calls `bm_glottis_tick()` and `bm_noise_tick()`, both of
which return `float`, and pushes the result through `bm_q_from_float()`. On a
part without an FPU the glottis - six partials, a sine per partial, and its
transcendentals - stays soft float and becomes the dominant cost.

So the cascade is free and the sources are not. Anyone wanting fixed point to
deliver what the header promises would have to convert the glottis and the noise
source too. Nothing is wrong with the current code; it just buys less than it
looks like it buys, and the number to plan around is 7,651.

Optimisation level is not the lever either. At `-O2` GCC fully inlines
`bm_q_mul` - six `smlal` instructions, no calls, verified in the disassembly -
and the fixed-point figure improves only 4% (7,651 to 7,348) for 27% more flash.
On the FPU configs `-O2` is slightly *worse* than `-Os` (616 to 654). Build
these at `-Os`.

## What that means for real parts

At 22050 Hz, multiply the per-sample figure by 22,050:

| Target | Needs | Roughly available | Verdict |
|---|---:|---:|---|
| Cortex-M4F @ 96 MHz (MAX32660) | 13.6 M ins/s | ~90 M | comfortable, ~15% of a core |
| Cortex-M4F @ 64 MHz (nRF52840) | 13.6 M ins/s | ~55 M | comfortable, ~25% |
| Cortex-M3 @ 72 MHz (STM32F103) | 169 M ins/s | ~55-70 M | **no** - short by ~2.5x |
| Cortex-M3 @ 72 MHz, at 8 kHz | 61 M ins/s | ~55-70 M | marginal, and 8 kHz costs the top formants |

An FPU-less part is a much worse proposition than the flash and RAM figures
suggest. Get a Cortex-M4F.

The effects chain is nearly free in CPU and expensive in RAM: 616 to 628
instructions per sample, a 2% cost, against 23,184 to 81,820 bytes of engine.
That trade points the other way from what you would guess.

M7 measures the same as M4F (613 vs 616), as it should - same ISA, same work.
M7's real advantage is dual-issue and cache, neither of which QEMU models.

## Flash

Whole core, all sixteen translation units, `-Os`, harness excluded:

| Configuration | Cortex-M0+ | Cortex-M4F |
|---|---:|---:|
| Full - float, effects, text front end, LTS rules | 39,998 | 38,298 |
| `-DBM_WITH_EFFECTS=0` | 35,810 | 34,928 |
| Fixed-point + `int16` out + no effects | 36,614 | - |
| **Synthesis only** - caller feeds ARPABET, no text/LTS/rules | **21,546** | - |

The fixed-point build is *larger* than the float build. Coefficients are still
computed in float once per frame, so a fixed-point build carries both paths. You
choose it for speed on an FPU-less part, never for size - and per the section
above, it does not buy much speed either.

The 1.6 MB compiled CMUdict is not an option on any of this. The NRL
letter-to-sound rules - 12,583 bytes, already in every row - are the embedded
front end.

## RAM

One `bm_engine` and almost nothing else. The engine allocates nothing: the
caller supplies a `bm_engine_storage`, and a static assertion fails the build if
the real struct stops fitting. Cortex-M4, one instance:

| Configuration | Engine |
|---|---:|
| Desktop defaults (512 phonemes, 1024 text, effects on) | 81,820 |
| `-DBM_WITH_EFFECTS=0` | 23,184 |
| + `BM_MAX_PHONEMES=128 BM_MAX_TEXT=256` | 6,672 |
| + `BM_MAX_PHONEMES=64 BM_MAX_TEXT=128` | 3,920 |
| + `BM_MAX_PHONEMES=32 BM_MAX_TEXT=64` | 2,544 |

`BM_WITH_EFFECTS=0` is the largest single lever - `bm_synth` goes from 59,180
bytes to 544, because the echo and reverb delay lines are 58 KB between them.
After that it is `bm_frame_gen`, six arrays indexed by `BM_MAX_PHONEMES`, about
33 bytes per phoneme of buffered utterance.

Set `BM_ENGINE_RESERVED` too. It defaults to 131072, and `bm_engine_storage` is
a union of that size, so a firmware that leaves it alone reserves 128 KB of .bss
regardless of what the engine actually uses.

## Freestanding does not mean self-contained

`make check-freestanding` proves the core includes no `<stdio.h>`, `<stdlib.h>`,
`<math.h>` or `<string.h>`. That is true and worth keeping, and it does not mean
the core links without a libc.

A C compiler may emit calls to `memcpy`, `memmove`, `memset` and `memcmp` for
code that never names them - struct assignment, array initialisation, zeroing an
aggregate - and GCC does. A `-nostdlib` link therefore fails on undefined
symbols no matter how clean the source is.

Measured across the whole core, the complete external dependency is:

- **`memcpy` and `memset`.** Nothing else - no `memmove`, no `memcmp`.
- **libgcc**: soft-float helpers on parts without an FPU, `__aeabi_lmul` on
  ARMv6-M, and one `__aeabi_uldivmod` for the single 64-bit division in
  `bm_measure.c`. Hence `-lgcc` at the end of the link.

`qemu/shim.c` provides the two string functions for the harness. A real firmware
should use its SDK's versions. Check your own build rather than trusting this
list, since the set depends on compiler and optimisation level:

```sh
arm-none-eabi-nm -u *.o | sort -u
```

## The SMULL floor, and why it matters less than expected

`bm_fixed.h` accumulates Q18 products in `int64_t` and notes that this is one
instruction "on any ARM with SMULL, which is everything from the M3 up". Correct,
and confirmed in the disassembly - M3 emits a single `smlal` with the rounding
addend folded in, while ARMv6-M (Cortex-M0, M0+) falls back to a call to
`__aeabi_lmul` for every one of roughly fifty multiplies per sample.

This still matters, but it is second-order now that the sources are known to
dominate. An M0+ pays the `__aeabi_lmul` penalty on top of a fixed-point path
that was only buying 1.34x to begin with.

## Sample rate

`cfg.sample_rate` is free to lower and the engine stays well-behaved:

| Rate | Samples | Duration | RMS | Peak |
|---|---:|---:|---:|---:|
| 22050 | 103,405 | 4.690 s | 0.1006 | 0.5000 |
| 16000 | 75,200 | 4.700 s | 0.1022 | 0.4885 |
| 11025 | 51,702 | 4.690 s | 0.1019 | 0.5016 |
| 8000 | 37,600 | 4.700 s | 0.1004 | 0.4871 |

No instability, no clipping, duration preserved. 8 kHz buys 2.76x the cycle
budget of 22050 and costs the top of the spectrum - set `BM_NFORMANTS=3` below
about 10 kHz, and expect sibilants to suffer first. Comparison renders are in
`render/mcu-*.wav`.

The Q18 loop measures **52.1 dB SNR** against the float reference over a
14-second utterance, against the 52.9 dB `bm_fixed.h` claims for the format.

## Running the harness

```sh
embedded/qemu/run.sh              # all configurations
embedded/qemu/run.sh m3-fixed     # just one
OPT=-O2 RATE=8000 embedded/qemu/run.sh
```

Needs `qemu-system-arm`, `arm-none-eabi-gcc` and glib headers. Newlib and the
semihosting libraries are deliberately unused: the harness has its own vector
table, its own startup, and talks to the host through raw `bkpt #0xAB`, because a
test of a freestanding core should not link a libc to print its results.

Three failure modes cost a debugging round each and are commented in place, since
none of them announces itself:

- **QEMU reads stdin under `-nographic`.** Inside a `while read` loop it eats the
  remaining input and the loop resumes mid-line with its fields shifted. The
  symptom is configurations with garbled names failing to assemble.
- **The FPU is disabled at reset** on every Cortex-M that has one. A hard-float
  build faults on its first VFP instruction, lands in the fault handler's
  infinite loop, and runs until the timeout - hundreds of billions of
  instructions, no output, no error. `startup.c` sets CPACR first thing.
- **An absolute path containing a space** is shredded by the unquoted word
  splitting that assembling a compiler command line from a variable requires.
  `run.sh` does its work from the repo root using relative paths.

## Still unmeasured

Cycles, as above - QEMU counts instructions, and the conversion to megahertz in
the parts table is an estimate. A board and `DWT_CYCCNT` would settle it.

Cortex-M0+ has not been run, only compiled and disassembled. QEMU's `microbit`
machine is the available ARMv6-M target and its 16 KB of RAM fits only a trimmed
configuration, so it needs its own linker script rather than `mps2.ld`.

No real hardware, no DAC or I2S path, no interrupt-driven audio callback. The
harness renders into a buffer as fast as it can, which is the right way to count
instructions and says nothing about whether a real firmware meets its deadline.
