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
- [ ] Prosody: durations, stress, F0 contour — *partially done inside `bm_frames.c`
      (declination, stress-based duration and pitch). Move it out to `bm_prosody.c`
      and do phrase-level contours properly. Biggest remaining naturalness gap.*
- [x] Engine layer (`bm_engine.c`) — full public API, no allocation, 8.5 KB of state
- [x] Text front end — normalization, number expansion, abbreviations, and the
      329 NRL letter-to-sound rules generated from the public-domain source by
      `tools/mkrules.c`
- [x] `bm` CLI (`src/host/main.c`) — text or phonemes in, WAV out
- [ ] CMUdict lookup in front of the rules, for the ~10% they miss ("machine").
      Needs `tools/mkdict.c` and a compile-time switch: 135k entries is far too
      much for a microcontroller, and the rules alone are the embedded path.
      **This also replaces the stopgap EXCEPTIONS table in `bm_text.c`** — delete
      that table when the dictionary lands rather than letting it grow.
- [ ] Stress assignment. The rules emit no stress marks at all, so every word is
      spoken with flat, uniform emphasis. Unmarked stress now means "no
      information" rather than "unstressed" (which was reducing every vowel in
      every sentence), but real stress needs either the dictionary or a
      syllabification heuristic. This is the other half of the prosody gap.

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
- [ ] More naturalness controls, each following the same contract: perceptually-spaced
      formant interpolation, per-phoneme bandwidth variation, phrase-final F0 fall.
- [ ] A `bm_voice_random()` for exploring the space — most good presets will be found by
      accident before they are found by reasoning.

## Later / speculative

- Fixed-point build (`BM_SAMPLE_FLOAT=0`) — needs Q-format multiply macros through the DSP
- WASM target via `clang --target=wasm32`
- Singing / explicit F0 control through the public `bm_frame` path
- ~~Inline markup in text~~ — **done**, and it lives in core. `bm_config.markup` gates it
  at runtime (off by default, so brackets stay ordinary text for anyone who did not opt
  in) and `BM_WITH_MARKUP` removes the parser entirely for embedded builds.
  `[pitch N]`, `[speed X]`, `[pause N]`, `[reset]`.

  The design decision worth recording: commands survive into the phoneme string rather
  than being resolved away in the front end. That keeps the phoneme string the single
  interface between text and synthesizer — `bm -t` shows exactly what will be acted on,
  `bm_speak_phonemes()` honours markup for free, and there is no side channel to keep in
  sync. The cost is 6 KB of per-phoneme override storage in the frame generator.
