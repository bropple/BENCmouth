<p align="center">
  <img src="assets/brand/BENCO_Logo_README.png" alt="BENCO Holdings" width="420">
</p>

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
holds all its state in caller-supplied storage — 19 KB without the effects stage, 75 KB
with it, since two of those effects are delay lines — which means it drops into a
microcontroller, an audio callback, or a WASM module unchanged.

![the BENCmouth GUI](assets/gui.png)

**[Download a build](https://github.com/bropple/BENCmouth/releases/latest)** for Linux,
macOS, Windows or the browser — or `make` it, which takes about two seconds.

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
| `make audio` | compile live playback in and rebuild |
| `make wasm` | build `bencmouth.wasm` (needs clang and lld) |
| `make gui` | build the desktop GUI (needs raylib) |
| `make dict` | compile CMUdict in and rebuild (see below) |
| `make test` | run all ten test suites |
| `make check-freestanding` | assert the core includes no hosted headers |
| `make clean` | remove build products |

---

## Using the CLI

```
bm [options] "text to speak"

  -o FILE      write WAV here (default out.wav; - for stdout)
  -v NAME      use a voice preset
  -e NAME      use an effects preset
  -f FILE      load a voice file
  -R SEED      generate a random voice from SEED
  -s SPEED     speech rate; 1.0 nominal, 2.0 twice as fast
  -p PITCH     base pitch in Hz
  -P           input is ARPABET phonemes, not text
  -S FILE      sing a .bmsong
  -m           enable inline markup
  -t           print phonemes and exit; render nothing
  -w FILE      write the resolved voice to a voice file and exit
  -l           list voice and effects presets
  -r RATE      sample rate (default 22050)
  -h           help
```

```
bm "Hello world" -o hello.wav
bm -v retro -s 0.8 "I am sorry Dave" -o dave.wav
bm -P "HH AH0 L OW1" -o hello.wav          # phonemes directly
bm -t "the quick brown fox"                # see what the rules produced
bm -f voices/Gravel.bmvoice "testing" -o t.wav
bm -S songs/daisy.bmsong -a                # sing a song
bm -v deep -e enforcer "you have thirty seconds to comply"
```

Each render reports on stderr:

```
out.wav  3.00 s  peak 0.414  rms -19.3 dB  [BENCmouth]
```

Both level figures, because they answer different questions and this synthesizer makes
them disagree. Peak is against the limiter, which engages at 0.85 and adds `LIMITED` to
that line when it does. RMS is loudness. `Aggressor` peaks at 0.124 where `Gravel` peaks
at 0.527 — four to one — and is *one decibel quieter*, because drive and crush collapse
the crest factor. Render a dozen voices through the same sentence and read down the
column; the peak column will lie to you and the rms column will not.

### Inline markup

Off unless you ask for it with `-m`, because with it off `[pitch 70]` is ordinary text
and gets spoken as the words "pitch seventy" — turning brackets into commands silently
would swallow text for anyone who did not opt in.

```
bm -m "normal speed. [speed 0.7] now slower. [pitch 160] and higher."
bm -m "wait for it [pause 800] there it is"
bm -m "[pitch 70] low ... [reset] and back to normal"
```

| Command | Effect |
|---|---|
| `[pitch N]` | base pitch in Hz, 20–500, for everything after it |
| `[speed X]` | rate multiplier, 0.1–10 |
| `[pause N]` | N milliseconds of silence, inserted here (max 10000) |
| `[note NAME]` | absolute pitch by name — `C4`, `A#3`, `Bb5`, `G` |
| `[hold N]` | length in ms of the vowels that follow |
| `[reset]` | back to the voice's own settings |

`[note]` and `[hold]` are what make it sing. `[note]` is absolute where `[pitch]` is a
transposition — a sung note should be that pitch, not that pitch plus whatever accent the
prosody planner had in mind. `[hold]` applies only to vowels, because that is where a
note's duration lives; stretching the consonants turns the word into a groan.

```
bm -m -P "[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0 ..." -o daisy.wav
```

A whole song is a `.bmsong` file — see [Songs](#songs) below.

Commands survive into the phoneme string rather than being resolved away, so `bm -m -t`
shows exactly what the synthesizer will act on, and `-P` input honours them too:

```
$ bm -m -t "hello [pitch 70] world"
HH EH L OW [pitch 70] W ER L D
```

A malformed command is an error, not a guess — `[pitch]`, `[wobble 3]` and an unterminated
bracket all fail loudly rather than being quietly ignored.

Compile with `-DBM_WITH_MARKUP=0` to remove the parser entirely; `-m` then does nothing and
brackets stay ordinary characters.

### Playing it immediately

```
make audio
./bm -a "straight out of the speakers"
```

`make audio` picks a backend from `uname`: **ALSA** on Linux (`-lasound`), **AudioQueue**
on macOS, **waveOut** on Windows. `bm -h` prints which one is compiled in.

Optional for the same reason the dictionary is — a plain `make` should need nothing but a
C compiler, and ALSA headers are a package a first-time builder should not have to hunt
for. Without it, `-a` says so rather than failing to build.

Playback streams straight from `bm_read()` to the device with no buffer of the whole
utterance in between, which is the pull interface doing exactly what it was shaped for.
Limiting is shared with the WAV writer, so a file is a faithful record of what you heard.

`-o -` still writes a WAV to stdout if you would rather pipe it:

```
bm "hello world" -o - | aplay              # Linux/ALSA
bm "hello world" -o - | paplay             # Linux/PulseAudio
bm "hello world" -o - > /tmp/x.wav && afplay /tmp/x.wav   # macOS
```

---

## Voices

Twenty-five presets ship in the binary:

| Voice | Character |
|---|---|
| `BENCmouth` | the default — Retro with coarticulation on |
| `BENCmouth Retro` | **the original voice, pinned** (see below) |
| `BENCmouth Monotone` | no flutter, no intonation; maximum machine |
| `Deep` | a larger speaker — longer vocal tract, not just lower pitch |
| `Bright` | a smaller speaker |
| `Compact` | the all-in-one machine on the desk, 1984 |
| `Announcer` | deep and resonant |
| `Operator` | the professional female voice of the era |
| `Cadet` | a child |
| `Hushed` | no voicing at all — turbulence through the formants |
| `Rattled` | the one that came loose |
| `Frantic` | fast, high and unstable — far too much of something |
| `Grizzled` | old and worn: heavy flutter and a leaky glottis, not just a low pitch |
| `Frederick` | the ordinary default male voice the novelty ones were novelties against |
| `Duchess` | light and high — breathier than `Operator`, and quicker |

Ten more are a voice **and** an effects chain, and `-v` brings both:

| Voice | Chain | Character |
|---|---|---|
| `Aggressor` | `Enforcer` | a machine that has decided about you |
| `Carillon` | `Chamber` | a bell made to speak — bell partials for a source |
| `Diver` | `Downlink` | underwater, or through a helmet |
| `Emissary` | `Alloy` | the polite machine: ring modulation over no intonation at all |
| `Foreman` | `Bullhorn` | somebody shouting over machinery |
| `Gravel` | — | a rougher, lower Retro; the one here that wants no chain |
| `Harmonium` | `Cabinet` | reed organ — a pipe for a source, and no intonation |
| `Sentry` | `Sentinel` | something standing at a door |
| `Trinode` | `Trinode` | three of it, slightly out with each other |
| `Zarvek` | `Klaxon` | the harsh one |

These lived only as `.bmvoice` files for a while, on the reasoning that an effect is not part
of a voice. That was true and still the wrong call — the GUI dropdown lists presets, so a
voice that is only a file is a voice most people never find. The files stay: they carry the
working-out for each one, and `tests/test_voicefile.c` holds them to matching the preset
exactly, chain included, so the two copies cannot drift.

A `-e` *after* a `-v` overrides the chain, and a chain can be put on anyone —
`bm -v cadet -e klaxon` is a perfectly horrible thing you can now do.

The classic set is aimed at the voices desktop machines shipped with in
the eighties and early nineties. They are tuned toward those archetypes from the published
acoustics of the voice types involved — nothing was disassembled or lifted from a shipped
synthesizer, which is the same rule the rest of this project follows.

[CLASSIC-VOICES.md](CLASSIC-VOICES.md) has the tuning, the measured formant placements,
and — more usefully — the list of classic voices this engine **cannot** reach, with the
architectural reason for each. The short version: the formant voices are reachable, the
instrument-source ones are not without a pluggable excitation source, and the recorded
ones are a different kind of program entirely.

```
make bm && sh tools/classic-voices.sh    # render them all for comparison
```

Names match loosely, so `-v retro`, `-v "BENCmouth Retro"` and `-v bencmouth-retro` all
resolve.

### Voice files

Voices are plain text, carry the extension **`.bmvoice`**, and live in `voices/`. The
prefix is deliberate and matches `.bmsong`: `.voice` is a common enough word to collide
with something else on a shared machine, and a file type worth double-clicking is worth
owning a name for. Nothing in the loader cares about the extension — it opens whatever
path it is given — so a file saved under the old name still loads.

```
# BENCmouth voice
name           = Gravel
preset         = retro      # start from a preset, then override

f0_base        = 96
f0_flutter     = 0.45       # aperiodic drift; kills the buzz, meant not to be heard
vibrato        = 0          # semitones of *periodic* pitch modulation; meant to be
vibrato_rate   = 0          # Hz; 0 selects the default, about 5.5
throat         = 0.88       # governs F1 - the pharyngeal cavity
mouth          = 0.94       # governs F3 and up - the oral cavity
tilt           = 9
open_quotient  = 0.58
whisper        = 0          # 1 = no voicing at all, turbulence through the formants
gain           = 1.0
coarticulation = 0          # 0 = hits every target exactly, the retro sound
prosody        = 0          # 0 = the pre-bm_prosody.c pitch contour
formant_glide  = 0          # 0 = linear in Hz, 1 = geometric (as heard)
bandwidth_track= 0          # 0 = table bandwidths, 1 = scaled by formant frequency
```

Unknown keys are **errors**, not warnings — a silently dropped setting produces a voice
that mysteriously sounds wrong, which is far worse to debug than a refusal to load.

Dump any voice with `bm -v deep -w mine.bmvoice`, edit, and load it back with `-f`.

`whisper` and `vibrato` are worth calling out because each is easily confused with a
neighbour:

- **`source` decides what excites the tract**, 0..2: vocal folds, a harmonic pipe
  stack, or the inharmonic partials of a struck bell — crossfading, so 1.5 is a pipe
  with a bell's inharmonicity creeping in. The tract is untouched either way, so a
  bell-sourced voice still has formants and still says the words. That is what the
  classic instrument voices were: not an instrument playing instead of speech, but an
  instrument doing the speaking. The bell ratios are a real church bell's, and the
  tierce at 1.19 — a minor third, and a harmonic of nothing — is why it reads as a bell
  rather than a detuned chord.
- **`flatten` is what "monotone" actually needs.** `f0_range` is documented as
  "0 = monotone robot" and for a voice using the phrase planner it is — but a voice with
  `prosody` at 0 uses the older contour, which read `f0_range` not at all and applied its
  declination and its stressed-syllable bump regardless. BENCmouth Monotone swung **28.5%
  in pitch**, 4.3 semitones, the same spread as BENCmouth Retro, and stretched stressed
  vowels to 1.61× the length of unstressed ones. At `flatten = 1` the pitch is one number
  for the whole utterance and every vowel takes its nominal length whatever stress it
  carries. An absolute `[note]` is exempt, so song mode still works.
- **`whisper` is not `breathiness`.** Breathiness *adds* aspiration alongside phonation and
  leaves the vocal folds working. Whispering is not breathy speech — the folds do not
  vibrate at all, so there is no fundamental, and the formants are excited by glottal
  turbulence instead. The voiced/voiceless distinction goes with it: a whispered *bat*
  and *pat* really are near-identical, and that is the effect, not a defect in it.
- **`vibrato` is not `f0_flutter`.** Flutter is three incommensurate oscillators summed so
  the pattern never repeats; it exists to stop a sustained vowel sounding like a test tone
  and you are not supposed to notice it. Vibrato is one oscillator you *are* supposed to
  notice, and it is what a held note needs to read as sung rather than beeped. Depth is
  in semitones because that is how the excursion is heard — a 3 Hz wobble is enormous at
  80 Hz and inaudible at 400.

Both default to 0 in every preset, so BENCmouth Retro is unchanged by their existence —
which is the contract in the section below, working as intended.

### Finding new voices

```
bm -R 4242 "hello there"          # random, but the same every time for that seed
bm -R 4242 -w found.bmvoice         # keep one you liked
```

The parameter space is large and mostly uninteresting, and the good corners of it turn up
by accident more often than by reasoning. Draws are deterministic from the seed, and gain
is trimmed by flutter rather than drawn at random — low-flutter voices stack peaks, and
without that roughly a third of them clipped.

### Intonation

Voices with `prosody` above 0 get phrase-level contours rather than one long decline:
each phrase starts fresh, stressed syllables get pitch accents, the phrase-final syllable
is lengthened, and the last third of a phrase moves according to how it ends.

```
$ bm -t "Hello there. Are you awake?"
HH EH L OW DH EH R . AA R Y UW AH W EY K ?
```

Punctuation becomes a boundary phoneme rather than generic silence — a question and a
statement used to both become `SIL SIL`, which discarded the only thing a contour planner
could act on. Planned pitch for that sentence, in Hz:

| ending | contour |
|---|---|
| `.` statement | rises to 133 on the stressed vowel, falls to **83** |
| `?` question | rises to **149** |
| `,` continuation | settles at **115** — unfinished, but not a question |

`f0_range` sets how far the contour swings, in semitones. `prosody` at 0 restores the
older behaviour exactly: one linear decline across the whole utterance and a flat bump on
stressed phonemes. BENCmouth Retro and Monotone sit at 0.

---

## Effects

A stage after the synthesizer, and a **separate struct** from the voice. That
separation is the design rather than an implementation detail:

> A voice is a claim about a speaker — the length of their throat, how their vocal
> folds close, how hard they push. An effect is something done to the sound
> afterwards. Ring modulation is not a property of anyone's larynx.

Keeping them apart is also what makes them compose. Any effect works on any voice, so
a metallic ring on the deep voice and the same ring on the child are one flag apart,
instead of being two more entries in a preset table that would have to hold every
combination.

```
bm -l                                             # lists effects and voices
bm -v deep -e enforcer "you have thirty seconds to comply"
bm -v cadet -e metal "resistance is useless"
```

| Effect | What it is | What it is for |
|---|---|---|
| `ring` / `ring_hz` | multiply by a sine carrier | inharmonic sidebands — the metallic, inhuman edge |
| `ring_drift` | the carrier wanders, slowly | keeps the ring from going stale on a voice that never varies — see below |
| `comb` / `comb_hz` | feedback comb, evenly spaced resonances | speaking through a metal tube, literally |
| `chorus` / `chorus_hz` | three delay taps swept by an LFO | three detuned copies — a *moving* delay is a pitch shift, a fixed one is not |
| `drive` | cubic soft clip with pre-gain | harmonics that were not there — this is what *aggressive* is |
| `crush` | hold every Nth sample | aliasing; the sound of a converter that could not keep up |
| `echo` / `echo_ms` | one delayed copy, fed back | repeats — the same mechanism as the comb, at a length the ear separates |
| `reverb` / `reverb_size` | four combs into two allpasses | a room: many arrivals rather than a few countable ones |
| `level` | output gain | see below |

The chain runs **ring → comb → chorus → drive → crush → echo → reverb**, and the order is
deliberate:
ring modulation on the clean voice keeps its sidebands distinct, the comb adds the
resonance, the chorus multiplies whatever has been built into several detuned copies,
and the drive then saturates the lot — which is what makes a robot sound angry rather
than merely mechanical. Crush ends the signal path because it is the digital layer, applied to a
finished sound. Chorus goes *before* drive because modulation after distortion smears
the harmonics the distortion just made; every guitar rig is wired the same way.

Echo and reverb come after all of it, because they are not done to the voice but to the
space around it. Distorting an echo squashes the repeats up against the dry signal until
the tail is as loud as the words; driving a reverb pumps the whole room in time with the
syllables. A voice goes into a room — a room does not go into a voice.

**Echo and comb are the same mechanism.** The only difference is length, and the change is
in the listener rather than in the signal: below about 30 ms the ear fuses the copy with
the original and hears a resonance, above about 80 ms it separates them and hears a
repeat. Two controls rather than one because a single delay knob spanning both would spend
most of its travel in the gap between them.

**Reverb is not a long echo.** An echo gives one repeat per delay time; the reverb gives
four at once, at lengths chosen to share no factors, each then smeared through allpasses
that spread an impulse in time without colouring it. The repeats multiply rather than add,
so within a few hundred milliseconds they stop being countable — which is what a room does
and what an echo never does. Measured on a single impulse: the reverb's response changes
sign 1629 times where the echo's changes 25.

Fifteen presets ship:

| Preset | |
|---|---|
| `None` | the bypass — and an *exact* one, bit for bit |
| `Metal` | ring modulation alone; the effect that most obviously is not a person |
| `Overdrive` | the waveshaper alone |
| `Crushed` | sample-rate reduction alone |
| `Sentinel` | the metallic sentry — inhuman rather than angry |
| `Enforcer` | the aggressive one; drive carries it |
| `Trinode` | three detuned copies of the same voice |
| `Chamber` | a small resonant body; one comb, spaced to miss the formants |
| `Downlink` | a voice arriving over a bad link |
| `Alloy` | ring modulation almost to the exclusion of the dry signal |
| `Bullhorn` | amplified and slightly broken |
| `Cabinet` | a low comb and nothing else — the case an instrument sits in |
| `Klaxon` | the harsh one; sidebands close enough to the pitch to beat against it |
| `Hall` | a large room and nothing else |
| `Canyon` | outdoors and a long way from anything: slow echo over a big room |

The last six arrived as part of a voice — see the pairings in the voice table above — and
are listed here as well because a chain is not specific to the voice it was built for.
A `.bmvoice` file can carry an `effects = NAME` line and any of the keys above.

**Why `ring_drift` exists.** Reported as "over a long sentence the Metal effect kind of
peters out" on `BENCmouth Monotone`. Measured over 5–20 second utterances of four
different shapes, it does not: sideband-to-harmonic energy, RMS and spectral centroid are
all flat end to end. The signal is not weakening — but on that voice *nothing else is
changing either*. Pitch is pinned at exactly 120 Hz, there is no flutter and no
intonation, and the carrier is fixed, so the output is strictly periodic and never
presents the ear with anything it has not already had. That is the stimulus attention
withdraws from.

So the fix is not more ring, it is a carrier that will not hold still: one cycle every
eight seconds, wider than any simple ratio against the fundamental, so the sideband
pattern is somewhere new every time you notice it. `0` is off and is exactly the old
behaviour, which is what keeps every shipped chain sounding as it did.

**Why `level` exists.** Voice `gain` is applied *before* the chain, which is correct:
`drive` is a threshold effect, and a drive stage that saw an untrimmed signal would
fold hard on a loud voice and do nothing on a quiet one. But that same ordering makes
the gain slider almost inert once drive is up — turning it up just drives harder into
a shaper that is already saturating. So: input trim, chain, output level, which is the
topology of every overdrive pedal ever built. A `level` of 0 means unity, not silence,
which is what keeps an all-zero `bm_effects` an exact bypass.

Every preset is level-matched to within 1.7 dB of the dry signal, because a knob
labelled with a timbre should not also be a volume control. Getting that right needed
measurement rather than judgement — see [CLASSIC-VOICES.md](CLASSIC-VOICES.md) for the
three things that were wrong on the first attempt.

Compile with `-DBM_WITH_EFFECTS=0` to remove the stage entirely. Worth doing on a
microcontroller: it drops the code *and* the comb delay line, which is 8 KB and the
only sizeable buffer in the library. The engine measures 27,184 bytes with effects and
18,976 without.

---

### The dictionary

The letter-to-sound rules handle any word, but they reproduce CMUdict's exact phonemes for
only about **28%** of its 125k entries — and they emit no stress marks at all. (That is not
a contradiction of the NRL report's "90% of running text": common words are far more
regular than the proper nouns and rare words that make up most of a dictionary.)

```
make dict
```

compiles `ref/cmudict-0.7b.txt` into the binary. The difference:

| | without | with |
|---|---|---|
| colonel | K AA L OW N EH L | **K ER1 N AH0 L** |
| Wednesday | W EH D N EH S D EY | **W EH1 N Z D IY0** |
| schedule | S K EH D UW L | **S K EH1 JH UH0 L** |
| choir | CH OY R | **K W AY1 ER0** |

Note the stress digits. The rules cannot produce them, and without them every vowel in
every sentence gets identical emphasis — a large part of why longer words are hard to make
out. The dictionary is what makes stress possible at all.

Cost: the binary goes from **107 KB to 1.8 MB**. That is why it is not the default — the
core exists to run on microcontrollers, and 1.5 MB of data is not an option there. The
rules are the embedded path.

The generated `src/core/bm_dict_data.c` is gitignored: it is 4.6 MB of C and fully
reproducible from an input that *is* committed.

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
| `BM_FIXED_POINT` | 0 | set 1 for a Q18 integer sample loop (no FPU needed) |
| `BM_WITH_MARKUP` | 1 | set 0 to drop the inline-markup parser |
| `BM_WITH_DICT` | 0 | set 1 (via `make dict`) to compile CMUdict in |
| `BM_ENGINE_RESERVED` | 65536 | storage union size; actual use is ~8.5 KB |

---

## Repository layout

```
include/bencmouth.h   the entire public API
src/core/             no stdio, no stdlib, no malloc, no libm
src/host/             platform glue - CLI, WAV writer, voice and song files
src/gui/              the desktop GUI; the only third-party dependency lives here
tools/                generators and demos (see ARCHITECTURE.md)
tests/                ten suites, run with `make test`
voices/               shareable .bmvoice files
songs/                shareable .bmsong scores
ref/                  source material and its provenance - read ref/README.md
render/               generated audio; gitignored
```

`ARCHITECTURE.md` covers the signal path and the design decisions worth defending.
`ROADMAP.md` covers what is done and what is not.
`CLASSIC-VOICES.md` covers the classic-era presets, and which classic voices this engine
cannot reach and why.

---

## Prebuilt binaries

> **No new releases for now.** Tagging is paused while song mode and the classic voices
> settle, and while the release pipeline gets the treatment it deserves — signing and
> notarization on macOS in particular, which is the one thing the runners genuinely
> cannot do without a paid Developer ID. Build from source, or take a CI artifact.

**[Releases](https://github.com/bropple/BENCmouth/releases)** carry tagged builds for
Linux, macOS, Windows and the browser, each with live audio and CMUdict compiled in.

There are two archives per desktop platform. `bencmouth-VERSION-PLATFORM` is the CLI
alone — a console program, so on Windows double-clicking it prints usage and closes,
which is what console programs do. `bencmouth-gui-VERSION-PLATFORM` is the windowed
application, with the font it needs beside it; that is the one to double-click.

Every push also builds on all three platforms and attaches the result to its
[workflow run](https://github.com/bropple/BENCmouth/actions/workflows/ci.yml) — useful
for testing an unreleased commit, but those expire after 90 days.

---

## License

MIT — see `LICENSE`. Do what you like with it; keep the copyright notice. That is the only
condition, and it is there because Ben thought of it.

The repository also contains third-party material with its own terms, listed in `NOTICE`:
the CMU Pronouncing Dictionary is 2-clause BSD and its notice must be reproduced in binary
redistributions, the NRL letter-to-sound rules are US federal government work and therefore
public domain, and the GUI bundles Terminus (TTF) under the SIL Open Font License. If you redistribute BENCmouth in any form, ship `NOTICE` alongside
it and you have covered both.

---

## Status

Working: the synthesizer, the phoneme inventory, frame interpolation, the text front end,
voices, phrase-level prosody, inline markup, the optional dictionary, live audio output,
songs, and the CLI.

---

## Songs

A score is phonemes with `[note]` and `[hold]` threaded through them — the same inline
markup the CLI already had, used to write a melody rather than to colour a sentence. A
`.bmsong` file is that score plus the voice that should sing it, because a melody written
for a 90 Hz voice with a little vibrato sounds wrong out of a 200 Hz voice with none.

```
bm -S songs/daisy.bmsong -a
bm -S songs/bad-news.bmsong -a      # the announcement nobody wants
bm -S songs/good-news.bmsong -a
bm -S songs/boing.bmsong -a         # a word dropped down a stairwell
```

`bad-news`, `good-news` and `boing` exist to make a point CLASSIC-VOICES.md makes in prose: some
of the classic novelty *voices* were never voices at all. There is no setting that makes
a synthesizer sing a fixed melody — a melody is a score, and a score is a file. Nothing
in either voice block is doing the work.

The format is text and deliberately the same shape as a `.bmvoice` file — a header of
`key = value` lines. The one addition is that a score is many lines of free text, which
`key = value` cannot carry, so a line reading exactly `score =` ends the header and
everything after it is the score:

```
# BENCmouth song
title          = Daisy Bell
voice          = BENCmouth
tempo          = 116
vibrato        = 0.28
prosody        = 0
score =
# Daisy, Daisy
[note C4][hold 520] D EY1 [note A3][hold 260] Z IY0
```

Points worth knowing:

- **Comments are whole lines beginning with `#`.** A `#` anywhere else is literal, and it
  has to be — `[note A#4]` is a sharp, and stripping from the first `#` to end of line
  the way the `.bmvoice` loader does would silently eat every accidental in the file.
- **Every voice key is written on save**, not only the ones that differ from a preset, so
  a song reopens as exactly the voice it was left as even if the preset it started from
  later moves.
- **Turn `prosody` down for singing.** It is speech planning — it declines the pitch
  across a phrase and lengthens the final syllable — and against a written melody all of
  that is interference.
- **`tempo` is the tempo the `[hold]` values are written at.** The engine has no idea
  what a tempo is — `[hold]` is milliseconds and always was — so the header line is what
  ties those milliseconds to a musical speed. The GUI gives it a control, and moving it
  rewrites every `[hold]` in the score by the same ratio: the song genuinely speeds up or
  slows down, and the header and the holds never disagree. There is a control for the
  quarter note in milliseconds too, since that is the number you actually type into a
  `[hold]`; they are one value in two units.

  Songs do not scale exactly with it, because consonants keep their own length. Daisy
  Bell runs 11.9 s at 116 BPM and 9.9 s at 160, not 8.6 s. That is what singing does as
  well — a note's duration lives in its vowel, which is why `[hold]` only touches
  vowels.
- An **unknown voice name is not fatal** — the score is the part that cannot be
  reconstructed, so the file still opens and only the starting point is lost. An unknown
  *setting* is fatal, for the reason voice files give: one that is quietly dropped
  produces a song that mysteriously sounds wrong.

### In a browser

```
make wasm
```

produces a bare `wasm32` module — no libc, no Emscripten runtime, nothing generated. That
works only because the core is freestanding, which is what `make check-freestanding` has
been protecting all along.

```js
import BENCmouth from './src/wasm/bencmouth.js';

const bm = await BENCmouth.load('bencmouth.wasm');
console.log(bm.phonemes('hello world'));   // HH EH L OW W ER L D
await bm.play('hello world');              // through Web Audio
const pcm = bm.say('hello world');         // or just the Float32Array
```

`bm.voice(name)` and `bm.param(key, value)` reach the presets and the `.bmvoice` file keys.

### The GUI

```
make gui-dict     # the GUI, with the CMU dictionary compiled in
make gui          # without it - smaller, letter-to-sound rules only
./bencmouth-gui              # optionally: ./bencmouth-gui 1400x760
```

`make gui` and `make gui-dict` differ in one thing that you can hear. Without the
dictionary every word goes through the letter-to-sound rules, which get *robot* as
`R AA B AA T` — two flat vowels, no stress. With it, `R OW1 B AA2 T`. The DICT button
switches between the two at runtime so you can hear the difference on any word, which
is also how you find the ones worth adding to the exception list; it greys out in a
build that has no dictionary to switch to. The status line says which build you have.

The window has two tabs. **TEXT** is the speech side: type, and the phoneme readout under
the field updates as you go. **SONG** is a score editor — a phoneme field with `[note]`
and `[hold]` in it, a word-to-phoneme translator with an INSERT button so you do not have
to know ARPABET by heart, a FORMAT button opening the full reference for the notation,
and LOAD/SAVE for `.bmsong` files. SPEAK becomes SING, and SAVE WAV renders whichever tab
is in front.

Each tab keeps its own voice. Song mode wants prosody off and a little vibrato; speech
wants the opposite, and one shared voice would mean every trip through the song tab
quietly retuned the text tab. The sliders always edit whichever is in front.

Type, and the phoneme readout under the field updates as you go. Both panels wrap and
scroll — mouse wheel or the bar on the right — so a paragraph is as workable as a
sentence. The text box is a real text box: click to put the caret anywhere, drag or
shift-arrow to select, Ctrl-A/C/X/V (Cmd on macOS), Ctrl-arrow by word, Home and End,
and a right-click menu for the same. The phoneme readout follows the DICT button, so
what you see is what SPEAK will say.

The readouts — phonemes, the word translator's output, the format reference, the licence
text — are **selectable but not editable**: click to place the caret, drag or shift-arrow
to select, Ctrl-A and Ctrl-C, and a right-click menu offering the two operations that do
not write. Typing, backspace and paste do nothing there, because what those boxes show is
derived from something else and an edit would be overwritten by the next keystroke. It is
the same widget as the text box with editing switched off rather than a second one, so
the caret and the selection cannot drift apart between them. Lifting phonemes out of the
readout is the natural way to start a score.

The effects column **scrolls** — there are more of them than fit, and a mouse wheel
anywhere over the column moves it. It scrolls by whole rows rather than pixels, so a
control is either there or it is not; a half-scrolled slider would still be live with its
hit area hanging outside the column.

Each slider is three controls. Drag the track for the coarse move; click the **number** to
the right of it and type an exact one, with Enter or a click elsewhere to commit and Escape
to put it back; or use the two small **arrows** beside it to step by whatever precision the
readout displays — 0.01 on a plain parameter, 1 Hz on a frequency, held down to repeat.
That last one is the difference between being able to place a formant and having to hunt
for it: a 100 px track across a 0–400 Hz range moves 4 Hz per pixel, and the values worth
finding are usually a hair off the one you can hit.

Every slider is bound to a `.bmvoice` file key, so
what you tune and what SAVE writes cannot drift apart, and LOAD reads the same files
back — the presets in `voices/`, or anything you saved. A file is applied over the
current voice, the way `bm -f` does, so one that sets two keys is an edit rather than a
whole voice; it is parsed into a copy first, so a file that fails halfway cannot leave
you with a half-changed voice. Audio streams from `bm_read()`
into the audio callback, so moving a slider mid-sentence is audible immediately.

**The meter shows two numbers, and the gap between them is the point.** The fill is RMS,
the bright marker is peak, and both are held for the whole utterance and reset by SPEAK.
They disagree here far more than they would on a mixing desk: across the voices that
carry an effects chain, peak spans four to one — `Aggressor` at 0.124 against `Gravel` at
0.463 — while their RMS differs by 0.3 dB. Nothing is quieter. Drive and crush collapse
the crest factor, which is what distortion does, and the ear follows RMS much more
closely than peak. A peak-only meter calls a driven voice quiet and sends you to turn it
up. The amber line is where the host limiter starts, at 0.85, and that one is a peak
question — which is why peak is shown as a fraction of it and loudness in decibels.

LOAD, SAVE and SAVE WAV ask where the file goes or comes from, through whatever dialog
the system already has: the standard Windows save dialog, the Cocoa save panel on macOS, and
zenity or kdialog on Unix. Nothing is bundled to do it — those are all either part of
the OS or already installed. On a bare X session with neither helper the file goes to
the working directory and the status line says where; that beats refusing to save over
a missing helper program.

raylib is the only third-party dependency in the project and it is confined to
`src/gui/` — `make`, `make test` and `make check-freestanding` all work without it.

On macOS the GUI ships as a disk image: mount `bencmouth-gui-VERSION-macos-arm64.dmg`
and drag BENCmouth to Applications. Inside it is `BENCmouth.app`, which is not packaging
taste — a bundle is the only way a macOS program gets an icon. Windows reads the icon from a resource inside the
executable and X11 takes it from a property the binary sets at startup, but GLFW's Cocoa
backend ignores `glfwSetWindowIcon` entirely, because a bare Mach-O executable has no
Finder or Dock identity to hang one on. The bundle is a wrapper: `Contents/MacOS` holds
a single file, because everything else is already inside it.

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

The GUI executable is self-contained: the font, the window icon, the BENCO wordmark and
the licence texts are all compiled into it. A file beside an executable is a file that
can go missing — unzip without the assets folder, make a shortcut, launch from a
terminal somewhere else, and the window came up in a fallback font with no icon. Drop
`bencmouth-gui` anywhere and it looks the way it is supposed to.

The font is Terminus (TTF), embedded unmodified — the licence reserves its name against
modified versions, so it is never subset or regenerated. It is a bitmap design, so it is
loaded at three native sizes with point filtering rather than scaled: crisp unantialiased
text at fixed sizes *is* the terminal look, arrived at honestly rather than simulated. A
copy on disk beside the binary still wins, so a different build of the face can be
dropped in without recompiling; the status line says which one loaded.

Embedding the font is what the OFL allows *provided* every copy carries the copyright
notice and the licence somewhere the user can easily view them. The ⓘ button in the top
right opens a window with the program's name, copyright, the MIT licence, every
third-party notice and the full OFL text — all read out of the binary. That window is
the compliance, not a nicety.

For a target without an FPU, `-DBM_FIXED_POINT=1` runs the sample loop in Q18 integer
arithmetic — 52.9 dB SNR against the float reference, measured on a rendered sentence.
Coefficients still come from float maths at the frame rate, where it costs nothing.
