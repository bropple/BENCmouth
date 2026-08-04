A formant speech synthesizer in C99. Text in, speech out.

An original work in the spirit of S.A.M., built from the published literature on
cascade/parallel formant synthesis rather than from any existing implementation.
See `ref/README.md` for provenance, including the sources deliberately not consulted.

## What is in the box

Two archives per desktop platform. The CLI on its own is small and has no icon,
because nobody double-clicks a console program. The GUI is a separate download.

| Archive | Contains |
|---|---|
| `bencmouth-VERSION-PLATFORM` | the `bm` CLI, with live audio and the 124,910-word CMU dictionary |
| `bencmouth-gui-VERSION-PLATFORM` | `bencmouth-gui` and the CLI beside it |
| `bencmouth-VERSION-wasm` | `bencmouth.wasm` and its JavaScript wrapper |

```
./bm "Hello world" -o hello.wav
./bm -a "straight out of the speakers"
./bm -v retro -s 0.8 "I am sorry Dave"
./bm -m -P "[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0"   # it sings
```

**The GUI is a single self-contained executable.** The font, the window icon, the
wordmark and the licence texts are compiled into it, so it can be put anywhere and
started from anywhere and still look right. There is no assets folder to keep beside
it. The ⓘ button in the top right shows the copyright and every licence, read out of
the binary.

## Notable

- **No dynamic allocation, no libm in the core, no I/O below the host layer.** The
  engine holds ~15 KB of state in caller-supplied storage.
- **BENCmouth Retro is pinned.** Every naturalness feature is a voice parameter whose
  off setting reproduces the older behaviour, and a golden-reference test holds the
  original voice to it.
- **The dictionary is a switch, not a rebuild.** The DICT button turns it off so you
  can hear the letter-to-sound rules alone — which is what a build for a
  microcontroller has, and how you find the words worth an exception.
- **`-DBM_FIXED_POINT=1`** runs the sample loop in Q18 integer arithmetic for targets
  without an FPU — 52.9 dB SNR against the float reference.
- The `wasm32` build is bare: no libc, no Emscripten runtime.

## Licensing

MIT for the source. `NOTICE` covers the third-party material and must ship with any
redistribution: the CMU Pronouncing Dictionary is 2-clause BSD and requires its notice
in binary form, the NRL letter-to-sound rules are US federal government work and public
domain, the GUI embeds Terminus (TTF) under the SIL Open Font License, and it links
raylib under the zlib licence. The GUI carries all of these inside the executable and
displays them, which is what makes shipping it as a lone file permissible.
