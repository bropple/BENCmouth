A formant speech synthesizer in C99. Text in, speech out.

An original work in the spirit of S.A.M., built from the published literature on
cascade/parallel formant synthesis rather than from any existing implementation.
See `ref/README.md` for provenance, including the sources deliberately not consulted.

## What is in the box

| Archive | Contains |
|---|---|
| `linux-x86_64.tar.gz` | `bm` CLI with live audio and CMUdict, plus the GUI and its font |
| `macos.tar.gz` | `bm` CLI with live audio and CMUdict |
| `windows-x86_64.zip` | `bm.exe` with live audio and CMUdict |
| `wasm.tar.gz` | `bencmouth.wasm` and its JavaScript wrapper |

```
./bm "Hello world" -o hello.wav
./bm -a "straight out of the speakers"
./bm -v retro -s 0.8 "I am sorry Dave"
./bm -m -P "[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0"   # it sings
```

The Linux archive includes `bencmouth-gui`. Run it from the archive directory so it
finds `assets/fonts/`.

## Notable

- **No dynamic allocation, no libm in the core, no I/O below the host layer.** The
  engine holds ~15 KB of state in caller-supplied storage.
- **BENCmouth Retro is pinned.** Every naturalness feature is a voice parameter whose
  off setting reproduces the older behaviour, and a golden-reference test holds the
  original voice to it.
- **`-DBM_FIXED_POINT=1`** runs the sample loop in Q18 integer arithmetic for targets
  without an FPU — 52.9 dB SNR against the float reference.
- The `wasm32` build is bare: no libc, no Emscripten runtime.

## Licensing

MIT for the source. `NOTICE` covers the third-party material and must ship with any
redistribution: the CMU Pronouncing Dictionary is 2-clause BSD and requires its notice
in binary form, the NRL letter-to-sound rules are US federal government work and public
domain, and the GUI bundles Terminus (TTF) under the SIL Open Font License.
