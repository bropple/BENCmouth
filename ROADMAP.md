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

- [x] **Terminus (TTF) bundled**, unmodified, with its OFL text in
      `assets/fonts/OFL.txt` and an entry in NOTICE. The GUI still falls back to
      raylib's built-in font if the file is missing, and the status line names whichever
      it actually loaded rather than asserting one.

## Songs

- [x] **`.bmsong`** (`src/host/bm_songfile.c`, `bm -S`, `songs/daisy.bmsong`). A score plus
      the voice that should sing it, because a melody written for a 90 Hz voice with a
      little vibrato sounds wrong out of a 200 Hz voice with none, and losing the voice on
      save makes every reload a re-tuning session.

      Same shape as a `.voice` file - a `key = value` header - with one addition: a line
      reading exactly `score =` ends the header and everything after it is the score.
      A key with no value rather than a marker of its own, so the whole file is one
      lexical form and there is nothing extra to explain.

      The decision worth recording: **comments are whole lines only.** The `.voice` loader
      strips from the first `#` to end of line, and doing that here would silently eat
      every accidental in the file - `[note A#4]` is a sharp.

      The parser works on a memory buffer with the file reader as a thin wrapper, which
      is what makes `tests/test_song.c` able to cover it without touching the filesystem.

- [x] **Song mode in the GUI** (`src/gui/bm_song_ui.c`). A second tab: score editor,
      word-to-phoneme translator with an INSERT button, the full notation reference behind
      a FORMAT button, and LOAD/SAVE for `.bmsong`.

      Each tab keeps its own voice. Song mode wants prosody off and a little vibrato and
      speech wants the opposite, so one shared voice would mean every trip through the
      song tab quietly retuned the text tab.

      Needed one fix in `bm_ui.c` that was not obviously a bug until a one-line field
      existed: 8 px of padding above and below a 24 px line means a box built to hold
      exactly one line has less room inside it than the line needs, so it clipped its own
      descenders and raised a scrollbar for text that fitted. The inset now shrinks below
      the height of two lines.

## Voices of other machines

- [x] **The classic set** - `Compact`, `Announcer`, `Operator`, `Cadet`, `Whisper`,
      `Rattled`. Tuned toward the archetypes of the voices desktop machines shipped with
      in the eighties and early nineties, from the published acoustics of the voice types
      involved. Nothing was disassembled or lifted from a shipped synthesizer.

      Two of them needed new engine parameters, and both arrived at their off setting:

      - **`whisper`** (0..1) trades voicing for turbulence. `breathiness` could not do it -
        it adds aspiration alongside phonation and leaves the folds working, and a whisper
        has no fundamental at all. The 16 dB trade level was measured, not chosen: it is
        where whispered vowels land at the same RMS as `/s/ /sh/ /f/ /th/`, which is the
        acoustic signature of whispering and why it sounds made of consonants.
      - **`vibrato`** / **`vibrato_rate`**, for held notes far more than for the wobbling
        voice that prompted them.

      **What the engine cannot reach is written down** in CLASSIC-VOICES.md rather than
      left as an absence: instrument-source voices need a pluggable excitation source
      (the glottal model is fixed, and that is what lets there be no lip-radiation stage);
      ring-modulated voices are ten lines and deliberately omitted, because a modulator is
      an effect applied to speech rather than a property of a speaker; a detuned chorus is
      already possible in the host by summing three engines; the fixed-melody novelty
      voices are songs, not voices; and the concatenative voices are a different kind of
      program entirely.

## Effects

- [x] **A post-synthesis effects stage** (`src/core/bm_effects.c`, `bm -e`).
      Ring modulation, a resonant comb, a waveshaping drive and sample-rate
      reduction, in that order.

      **`bm_effects` is a separate struct from `bm_voice`, and that is the
      design.** An earlier note in CLASSIC-VOICES.md said ring modulation was
      cheap and deliberately omitted, because "an effect is not a property of a
      speaker" and it would have been the first field in `bm_voice` that was not
      a claim about a vocal tract. That objection was right and still is; giving
      effects their own struct honours it. It also turned out to be the more
      useful arrangement, because the two now compose - any effect on any voice,
      rather than a preset table holding every combination.

      Three things were measured rather than chosen, and all three were wrong
      first time:

      - **Drive compensation.** RMS jumped 2.6x by a drive of only 0.2 and then
        *fell* as compression took over, so a straight-line trim was hopeless.
        `1/(1 + 4.5*drive^(1/4))` holds the level flat to 3%. The fourth root is
        two `sqrt` calls rather than a `pow()` the core does not have.
      - **Comb normalisation.** Folding the `(1-fb)` resonance compensation into
        the *mix* rather than into the wet signal left the output 78% dry at the
        full setting - 2 dB of tooth-to-notch depth where there should have been
        13. Now 25x on a rendered spectrum.
      - **An output level after the chain.** Voice `gain` is applied before the
        effects, correctly, because drive is a threshold effect. But that makes
        the gain slider inert once drive is up. Input trim, chain, output level -
        the topology of every overdrive pedal ever built.

      `tests/test_effects.c` asserts the one that matters: an all-zero chain is
      bit-for-bit the unprocessed signal, 0 of 30,140 samples differing. A whole
      new stage landed in the signal path without moving Retro's reference.

      `-DBM_WITH_EFFECTS=0` removes the stage, the code and the 8 KB comb delay
      line - the only sizeable buffer in the library. 27,184 bytes of engine with
      it, 18,976 without.

- [x] **Three-column parameter layout in the GUI.** Seventeen voice sliders and
      seven effect sliders in the same 216 px two columns used for the voice
      alone, because the effects column is one row shorter. The effects dropdown
      is the column heading rather than a separate control, which is the row that
      would not have fitted. Needed adaptive metrics in `bm_slider`: the label
      and readout now take a share of the row, capped at the old fixed widths, so
      a full-width slider is laid out exactly as before and only a narrow one
      gives ground.

## Monotone, actually

- [x] **`flatten`** (`bm_voice.flatten`, 0..1, default 0). BENCmouth Monotone was not
      monotone. `f0_range = 0` is documented as "monotone robot" and is read only by
      `bm_prosody.c`, which that preset does not use - so the older contour went on
      applying its 18% declination and its 6% stressed-syllable bump, and the preset
      measured a **28.5% pitch swing**, 4.3 semitones, the same spread as BENCmouth
      Retro. Stressed vowels also ran 1.61x the length of unstressed ones.

      At 1: no declination, no pitch accent, no durational stress. Default 0, so every
      other preset is untouched and Retro's reference is unmoved. An absolute `[note]`
      is exempt - flattening it would have made song mode and this parameter mutually
      exclusive for no reason.

      Monotone now measures 120.0..120.0 Hz across an utterance, and the same vowels
      take the same number of frames whatever stress digit they carry.

- [x] **Second wave of voice analogues.** `Frantic` and `Grizzled` as presets;
      `Emissary`, `Diver` and `Foreman` as `.voice` files, because each needs an effects
      chain and an effect is not part of a `bm_voice`. Plus `songs/bad-news.bmsong` and
      `songs/good-news.bmsong`, which are the honest form of two classic novelty
      "voices" that were always scores rather than settings.

      Both songs hit the limiter on first writing - sustained notes with the declination
      off stack up the way a flutter-free voice does - so CI now fails if any shipped
      voice or song clips.

## The excitation stops being fixed

- [x] **`bm_voice.source`** (0..2: folds, pipe, bell). CLASSIC-VOICES.md listed every
      instrument-source novelty voice as unreachable, and the reason was one line of
      architecture: `bm_glottis.c` produced the glottal flow derivative and nothing
      else. It now crossfades into two additive stacks.

      The bell table is a church bell's measured partials, and the tierce at 1.19 - a
      minor third, and a harmonic of nothing - is what makes it read as a bell.
      Measured at a steady 150 Hz, energy at 0.5x and 1.19x f0 goes from 14 and 25 dB
      on the glottal source to 58 and 61 dB on the bell. The pipe stays harmonic at
      both, which is the control.

      Two things that were not obvious:

      - The partials need their own phase accumulators. `sin(2*pi * phase * ratio)`
        looks equivalent and is not: `phase` wraps at 1, so a non-integer ratio steps
        discontinuously at every wrap - a click at the fundamental frequency, which is
        a buzz exactly where the source is supposed to be smooth.
      - They start spread around the cycle. Six sines starting in phase sum to one
        large spike on the first sample, which is a tick at the start of every
        utterance.

      Level-matched to within 0.1% RMS by measurement rather than by summing the
      amplitude columns - six sines are not as loud as their coefficients suggest, and
      the glottal pulse is a sparse spike train whose RMS is nothing like its peak.

      Default 0. Retro's reference is unmoved.

- [x] **The roster filled in.** `Frederick` and `Princess` as presets; `Zarvox`,
      `Carillon` (bell) and `Harmonium` (pipe) as `.voice` files; `songs/boing.bmsong`,
      which is the third member of the "this was never a voice" set - a bouncing pitch
      envelope written down is a score.

      Still out of range and still written down: a detuned chorus needs several engines
      summed, and the bell sustains rather than decaying because the source cannot see
      phoneme onsets.

- [x] **The voice dropdown sorts and scrolls.** Fifteen presets in declaration order
      ran off the bottom of the window. Alphabetical for display only - nothing else
      depends on preset order, because everything else looks them up by name - and the
      visible row count is computed from the space below the control rather than
      fixed, since the window is resizable.

## Known, and recorded rather than fixed

- **The default voices engage the limiter on long sentences.** On "The quick brown fox
  jumps over the lazy dog." with the dictionary compiled in, `BENCmouth` peaks at 1.367
  and `BENCmouth Retro` at 1.165 against a limiter threshold of 0.85. Found by the
  clipping check added for the songs, and confirmed present at the commit before it -
  this is long-standing, not a regression.

  Retro's `gain` cannot be lowered: it is pinned, and lowering it would move the one
  voice the golden reference exists to hold still. The soft limiter absorbs it, which is
  what a soft limiter is for, so this is a level-headroom observation rather than
  distortion. CI reports it for those two and fails for everything else.

  The real fix, if one is wanted, is a look-ahead normalisation pass in the host layer
  rather than a per-voice trim - peak level is a property of the sentence as much as of
  the voice, and no constant gain is right for both a word and a paragraph.

## Releases

Tagging is **paused** until the release pipeline is finished. The runners already build
everything, including a styled macOS `.dmg` - the gap is signing and notarization, which
needs a paid Apple Developer ID and is the one part no amount of CI work substitutes for.
Until then: build from source, or take a CI artifact.

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
