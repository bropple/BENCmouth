```
#####  #####  #   #  #####  #####
#   #  #      ##  #  #      #   #
#####  ####   # # #  #      #   #
#   #  #      #  ##  #      #   #
#####  #####  #   #  #####  #####

  B E N C O   H O L D I N G S

================================
```

# BENCmouth

[![build](https://github.com/bropple/BENCmouth/actions/workflows/ci.yml/badge.svg)](https://github.com/bropple/BENCmouth/actions/workflows/ci.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-78b946)](LICENSE)

A formant speech synthesizer in C99. Text goes in, speech comes out.

It is an original work in the spirit of S.A.M. (Software Automatic Mouth) — not a port,
a decompilation, or a transliteration of it. The synthesis is built from the published
literature on cascade/parallel formant synthesis; the letter-to-sound rules come from a
public-domain 1976 US Naval Research Laboratory report. See `ref/README.md` for the full
provenance.

**No dynamic allocation. No libm in the core. No I/O below the host layer.** The engine
holds about 8.5 KB of state in caller-supplied storage, which means it drops into a
microcontroller, an audio callback, or a WASM module unchanged.

---

## Quick start

```
make
./bm "Hello world" -o hello.wav
```

That is the whole thing. No configure step, no dependencies beyond a C compiler and the
system maths library.

---

## Building

The build is a plain GNU makefile with no external dependencies. Every platform below
produces the same `bm` executable plus `libbencmouth.a`.

### Linux

```
sudo apt install build-essential      # Debian/Ubuntu
sudo pacman -S base-devel             # Arch
sudo dnf groupinstall "Development Tools"   # Fedora

make
```

### macOS

```
xcode-select --install    # if you have not already
make
```

Apple's `make` is GNU make and `cc` is clang; both work as-is. If you prefer Homebrew GCC,
`make CC=gcc-14` also works.

### Windows

The makefile needs GNU make, so use one of:

**MSYS2 / MinGW-w64 (recommended)**

```
pacman -S mingw-w64-ucrt-x86_64-gcc make
mingw32-make
```

Run from the *UCRT64* shell, not the MSYS shell, so you get a native Windows binary
rather than one linked against the MSYS runtime.

**MSVC**, without make — the source list is short enough to compile directly. From a
*Developer Command Prompt*:

```
cl /std:c11 /O2 /Iinclude /Isrc\core /Isrc\host /Fe:bm.exe ^
   src\core\*.c src\host\*.c
```

MSVC needs `/std:c11` (or `/std:c17`) rather than a C99 flag; its C99 mode is not
selectable and the default is too old for `stdint.h` usage in the public header.

> **These are verified, not assumed.** CI builds and runs the full test suite on
> `ubuntu-latest`, `macos-latest` and `windows-latest` (MSYS2/UCRT64) on every push, plus a
> strict pass with `-Werror` and one under AddressSanitizer and UBSan. If a documented
> build path breaks, that is a bug rather than a limitation.

### Make targets

| Target | Result |
|---|---|
| `make` | library, `bm`, and both demos |
| `make lib` | `libbencmouth.a` only |
| `make bm` | the CLI only |
| `make test` | run all five test suites |
| `make check-freestanding` | assert the core includes no hosted headers |
| `make clean` | remove build products |

---

## Using the CLI

```
bm [options] "text to speak"

  -o FILE      write WAV here (default out.wav; - for stdout)
  -v NAME      use a voice preset
  -f FILE      load a voice file
  -s SPEED     speech rate; 1.0 nominal, 2.0 twice as fast
  -p PITCH     base pitch in Hz
  -P           input is ARPABET phonemes, not text
  -t           print phonemes and exit; render nothing
  -w FILE      write the resolved voice to a voice file and exit
  -l           list voice presets
  -r RATE      sample rate (default 22050)
  -h           help
```

```
bm "Hello world" -o hello.wav
bm -v retro -s 0.8 "I am sorry Dave" -o dave.wav
bm -P "HH AH0 L OW1" -o hello.wav          # phonemes directly
bm -t "the quick brown fox"                # see what the rules produced
bm -f voices/gravel.voice "testing" -o t.wav
```

### Playing it immediately

There is no built-in audio output yet, but `-o -` writes a WAV to stdout:

```
bm "hello world" -o - | aplay              # Linux/ALSA
bm "hello world" -o - | paplay             # Linux/PulseAudio
bm "hello world" -o - > /tmp/x.wav && afplay /tmp/x.wav   # macOS
```

---

## Voices

Five presets ship in the binary:

| Voice | Character |
|---|---|
| `BENCmouth` | the default — Retro with coarticulation on |
| `BENCmouth Retro` | **the original voice, pinned** (see below) |
| `BENCmouth Monotone` | no flutter, no intonation; maximum machine |
| `Deep` | a larger speaker — longer vocal tract, not just lower pitch |
| `Bright` | a smaller speaker |

Names match loosely, so `-v retro`, `-v "BENCmouth Retro"` and `-v bencmouth-retro` all
resolve.

### Voice files

Voices are plain text and live in `voices/`:

```
# BENCmouth voice
name           = Gravel
preset         = retro      # start from a preset, then override

f0_base        = 96
f0_flutter     = 0.45
throat         = 0.88       # governs F1 - the pharyngeal cavity
mouth          = 0.94       # governs F3 and up - the oral cavity
tilt           = 9
open_quotient  = 0.58
gain           = 1.0
coarticulation = 0          # 0 = hits every target exactly, the retro sound
```

Unknown keys are **errors**, not warnings — a silently dropped setting produces a voice
that mysteriously sounds wrong, which is far worse to debug than a refusal to load.

Dump any voice with `bm -v deep -w mine.voice`, edit, and load it back with `-f`.

### The Retro contract

`BENCmouth Retro` is the original voice and it does not drift. Every naturalness
improvement arrives as a voice parameter whose *off* setting reproduces the older, cruder
behaviour, and Retro leaves them off. A feature that cannot be switched off is a feature
that has silently taken the retro voice away.

`tests/test_voices.c` enforces this with a golden reference. See `ARCHITECTURE.md`.

---

## Using the library

The public API is `include/bencmouth.h` — one header, freestanding-safe, no I/O.

```c
#include "bencmouth.h"

bm_engine_storage storage;      /* stack, .bss, a pool - your choice */
bm_engine        *engine;
bm_config         config;
float             buf[1024];

bm_config_default(&config);
bm_engine_init(&storage, &config, &engine);
bm_speak_text(engine, "hello world", 0);

while (bm_is_speaking(engine)) {
    size_t n = bm_read(engine, buf, 1024);
    /* hand buf to your audio device */
}
```

`bm_read()` is a pull interface on purpose: every real audio API is "here is a buffer,
fill it now, don't block", and this satisfies that with no ring buffer and no thread. It
never allocates and never blocks, so it is safe to call from an audio callback.

Link against `libbencmouth.a` and `-lm` (the maths library is only needed by the host
layer; the core has its own).

### Embedded builds

Compile only `src/core/*.c`. It includes no `<stdio.h>`, `<stdlib.h>`, `<math.h>` or
`<string.h>` anywhere — `make check-freestanding` asserts that, and it is worth running in
CI because the microcontroller build is what breaks first if it stops being true.

Tunables, all overridable with `-D`:

| Macro | Default | Effect |
|---|---|---|
| `BM_MAX_PHONEMES` | 512 | phonemes buffered per utterance |
| `BM_MAX_TEXT` | 1024 | bytes of text buffered |
| `BM_NFORMANTS` | 5 | drop to 3 below ~10 kHz output |
| `BM_SAMPLE_FLOAT` | 1 | set 0 for `int16_t` output |
| `BM_ENGINE_RESERVED` | 65536 | storage union size; actual use is ~8.5 KB |

---

## Repository layout

```
include/bencmouth.h   the entire public API
src/core/             no stdio, no stdlib, no malloc, no libm
src/host/             platform glue - CLI, WAV writer, voice files
tools/                generators and demos (see ARCHITECTURE.md)
tests/                five suites, run with `make test`
voices/               shareable voice files
ref/                  source material and its provenance - read ref/README.md
render/               generated audio; gitignored
```

`ARCHITECTURE.md` covers the signal path and the design decisions worth defending.
`ROADMAP.md` covers what is done and what is not.

---

## License

MIT — see `LICENSE`. Do what you like with it; keep the copyright notice. That is the only
condition, and it is there because Ben thought of it.

The repository also contains third-party material with its own terms, listed in `NOTICE`:
the CMU Pronouncing Dictionary is 2-clause BSD and its notice must be reproduced in binary
redistributions, and the NRL letter-to-sound rules are US federal government work and
therefore public domain. If you redistribute BENCmouth in any form, ship `NOTICE` alongside
it and you have covered both.

---

## Status

Working: the synthesizer, the phoneme inventory, frame interpolation, the text front end,
voices, the CLI.

Not yet: live audio output, CMUdict (the letter-to-sound rules are about 90% accurate on
running text, and the ~10% they miss is what a dictionary is for), and real prosody — F0
currently does declination and a stress bump and nothing else.
