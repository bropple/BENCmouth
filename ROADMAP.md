# Roadmap

Ordered roughly by build sequence, not priority. See ARCHITECTURE.md for the rationale
behind the ordering.

## Core path

- [x] Public API surface (`include/bencmouth.h`)
- [x] Resonator / antiresonator primitives + libm-free math (`bm_math.c`, `bm_resonator.c`)
- [x] Voiced source, noise source (`bm_glottis.c`, `bm_noise.c`)
- [x] Cascade/parallel topology — **milestone reached**: `tools/vowel_demo.c` renders five
      sustained vowels, and `tests/test_synth.c` confirms spectral peaks land on the
      requested formants under noise excitation
- [x] Phoneme inventory and target tables (`bm_phonemes.c`) — all 39 ARPABET phonemes
      plus silence, loudness-balanced against natural speech intensities using
      `tools/level_check.c`
- [x] Frame interpolation (`bm_frames.c`) — target-and-transition model with
      smoothstep glides, stop closure/burst segments, diphthong glides
- [x] Prosody (`bm_prosody.c`) — phrase-level contours, behind `voice.prosody`.
      Segments at boundary phonemes so each phrase declines from its own starting
      pitch instead of a paragraph sliding into the floor; places pitch accents on
      stressed syllables (which the dictionary made possible); shapes the last third
      of each phrase by boundary type — fall for a full stop, rise for a question,
      small lift for a comma; lengthens the phrase-final syllable.

      Required distinguishing punctuation, which the front end had been collapsing:
      `.` `?` `,` are now boundary phonemes rather than all becoming `SIL`, so
      `bm -t` reads `HH AH0 L OW1 . AA1 R Y UW1 ?`.

      **`f0_range` finally does something.** It was documented as "semitones of
      intonation excursion", written into every voice file, and read by nothing at
      all. It is now the excursion scale for the whole contour.
- [x] Engine layer (`bm_engine.c`) — full public API, no allocation, 8.5 KB of state
- [x] Text front end — normalization, number expansion, abbreviations, and the
      329 NRL letter-to-sound rules generated from the public-domain source by
      `tools/mkrules.c`
- [x] `bm` CLI (`src/host/main.c`) — text or phonemes in, WAV out
- [x] CMUdict lookup in front of the rules (`tools/mkdict.c`, `bm_dict.c`).
      124,910 entries, front-coded and compiled to ~1.5 MB, behind `BM_WITH_DICT`
      and built with `make dict`. Not the default: the core exists to run on
      microcontrollers and the rules are that path.

      Measured before building it: the rules match cmudict exactly for only 28%
      of entries, so an exceptions-only table would have had to hold most of the
      dictionary anyway. The EXCEPTIONS table in `bm_text.c` stays as the
      no-dictionary fallback rather than being deleted — it is only reached on a
      dictionary miss.
- [x] Stress assignment — solved by the dictionary, which carries a stress digit
      on every vowel. Words that miss the dictionary still come through unmarked,
      and a syllabification heuristic for those remains open.

## WAV export

Slotted at `src/host/bm_wav.c`. The pull model means core needs no support for this at all:
pull `bm_read()` until `bm_is_speaking()` goes false, convert, write a RIFF header. Points
that need an actual decision when we get there:

- **Float to int16 conversion policy.** Clip or soft-limit? A formant synth with several
  parallel branches summing can overshoot on plosives. I'd soft-limit and log peak level,
  because silent clipping is miserable to debug by ear.
- **Bit depth.** 16-bit PCM covers the use case; 32-bit float output is worth having behind
  a flag for anyone doing analysis on the result.
- **`-o -` writes to stdout** so `bm "hello" -o - | aplay` works. Costs nothing, and it is
  how this will get used during development far more than files will.

- [x] **Live output** (`src/host/bm_audio.c`, `bm -a`, `make audio`). ALSA, AudioQueue and
      waveOut behind one blocking-write interface, streaming from `bm_read()` with no
      intermediate buffer. Limiting is shared with the WAV writer rather than duplicated,
      so a rendered file matches what came out of the speakers.
      *Only the ALSA path has actually been heard; the other two are compile-verified in
      CI, which is not the same thing.*

## Voice characteristics

- [x] **Two-axis vocal tract.** `formant_scale` replaced by `throat` (governs F1) and
      `mouth` (governs F3+), with F2 answering to both. Two knobs a person can turn beats
      five independent numbers; the goal is tunability by ear, not maximum expressiveness.
- [x] **Voice files.** `key = value` text format, loader in `src/host/bm_voicefile.c`,
      per-key application in core so embedded targets can accept settings from a string
      without the parser. Unknown keys are errors — a silently dropped setting produces a
      voice that mysteriously sounds wrong.
- [x] **Preset set.** BENCmouth Retro, BENCmouth Monotone, Deep, Bright. See the Retro
      contract in ARCHITECTURE.md.
- [x] **Coarticulation**, as a parameter defaulting to off.
- [x] **Per-voice `gain`.** Peak level is not a property of the phoneme table alone: a
      voice with `f0_flutter = 0` drives the cascade with a perfectly periodic pulse
      train, the resonators ring in lockstep, and peaks stack up. Monotone reached 0.96
      where flutter-bearing voices sit near 0.55. Applied at the synthesizer output, not
      as a dB offset to the source amplitudes — those floor at 0 dB meaning silence, so a
      negative trim there would mute quiet branches outright rather than attenuate them.
- [x] **Phrase-final F0 fall** — part of `bm_prosody.c`.
- [x] **Perceptually-spaced formant interpolation** (`formant_glide`). Linear-in-hertz
      spends most of a transition in the top of the range; geometric spacing moves in
      equal ratios per unit time, which is how formant movement is actually heard. A
      300→2300 Hz glide passes through 830 Hz at the halfway point rather than 1300.
      Needed `bm_log2f`, which the core did not have.
- [x] **`bm_voice_random()`** (`bm -R SEED`). Deterministic from the seed; `-w` captures
      one worth keeping. Gain is trimmed by flutter rather than drawn at random, because
      low-flutter voices stack peaks — without that roughly a third of draws clipped.
- [x] **Per-phoneme bandwidth variation** (`bandwidth_track`). The table gives one
      bandwidth per formant per phoneme class, so every vowel got a 60 Hz first formant
      whether F1 sat at 270 for /i/ or 730 for /a/. Bandwidth now scales as
      `(F / F_reference)^0.7`, putting those at 39 and 78 against measured values near 45
      and 90. Also fixes a quieter problem: `throat` and `mouth` move formants without
      touching the table, so a large speaker was getting a small speaker's bandwidths.

## GUI

- [x] **Desktop GUI** (`make gui`, `src/gui/`). raylib, hand-drawn widgets, BENCO palette,
      the layout from GUI-PLAN.md. Text field with a live phoneme readout, voice dropdown,
      fourteen parameter sliders bound to the `.voice` keys, waveform, peak meter with the
      limiter threshold marked, WAV export, and a random-voice button.

      Audio streams from `bm_read()` straight into raylib's audio callback, so a slider
      moved mid-sentence is audible immediately - which is the entire point of tuning by
      ear, and the reason the pull interface was shaped that way.

      The dependency is confined to `src/gui/`: `make`, `make test` and
      `make check-freestanding` all work with raylib absent.

- [ ] Terminess is looked for at a few paths and falls back to raylib's built-in font.
      Bundling it means shipping the OFL text alongside, which is a packaging decision
      rather than a code one.

## Later / speculative

- [x] **Fixed-point sample loop** (`-DBM_FIXED_POINT=1`, `src/core/bm_fixed.h`). Q18 in
  int32 with int64 products. Off by default: float is the reference, it is what every
  voice was tuned against, and anything with an FPU is better off with it. The case is
  the low end — a Cortex-M0 or M3 has no FPU and soft-float costs roughly an order of
  magnitude on a loop that runs ~50 multiplies per sample.

  Only the per-sample path is converted. Coefficients come from transcendental functions
  a hundred times a second; they stay in float and are stored as Q18.

  Two measurements drove the design. First, converting *only* the resonator gave 25.1 dB
  SNR against float — worse than either pure path, because twelve filters in series meant
  24 round-trip quantizations per sample. Completing the conversion through `bm_synth_tick`
  took it to 40.3 dB. Second, sweeping the Q format:

      Q16  40.3 dB   headroom +-32768
      Q18  52.9 dB   headroom +-8192    <- chosen
      Q20  65.6 dB   headroom +-2048    saturates the stress test
      Q22  77.0 dB   headroom +-512

  Q18 is the most precision available that still clears the worst excursion the suite
  produces (3869). Two tests carry a fixed-point tolerance and say why: resonator DC gain
  holds to 1e-3 rather than 1e-4, and the nasal pole/zero cancel to 6e-4 rather than 1e-5.
- [x] **WASM target** (`make wasm`, `src/wasm/`). Bare wasm32 with no libc and no
  Emscripten runtime, which is possible only because the core is freestanding - the
  property `make check-freestanding` has been guarding since the first commit. The glue
  is about a hundred lines of JavaScript; there is no generated runtime, no emulated
  filesystem, and nothing allocates.

  *Written without a local toolchain: this machine has no clang, no wasm-ld and no
  Emscripten. The C and the JS are type- and syntax-checked here; the build and a Node
  smoke test that renders audio and inspects it run in CI, which is where the evidence
  that it works actually comes from.*
- [x] **Singing.** Two markup commands rather than a separate API: `[note NAME]` sets an
  absolute pitch by name (`C4`, `A#3`, `Bb5`) and `[hold MS]` sets the length of the
  vowels that follow. Both reach `bm_speak_phonemes()` too, so `bm -m -P` sings.

  `[note]` had to be *absolute* where `[pitch]` transposes. Sharing the transposing
  behaviour meant the prosody planner's accent multiplied on top and A4 came out at
  525 Hz. `[hold]` applies to vowels only: a sung note's duration lives in its vowel,
  and stretching the consonants turns the word into a groan.

  `render/daisy.txt` holds the score; `make dict && sh` it through `bm -m -P`.
- ~~Inline markup in text~~ — **done**, and it lives in core. `bm_config.markup` gates it
  at runtime (off by default, so brackets stay ordinary text for anyone who did not opt
  in) and `BM_WITH_MARKUP` removes the parser entirely for embedded builds.
  `[pitch N]`, `[speed X]`, `[pause N]`, `[reset]`.

  The design decision worth recording: commands survive into the phoneme string rather
  than being resolved away in the front end. That keeps the phoneme string the single
  interface between text and synthesizer — `bm -t` shows exactly what will be acted on,
  `bm_speak_phonemes()` honours markup for free, and there is no side channel to keep in
  sync. The cost is 6 KB of per-phoneme override storage in the frame generator.
