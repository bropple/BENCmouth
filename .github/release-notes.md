A formant speech synthesizer in C99. Text in, speech out.

An original work in the spirit of S.A.M., built from the published literature on
cascade/parallel formant synthesis rather than from any existing implementation.
See `ref/README.md` for provenance, including the sources deliberately not consulted.

## New in 0.2.1

**Song mode's tempo is a control, and it now does something.** It was displayed and
inert — nothing outside the readout read it. It means one thing now: the tempo the
score's `[hold]` values are written at. Change it and every `[hold]` is rewritten by the
same ratio, so the song genuinely speeds up or slows down and the file stays honest — its
header and its holds always agree. There is a slider for the quarter note in milliseconds
too, since that is the number you actually type into a `[hold]`; it and the BPM are one
value in two units.

Songs do not scale exactly with it, because consonants keep their own length. Daisy Bell
runs 11.9 s at 116 BPM and 9.9 s at 160. That is what singing does as well — a note's
duration lives in its vowel, which is why `[hold]` only touches vowels.

**`make all` builds everything**: dictionary, live audio, GUI and wasm, each in its best
configuration, testing for raylib, clang and the ALSA headers rather than assuming them
and reporting whatever it skips. A plain `make` is unchanged and still needs nothing but
a C compiler.

**Fixed:** the minimum window size had been about twelve pixels shorter than the layout
needed, so at the smallest size the status line — the one row that says what the program
is doing — was cut off the bottom.

## New in 0.2.0

It sings, it has an effects rack, and there are twenty-five voices instead of ten.

**Song mode.** A second tab in the GUI: a score of phonemes with `[note]` and `[hold]`
in it, a word-to-phoneme translator with an INSERT button so you do not have to know
ARPABET by heart, a reference for the notation, and save/load of `.bmsong` files. Each
tab keeps its own voice, because singing wants prosody off and a little vibrato and
speech wants the opposite. On the command line, `bm -S songs/daisy.bmsong -a`.

**A post-synthesis effects stage**, kept in its own struct beside the voice rather than
bolted onto it — an effect is not a property of a speaker, and keeping them apart is what
lets any chain go on any voice. Nine controls: ring modulation with a slowly drifting
carrier, a resonant comb, a three-tap chorus, drive, sample-rate crush, echo, reverb, and
an output level. Fifteen presets from `Metal` to `Hall`. All of it compiles away under
`-DBM_WITH_EFFECTS=0`, and an all-zero chain is a bit-for-bit bypass, which is what lets
the pinned voice survive a new stage in the signal path.

**Twenty-five voices.** The classic-era set — deep, professional, child, whispering,
unstable — plus ten that carry an effects chain and arrive with it: `Zarvek`, `Sentry`,
`Aggressor`, `Carillon`, `Harmonium`, `Diver`, `Foreman`, `Emissary`, `Trinode`, `Gravel`.
`CLASSIC-VOICES.md` covers what the engine can reach, what it cannot, and why.

**The excitation is selectable.** `source` crossfades the vocal folds into a harmonic
pipe or the measured partial series of a church bell. The vocal tract is untouched, so a
bell-sourced voice still has formants and still says the words — which is exactly what
the classic instrument voices were.

**New voice parameters**, all defaulting to off: `whisper` (voicing traded for
turbulence), `vibrato` / `vibrato_rate`, `flatten` (one pitch for the whole utterance —
`BENCmouth Monotone` was not actually monotone before this), and `source`.

**The GUI grew up.** Sliders whose readouts you can type into, with arrows that step by
the precision shown. A voice list that is sorted and scrolls. An effects column that
scrolls. Selectable, copyable read-only boxes. And a meter that shows RMS as well as
peak, because this synthesizer routinely makes them disagree — `Aggressor` peaks four
times lower than `Gravel` and is a decibel *quieter*, since drive collapses the crest
factor. `bm` reports both figures too.

**Voice files are now `.bmvoice`**, matching `.bmsong`. Contents are unchanged and the
loader never looked at the extension, so anything you saved under the old name still
loads with `-f`.

## What is in the box

Two archives per desktop platform. The CLI on its own is small and has no icon,
because nobody double-clicks a console program. The GUI is a separate download.

| Archive | Contains |
|---|---|
| `bencmouth-VERSION-PLATFORM` | the `bm` CLI, with live audio and the 124,910-word CMU dictionary |
| `bencmouth-gui-VERSION-PLATFORM` | `bencmouth-gui` and the CLI beside it (macOS: a `.dmg`, drag to Applications) |
| `bencmouth-VERSION-wasm` | `bencmouth.wasm` and its JavaScript wrapper |

```
./bm "Hello world" -o hello.wav
./bm -a "straight out of the speakers"
./bm -v retro -s 0.8 "I am sorry Dave"
./bm -m -P "[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0"   # it sings
```

**On macOS the GUI is a `.dmg`.** Mount it and drag BENCmouth to Applications. Inside is
`BENCmouth.app` — a bundle, because that is the only way a macOS program gets an icon:
a bare Mach-O executable has nothing to attach one to and GLFW's Cocoa backend ignores
the request. The bundle is a thin wrapper; `Contents/MacOS` holds one file.

macOS binaries here are unsigned in the sense that matters to Apple — there is no
Developer ID and no notarization — so Gatekeeper will stop the first launch. The app *is*
ad-hoc signed, which is what keeps the message honest: you should see "Apple could not
verify..." with an Open Anyway button under System Settings → Privacy & Security, not
"damaged and can't be opened".

If you do see **damaged**, the signature failed to validate rather than merely being
untrusted, and either of these fixes it:

```
xattr -dr com.apple.quarantine /Applications/BENCmouth.app   # drop the download flag
codesign --force --sign - /Applications/BENCmouth.app        # or re-sign it yourself
```

Both work on the CLI too (`./bm`). Nothing here can remove the prompt entirely — that
needs a paid Developer ID and notarization through Apple.

**The GUI is a single self-contained executable.** The font, the window icon, the
wordmark and the licence texts are compiled into it, so it can be put anywhere and
started from anywhere and still look right. There is no assets folder to keep beside
it. The ⓘ button in the top right shows the copyright and every licence, read out of
the binary.

## Notable

- **No dynamic allocation, no libm in the core, no I/O below the host layer.** The
  engine holds all its state in caller-supplied storage: 19 KB without the effects
  stage, 75 KB with it, since the echo and the reverb are delay lines.
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
