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
      fourteen parameter sliders bound to the `.bmvoice` keys, waveform, peak meter with the
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

      Same shape as a `.bmvoice` file - a `key = value` header - with one addition: a line
      reading exactly `score =` ends the header and everything after it is the score.
      A key with no value rather than a marker of its own, so the whole file is one
      lexical form and there is nothing extra to explain.

      The decision worth recording: **comments are whole lines only.** The `.bmvoice` loader
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

## The piano roll, and the plugin

The goal is a third tab: notes on a grid, a phoneme typed into each one, durations
dragged - and, later, the same thing inside a DAW as a CLAP and a VST3, following the
host's transport. `../BENCsynth` has already done the plugin half once and its
`docs/PLUGIN.md` is the reference.

Two things had to be true before any of it could be drawn, and neither was.

- [x] **`[dur MS]`** (`apply_dur_groups` in `bm_frames.c`). `[hold]` sets the length of
      the vowels and lets the consonants take whatever they take on top. Measured, at
      `[hold 400]`: `IY1` sounds for 450 ms, `M IY1` for 560 ms, `S T R EY1 T` for 1000 ms.
      Three notes written the same length, drawn the same width, playing at three
      different lengths - and the error is different for every note and accumulates, so
      by the second bar the grid is decorative.

      That is not a bug in `[hold]`. Stretching a consonant cluster bodily *does* turn the
      word into a groan, and for a score you read rather than a score you draw, vowel-only
      is the right answer. So `[dur]` is a second way of asking rather than a change to
      the first: `[dur 400]` means the run occupies 400 ms in total, consonants included.
      Every existing `.bmsong` renders sample-for-sample as it did - `songs/daisy.bmsong`
      is still 11.88 s.

      The run is delimited by the brackets rather than by syllabification. A group ends at
      the next `[dur]`, at a `[pause]`, at `[reset]` or at the end of the string, which is
      unambiguous and needs no English phonology; a compiled note grid writes one `[dur]`
      per note, so the question never arises there. Group membership is carried as an id
      rather than found by looking for equal lengths, because two notes of the same length
      in a row is what a melody mostly *is*, and matching on the value would merge them.

      **When it will not fit, the consonants compress.** The time comes out of the vowel
      first, down to 60 ms, and after that out of the consonants via the existing `speed`
      modifier, capped at half length - which is what a singer does with a word too long
      for its beat. `S T R EY1 T` has 600 ms of consonants in it and sings in a 400 ms note
      through that mechanism. Below the cap it overruns rather than pretending, and
      `bm_measure()` reports the length it really came out at.

      The lengths are assigned, then **measured and corrected**. Every duration goes
      through a rounding to whole frames and back and no segment is ever shorter than one
      frame, so the errors are all in the same direction and do not cancel; predicting
      them would have meant a second model of `segment_frames` sitting beside the first,
      which is the arrangement this whole change exists to remove.

- [x] **`bm_measure()`** (`src/core/bm_measure.c`). Where each phoneme starts, how long it
      lasts, the byte offset of the token that produced it, and which `[dur]` group it
      belongs to - without rendering a sample. An editor cannot work any of that out for
      itself without reimplementing the frame generator, and a second implementation would
      be free to drift from the one making the sound. A timeline that is approximately
      right is worse than none: it looks authoritative.

      So there is no arithmetic in the file. Every length comes from
      `bm_frame_gen_phoneme_frames()`, which is the renderer's own `segment_frames()`
      summed up. `tests/test_timing.c` renders through `bm_read()` and compares sample
      counts against the prediction: the difference is the 100 ms filter ring-out and
      nothing else, on all three scores it tries.

      It takes a `bm_engine_storage` as scratch rather than allocating or putting a
      hundred kilobytes on the caller's stack, which is the arrangement `bm_engine_init`
      already uses.

- [x] **`[pause N]` is N milliseconds.** Found while checking that a compiled roll sounds
      at the times it was drawn at: the third note came in 30 ms late, every time. The
      length a pause is given lands on the silence phoneme's *steady* segment, and the
      silence also has a 30 ms transition in front of it - so `[pause 400]` had always
      been 430 ms of silence. Never audible on its own, and it is the gap between two
      phrases, so nothing ever pointed at it. On a grid it is the same accumulating error
      `[dur]` exists to remove, arriving through the rests instead.

      Fixed the way `[dur]` is: the total is what was asked for. Every shipped song is
      exactly 30 ms shorter per rest - boing has six of them and lost 180 ms, good-news
      has none and did not move - which is the arithmetic being right rather than a
      regression, and is imperceptible either way.

- [x] **The ROLL tab** (`src/host/bm_roll.c`, `src/gui/bm_roll_ui.c`). A note is a pitch,
      a start, a length and a lyric; the lyric is typed as a word and spelled by the front
      end, with the phoneme field there to type over when the dictionary is wrong. It
      compiles to a score and plays through `bm_speak_phonemes()`, so the audio path did
      not change at all.

      The model, the compiler and the file format are in the host layer and know nothing
      about a screen, which is what lets `tests/test_roll.c` cover all three - including
      the property the tab exists for: compile a roll, measure the result, and check the
      notes sound at the times they were drawn at.

      **Notes cannot overlap**, and that is not a simplification - there is one voice.
      Dragging a note onto its neighbour makes the neighbour give way, which also makes
      the compile exact by construction rather than by clamping something later.

      **An overrun is drawn.** A note is the length it asks for until its consonants will
      not fit, and then it runs over; the excess is an amber bar past the note's right
      edge and the readout says what it really sounds for. This is the whole reason
      `bm_measure()` was built first: the figure comes from the code that makes the sound,
      so a roll with no amber in it plays exactly as drawn.

      Stored as repeated `note =` lines in the `.bmsong` header - one more key rather than
      a second block, so `score =` goes on being the thing that ends the header - beside
      the compiled score, which is still what sings. `bm -S` needed no changes. A file
      with no notes opens in the ROLL tab as a refusal rather than as an empty grid,
      because a score can say things a grid cannot draw.

      Driven under Xvfb with xdotool to check the gesture end to end, which is worth
      recording as a technique: **raylib misses a click that is pressed and released
      between two frames**, so `xdotool click` does nothing and press, sleep, release is
      what works. The window is not at the origin either - read the offset from
      `xdotool getwindowgeometry --shell` rather than assuming it.

- [x] **Legato** - `[glide MS]` in the core, TIE in the roll. Asked for as "changing tone
      without stopping the pronunciation", and the answer turned out to be that half of it
      already worked and the other half was wired to the wrong switch.

      **The tone half needed nothing.** Put the same vowel on the next note with no
      consonant in front of it and the formant targets do not move across the boundary, so
      nothing re-articulates. Measured in 25 ms windows, the envelope of two notes on one
      vowel is `0499999...` - indistinguishable from a single held note - where re-singing
      the consonant dips it to `5778`.

      **The pitch half was a step**, and the reason was an accident: `f0_smooth` chases its
      target across phoneme boundaries, but that lives on the `prosody > 0` branch and
      singing turns prosody off, because prosody is speech planning and a written melody
      wants none of it. Pitch smoothing is a different concern that happened to be behind
      the same switch. Turning prosody up is not the fix either - it scoops into *every*
      note change by about 140 ms, which is right for a slur and drunk for a stepwise
      melody.

      So `[glide MS]` belongs to the transition rather than to the voice: a slur is
      something a score asks for on one note, not a mode a singer is in. Geometric, so the
      middle of an interval is where the ear expects it - a fifth from 262 Hz passes
      through 321 and not 327 - which is the same argument `glide_freq` already makes about
      formants. Default 0 is exactly the old behaviour, so nothing existing moved.

      The first note of a phrase is never glided into. It falls out of the smoother being
      seeded on its first frame rather than glided into, and it is the right answer: there
      is nothing to glide from.

      **The tie is where the real work was.** A tied note carries the vowel of the note it
      is tied to - through a chain of ties, since a run of them is one held vowel - and the
      first measurement of it was wrong in a way that only the audio showed: tying onto
      "S T R EY1 T" left the final T at the end of the *first* note, so the tone closed
      before the slur began and the envelope had a gap exactly where the legato was meant
      to be. The coda has to move to the end of the held note: `S T R EY1` then `EY1 T`,
      which is what a singer does.

      Two editor bugs came out of testing it, both worth recording because neither is
      about ties:

      - **Selecting a note moved it.** A press applied the grid snap whether or not the
        mouse then moved, so clicking a note that was not already on the grid nudged it.
        The opening scale was 400 ms notes against a 250 ms grid, so *every* first click
        moved something. Fixed both ends: a drag has to travel two pixels before it counts,
        and the opening scale is quarter notes at its own tempo.
      - **The default view could not show its own scale.** Twelve lanes at 12 px, and C4 to
        C5 is thirteen semitones, so the roll opened with the first note of its own opening
        bars off the bottom. Lanes are 11 px.

Still open, in order:

- [x] **Offline render, and a transport** (`src/host/bm_render.c`, `src/host/bm_player.c`).
      The engine is queue-and-pull: there is no seek in it, and a DAW asks for bar nine
      constantly. Rather than teaching it to seek, render the score once and index into
      what comes out - scrubbing, looping and playing from the middle stop being engine
      problems and become arithmetic.

      **In-process it renders at about 640x real time**: 45 seconds of audio in 70 ms. The
      97x measured earlier was `bm` from a cold start and included loading the dictionary
      and writing a WAV. So re-rendering is affordable, and the split is that measuring a
      score is live on every edit while rendering it happens on SING.

      The two halves are split by what they cost and what thread they can be on.
      `bm_render_score` allocates and runs the DSP; `bm_player_read` allocates nothing,
      locks nothing and is a memcpy with an index. A host with its own transport calls
      `bm_player_locate` each block instead of letting the player advance itself, which is
      the shape the plugin needs - and building it this way round means the standalone and
      the plugin will play through the same code rather than two versions of it.

      The ROLL tab now has a real playhead: drag it in the ruler, SING from wherever it is,
      LOOP, and the head reads from the position of the samples actually going out rather
      than from the frame clock, so it does not drift when a frame is dropped.

      **Two buffers, used alternately.** `bm_render_score` reallocates, and the audio
      thread may be inside a memcpy from the buffer when a new render starts - so the one
      being played is never the one being written, and a buffer a callback could still be
      reading stays alive until the render after next. One spare copy of the audio, and
      the only way this could have crashed is gone.

      It also deleted a duplicate: `SAVE WAV` had its own realloc-and-pull loop, which is
      exactly what `bm_render_score` is. Two of them would have been two chances to get
      the growth arithmetic wrong.

      `tests/test_player.c` covers it with no audio device and no window. The case worth
      naming is a block that spans the loop point: an audio callback hits it a few times a
      second, and a player that zero-fills the tail of that block instead of wrapping
      clicks once per pass - which sounds like a bad loop point rather than like a bug.

- [x] **The CLAP** (`src/clap/bencmouth_clap.c`, `make clap`). A score player: the plugin
      owns the song, the host owns the transport, and the two meet at a position in
      seconds. `process()` is a locate and a memcpy - everything that could stall happens
      on the main thread, and the audio thread never allocates, never locks and never
      touches the engine.

      Located every block rather than free-running, so a loop, a scrub or a click in the
      ruler is followed exactly instead of caught up with. Seconds where the host offers
      them, beats through the tempo where it does not - which is exact while that tempo
      holds and wrong across an earlier tempo change, so a host with a seconds timeline is
      always believed instead.

      **State is the `.bmsong` text**, not a private blob. A project file then contains
      something a person can read, the same bytes open in the editor and in `bm -S`, and
      the format only had to be got right once. That needed `bm_song_format` - the writer
      working on memory with the file as a wrapper, which is the arrangement the parser has
      had since it was written and for the same reason.

      **Two parameters**, and the shortness of the list is the design rather than a gap in
      it. The audio is rendered ahead of the transport, so anything changing how the voice
      sounds means rendering again - fine for an edit, useless for automation. So the
      parameters are the ones that apply to samples that already exist: gain is a multiply,
      sync is a decision about where to read.

      It opens with a song in it. An empty instrument gives no way to tell "installed
      wrong" from "working, and silent because it has nothing to sing".

      `tools/clap_host.c` (`make clap-test`) loads it the way a DAW does and plays it -
      transport, locating, parameters, and a state round trip. It found the thing worth
      finding: the first block at position zero is *silent*, which is the frame generator
      starting from a vocal tract at rest and gliding out of it over about 40 ms. The test
      now asserts that it is quiet at the top and sounding fifty milliseconds later, which
      pins where the transport is rather than merely that noise came out.

      A `.bmsong` can be loaded through CLAP's preset-load extension, which is the only way
      to get a song in until the editor exists.

- [x] **Six things the roll needed once somebody used it.**
      - Both fields are there from the moment the tab opens: the first note is
        selected, and WORD and PHONEMES are drawn empty rather than replaced by
        the words "no note selected" - which answered a question nobody asked
        and hid the two boxes the tab is mostly about.
      - **A scrollbar under the grid.** Sideways used to be shift-wheel and
        nothing else: a gesture with no sign it exists, on the one axis a song
        is long in. It dims when the whole song is already on screen.
      - **Both ends of a note are handles**, the left one moving where it starts
        and the right one how long it is, with the pointer turning into a double
        arrow and two pixels of contrast drawn into the ends. The grab was five
        pixels and invisible, so the note simply moved instead and the gesture
        read as missing.
      - **A right-click menu**: octave and semitone either way, tie, delete.
        That needed a general menu in the widget set - the one there was is four
        fixed clipboard actions belonging to the text boxes.
      - **A note says itself as it is dragged through pitches.** Choosing a note
        by ear is the point of dragging it up and down, and doing that in
        silence means drag, let go, listen, and go back for another try.
      - **Zoom buttons**, and the grid takes whatever height the window has
        spare. The other two tabs are a fixed amount of content and a taller
        band would be empty; a piano roll is the one thing here always short of
        room.

Still open:

- [x] **The editor, in another process** (`src/plugin/bm_shm.c`, `src/plugin/bm_spawn.c`,
      `bencmouth-gui --editor`). The plugin's window is the standalone, started by the
      plugin and attached to a block they share. Floating only: CLAP supports a
      plugin-created top-level window outright, and it is the only thing raylib can do.

      **What crosses is the .bmsong text.** One thing to serialize, and it is the thing the
      file format already describes - no note structs, no versioned binary layout, nothing
      two builds compiled a month apart have to agree about. The worst a mismatch can do is
      a header key one end does not know, which the parser has had an answer for since it
      was written.

      Each channel is a seqlock, and `tests/test_shm.c` tests it the only way that means
      anything: across a real fork, with the child publishing four thousand songs while the
      parent reads. The first version of that test caught seven of the four thousand and
      would have missed a tear in the rest - the reader is a sequence compare and the writer
      is a 64 KB memcpy, so a counted loop finishes long before the writer does.

      **And then it found a real one.** Both the seqlock and the player published across
      threads with `volatile` and a compiler barrier, under a comment asserting that was
      all that was needed "on the architectures this runs on". That is false: `volatile`
      orders the compiler and says nothing about the processor, and arm64 is weakly
      ordered. Four thousand rounds passed on the x86 machine it was written on - x86
      orders stores for you - and the first Apple Silicon runner it met read two halves of
      two different songs.

      The same mistake was in `bm_player_set_source`, where it is worse: a callback that
      saw the new length against the old pointer would read off the end of a shorter
      render, in a DAW, on the audio thread. One real fence now, in `src/host/bm_fence.h`,
      used by both - a second copy would be right until the day one of them changed.

      Worth keeping as the general lesson: a lock-free test that passes on x86 has told you
      nothing about arm64, and CI on both is the only way to find out.

      **The editor is asked to close, not killed**, so it can put its own window away; it
      is killed only if it will not go. An editor that dies leaves the plugin holding the
      last song it sent, which is the right outcome - the music should not disappear
      because a window did.

      Publishing is held back while the mouse is down and for a moment after. Every
      published song costs the plugin a re-render, and a drag that published every frame
      would ask for sixty of them a second and stutter the audio it was editing.

      `make editor-test` runs the real plugin against the real editor under a virtual
      display, and it found the bug this arrangement was always going to have: the plugin
      pumps the editor from `on_main_thread`, which only happens because something asked
      for it - so it pumped exactly once, at the moment the window opened, and never
      noticed another edit. An open editor now asks for the next callback on every one.

- [x] **CI builds and plays the plugin on all three platforms**, and the macOS job asserts
      the three things that make a plugin report as damaged or simply absent - signed,
      universal, no `.dSYM` - before this has ever shipped, because BENCsynth shipped a
      release failing all three. The Linux job additionally starts the editor under Xvfb
      and checks that an edit reaches the audio.

- [x] **VST3 through clap-wrapper** (`cmake/CMakeLists.txt`, `make vst3`), and the AU
      alongside it on macOS. Both are shims: neither contains any BENCmouth, each finds
      `BENCmouth.clap` at run time and loads it, so they install together with it and a
      shim on its own does nothing.

      **The VST3 SDK is pinned to a commit, not taken at tip.** At 3.8.1 the SDK declares
      `iid` in more than one base class and every clap-wrapper up to v0.16.0 fails against
      it - dozens of "reference to 'iid' is ambiguous", not one of which names a version as
      the cause. The pin is the commit the wrapper is known to build against.

      `make vst3-test` asks the built shim what its factory contains, which is the failure
      worth catching: a wrapper that builds and finds nothing has an *empty* factory, and
      in a DAW that is indistinguishable from a plugin that failed to install. Checked both
      ways - with the CLAP installed it offers "BENCmouth", and with it moved aside the
      factory is null and the test fails.

- [x] **The installers carry the plugins.** The Windows installer offers CLAP and VST3 as
      components, machine-wide into `%COMMONPROGRAMFILES%` - not the per-user CLAP path,
      because an elevated installer sees the *elevating* account's `LOCALAPPDATA` and a
      "per-user" install would land wherever the consent prompt was approved. Ticking the
      VST3 ticks the CLAP, since the first is useless without the second. The uninstaller
      reads back where it put them rather than assuming.

      macOS gets a second disk image with every format in it and a script that installs
      each where its host looks, carrying the application as well - the plugin opens its
      window by starting it, so a plug-in installed alone plays a song it cannot show you.

- [ ] **MIDI in, for auditioning a syllable live.** Decided against as the *primary*
      interface and still worth having as a secondary one: MIDI has no channel to carry a
      lyric, so a note list and a lyric list matched by order would come apart the moment
      either was edited - but playing the current syllable from a keyboard while writing is
      a different and much smaller thing.

## Voices of other machines

- [x] **The classic set** - `Compact`, `Announcer`, `Operator`, `Cadet`, `Hushed`,
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
      `Emissary`, `Diver` and `Foreman` as `.bmvoice` files, because each needs an effects
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

- [x] **The roster filled in.** `Frederick` and `Duchess` as presets; `Zarvek`,
      `Carillon` (bell) and `Harmonium` (pipe) as `.bmvoice` files; `songs/boing.bmsong`,
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

## Chorus

- [x] **`chorus` / `chorus_hz`.** The detuned-chorus voice was listed as out of range
      twice - first as needing several engines summed, then again after the comb
      arrived and turned out not to help. A comb is a *fixed* delay: its copies are in
      unison, nothing is detuned, and what you hear is a tube.

      A swept delay is a pitch shift, which is what the Doppler effect is, so three taps
      swept a third of an LFO cycle apart really are three different pitches. Measured:
      energy either side of the fundamental relative to the fundamental itself goes from
      0.007 dry to 0.356 - spread across a band rather than copied, which is the
      difference between a chorus and an echo.

      Three things that were not free:

      - The taps have to be interpolated. A delay that steps by whole samples as it
        sweeps is a train of discontinuities where the smooth shift should be.
      - Divide by sqrt(taps), not by taps. Detuned copies sum incoherently, so three are
        sqrt(3) louder than one; dividing by 3 measured 6.4 dB down against dry.
      - Chorus before drive. Modulation after distortion smears the harmonics the
        distortion just made.

      `Trinode` as an effects preset and as a `.bmvoice` file.

- [x] **Near-miss names.** Where a preset aims at a voice with a well-known name it now
      carries a near-miss rather than the name: `Zarvek`, `Duchess`, `Hushed`. Same rule
      the parameter tables already followed - close enough to say what it is reaching
      for, far enough not to be a copy. `Frederick` is the deliberate exception.

- [x] **Every voice is in the dropdown.** Ten voices existed only as `.bmvoice` files -
      the ones that need an effects chain - on the reasoning that an effect is not a
      property of a speaker and a preset table holding pairs would duplicate one of
      them. Right about the structs, wrong about the shipping: the dropdown lists
      presets, so a voice that is only a file is a voice nobody finds. `Zarvek` was
      reported missing, which is what that looks like from outside.

      They are presets now, with `bm_voice_chain()` naming the chain each belongs
      with - a pairing beside the voice table rather than a field inside `bm_voice`,
      so a chain still composes onto anybody. `-v` brings the chain, a later `-e`
      overrides it, and in the GUI selecting a voice moves the effects column too.
      The six chains that arrived attached to a voice are now effects presets in
      their own right: `Chamber`, `Downlink`, `Alloy`, `Bullhorn`, `Cabinet`, `Klaxon`.

      The files stay, and `tests/test_voicefile.c` compares each against its preset so
      the two copies cannot drift. Writing it found three that already had:

      - `BENCmouth Monotone.bmvoice` had no `flatten = 1`. The file was not monotone; the
        preset of the same name was. It had shipped that way since flatten landed.
      - `Sentry.bmvoice` had no `level`, so it played 3.7 dB below the `Sentinel` chain it
        is meant to use.
      - `preset = retro` renamed the voice that used it. A preset is a whole `bm_voice`,
        name included, and every file puts `name` first - so `Gravel` had announced
        itself as `BENCmouth Retro` for its entire existence.

      Also: the CI step that renders every effects preset had a hardcoded list of six
      and there were thirteen. It enumerates from `bm -l` now, and a companion step
      renders every preset voice.

- [x] **Typable sliders with fine steppers.** The readout beside each slider is a text
      field - click, type, Enter or click-away to commit, Escape to revert - and two
      small arrows step it by whatever precision the format string displays: 0.01 on a
      plain parameter, 1 Hz on a frequency, auto-repeating when held. Deriving the step
      from `fmt` rather than from the range is what keeps the arrow and the number
      honest with each other; a step finer than the display would move a value and
      appear to do nothing.

      The step snaps to the displayed grid before incrementing, so repeated clicks give
      round numbers instead of carrying whatever fraction a drag left behind. Dragging
      now grabs on press and follows the mouse until release, which it had to once the
      readout became a control: a drag that ran off the end of the track used to stop
      dead where the number began instead of pinning to maximum.

      One thing that needed the two states kept apart: text boxes are laid out above the
      sliders and so run first, and clicking one takes `focus` before the slider that
      had it is called. `num_id` outliving the focus change is what lets the slider
      notice it was interrupted and commit rather than discard.

- [x] **The meter reads loudness as well as peak.** The peak meter was misinforming
      anyone who used it to trim gain, and misinforming them hardest on exactly the
      voices most likely to need trimming. Rendering "compliance is mandatory" through
      each voice:

      | Voice | peak | RMS |
      |---|---|---|
      | `BENCmouth` | 0.520 | −18.98 dB |
      | `Gravel` | 0.463 | −19.70 dB |
      | `Trinode` | 0.264 | −17.97 dB |
      | `Aggressor` | 0.124 | −19.98 dB |

      `Aggressor` peaks nearly four times lower than `Gravel` and is one decibel
      quieter. `Trinode` peaks half as high as `BENCmouth` and is a decibel *louder*.
      Drive and crush collapse the crest factor - that is what distortion is - and
      loudness follows RMS, not peak.

      So the bar fills to RMS and peak becomes a marker on top of it, which is how a
      meter that has to be believed is usually built. Accumulated over the whole
      utterance and reset by SPEAK, matching the peak hold rather than being a
      short-time meter: the question being asked is "is this voice louder than that
      one", and that question is asked after both have finished speaking.

      Only samples the engine actually produced are averaged in. Past them the callback
      writes zeros, and a mean that included those would walk toward silence for as long
      as the window stayed open - a maximum does not care, a mean cares a great deal.

      The CLI reports it too, from `bm_wav_report.rms`, because rendering a batch of
      voices through one sentence and reading down the column is where the comparison
      actually gets made - it is how the mismatch above was found. Appended rather than
      inserted, so `LIMITED` stays where anything grepping for it expects. Accumulated
      in double: a long utterance is a few million squares of numbers below one, and a
      float running total stops noticing them once it has grown, which is the classic
      way a mean over a large set comes out quietly wrong.

- [x] **`ring_drift`, for the effect that stops being noticed.** Reported as the Metal
      chain petering out over a long sentence on `BENCmouth Monotone`. Measured across
      5-20 second utterances of four shapes - one long sentence, several sentences, a
      comma-heavy one, and twenty sustained vowels - by three independent measures:

      | | first third | last third |
      |---|---|---|
      | sideband / harmonic energy | 6.3-7.0 | 6.1-7.0 |
      | RMS | 0.063-0.148 | 0.064-0.155 |
      | spectral centroid | 1518 Hz | 1600 Hz |

      Nothing weakens. What the ratio also says, though, is that nothing *changes*: on
      that voice pitch is pinned at exactly 120 Hz with no flutter and no intonation, and
      the carrier is fixed, so the product is strictly periodic. An unvarying stimulus is
      the one attention withdraws from, and the fix for that is not more effect but a
      carrier that will not hold still.

      One cycle every eight seconds - slower than any sentence, so it never repeats
      within an utterance - at 0.119 Hz rather than a round eighth, because the one thing
      it must not do is come back into step with anything. Depth of 0.35 at full setting
      takes a 62 Hz carrier from 40 to 84, across both the half and two-thirds ratios
      against a 120 Hz fundamental, so each crossing is a different pattern rather than a
      louder one.

      Frequency modulation rather than phase modulation, deliberately: modulating phase
      would move the carrier and then return it, and a carrier that comes back is one the
      ear can still learn. The LFO starts a quarter turn in so an utterance opens with
      the carrier moving fastest rather than sitting at a turning point.

      `0` everywhere, so every shipped chain is unchanged and the zeroed-struct bypass is
      still exact. `tests/test_effects.c` drives the stage with a constant - a ring
      modulator fed 1.0 hands back its own carrier - and counts zero crossings: 62.00 Hz
      throughout with drift off, and 82 / 44 / 66 Hz at three seconds apart with it on.

- [x] **The read-only boxes are selectable.** Phonemes, the word translator's output, the
      format reference and the licence text can be clicked into, swept, shift-selected,
      Ctrl-A'd and copied - and cannot be typed into, backspaced or pasted over, because
      each is derived from something else and an edit would vanish at the next keystroke.

      Done by giving the text box an `editable` flag rather than writing a second widget.
      The two differ in about twenty lines - insert, delete, cut, paste - and agree on
      wrapping, the caret, selection, the scrollbar, the arrow keys, Home and End. A
      separate read-only implementation would have been a second copy of all of that, and
      the second copy is the one that stops matching.

      Two consequences worth recording. The context menu drops to COPY and SELECT ALL
      rather than greying two of four rows, which reads as "cannot be edited" instead of
      "broken". And `bm_textview` takes a mutable pointer despite never changing the
      string: copying a selection terminates it in place and puts the byte back, which
      made the song panel's `REFERENCE` a `static char[]` instead of a string literal -
      undefined behaviour for that one instant otherwise, even though the text is
      identical either side of it.

- [x] **`.bmvoice`, and the names on screen agree with the files.** The extension is now
      `.bmvoice`, matching `.bmsong`: `.voice` is a common enough word to collide with
      something else, and a file type worth double-clicking is worth owning a name for.
      Contents unchanged, and the loader never looked at the extension anyway - only the
      dialog filter and the suggested save name did - so old files still load.

      Two naming bugs went with it. The voice dropdown displayed `items[index]`, which is
      whichever preset was last *selected* - so a voice loaded from a file, or one whose
      sliders had been moved since, was labelled with something that was no longer there.
      It displays the live `voice.name` now, and so does the effects dropdown. And saving
      names the voice after the file: it used to write `name = edited` and leave the
      control showing the original preset, so the screen, the file and what you had just
      called it were three different answers.

      Writing the round-trip test for that turned up two fields `bm_voicefile_save` had
      never written: `ring_drift`, immediately, and `level` since the day it was added -
      so saving `Sentry` lost the trim that keeps it from playing 3.7 dB down. Nothing had
      ever written a file and read it back. `tests/test_voicefile.c` does now, over every
      shipped chain and over a synthetic one with a distinct non-zero value in every
      field, because a field is only tested by a value it could lose - every preset has
      `ring_drift` at 0, so round-tripping the presets alone passes 0 against 0.

      The synthetic values are sixty-fourths rather than tenths. An earlier version used
      0.11, 0.22, ... and reported `ring_drift = 0.33 in the file, 0.33 in the preset`:
      `0.11f * 3` is not the float nearest 0.33, so writing it as text and reading it back
      legitimately changed it. The comparison is exact on purpose, so the values fed to it
      have to be ones text can carry without loss.

- [x] **Echo and reverb** — `echo` / `echo_ms`, `reverb` / `reverb_size`, at the end of
      the chain, with `Hall` and `Canyon` as presets.

      Echo is the comb again at a length the ear reads differently: below about 30 ms a
      delayed copy fuses with the original into a resonance, above about 80 ms it
      separates into a repeat. Same mechanism, opposite percept, which is why they are
      two controls and not one knob spanning the gap.

      Reverb is four feedback combs into two allpasses — Schroeder's arrangement, and
      still the cheapest way to turn one impulse into a tail. The comb lengths share no
      factors on purpose: four repeats that coincide at a common multiple give the tail a
      *pitch*, which is the one thing a room must not have. Each comb has a one-pole
      lowpass inside its loop, because a real room absorbs treble faster than bass and
      without that the tail rings metallically all the way down.

      Not a long echo, and the difference is countable rather than descriptive. On a
      single impulse the reverb's response changes sign 1629 times where an echo's
      changes 25, which is what the test asserts — a relative claim, because an absolute
      density threshold would really be a guess about the damping.

      Both are placed after the drive, and both *add* to the dry signal rather than
      mixing against it. That is what an ambience is: the original arrives unaltered and
      the room arrives afterwards. It is also what makes them run hot, so both carry a
      measured trim - echo lands 2.1 dB under dry at full, reverb 3.5 dB over.

      Two things had to change around them:

      - **The engine's tail.** It rendered a flat 100 ms past the last frame, which is
        right for the resonators and wrong by an order of magnitude for anything with
        feedback: a 330 ms echo was cut off inside its first repeat, and a rendered file
        was *exactly as long* with the effect as without it. `bm_effects_tail_ms` now
        reports the ring-out from the parameters - time for the loop to fall 40 dB, not
        60, since the last twenty are inaudible over the next sentence - capped at two
        seconds so a feedback near 1 cannot ask for minutes of decaying silence.
      - **The storage budget.** The two delay lines are 44 KB between them and the engine
        went from 31 KB to 76 KB, so `BM_ENGINE_RESERVED` doubled to 131072. The embedded
        path is unmoved: `-DBM_WITH_EFFECTS=0` removes every buffer along with the stage
        and the engine measures 19,120 bytes, so a microcontroller build sets the reserve
        to 24576. `BM_ECHO_LEN` and `BM_REVERB_LEN` are the middle option.

      **The effects column scrolls.** Fourteen controls will not fit in the ten rows a
      column has, and the two ways to make them fit both cost something permanent: a
      taller window runs out of screen, and a fourth column cost 100 px of width and a
      940 px minimum. Scrolling one column costs a 12 px gutter.

      It is also the arrangement that keeps working. The effects list has grown five
      times now, and a layout that has to be redrawn on each occasion is a layout that
      will be wrong again. `bm_scroll_rows` moves by whole rows rather than pixels,
      which is not cosmetic: a half-scrolled row is still a live control with its
      rectangle hanging outside the visible band, so clicks meant for the buttons above
      would land on a slider nobody can see. Whole rows mean the ones out of range are
      simply not drawn. Slider ids follow the parameter rather than the screen row, so a
      number being typed into keeps its identity while the column moves under it.

- [x] **A vocoder** — `vocoder` / `vocoder_hz`, between drive and crush, with `Vocoder`
      and `Chorale` as presets.

      The first effect here that does not alter the voice. Sixteen third-octave channels
      measure how loud it is in each band; a carrier generated inside the stage is cut
      into the same sixteen bands and each one is turned up or down to match. Nothing of
      the input reaches the output — only the sixteen numbers — which is why the output
      carries the words and the carrier's pitch, and why an effect placed ahead of it
      survives to the output only insofar as it moved energy between bands.

      Almost every constant in it is a measurement, and three of them were surprises:

      - **Three sections a channel, not two.** A channel's skirts are the steepest
        spectrum it can report: where the voice falls off faster than the filter does, a
        channel stops measuring its own band and starts measuring what has leaked in from
        its louder neighbours, and the vocoder reproduces something flatter — brighter —
        than it was given. Against the voice it came from: 4-pole is +1.9 dB/octave out,
        6-pole is +0.7, 8-pole is also +0.7 and costs a third more arithmetic. Six poles
        also happens to land the output's crest factor on the voice's own 13.2 dB, which
        is what lets the two be levelled against each other at all.

      - **The carrier is a differentiated sawtooth, and each band is then weighted by
        `sqrt(f_low/fc)`.** The bands are constant-Q, so the top one is 42 times wider
        than the bottom one and collects 42 times as much of a flat carrier. Measured
        through the bank: a sawtooth arrives 17 dB down across the range, its derivative
        16 dB up, and a carrier has to sit at the geometric middle of those. Differencing
        the sawtooth makes it flat and the per-band weight then takes the bandwidth back
        out — and because *both* halves of the carrier are flat, one weight serves the
        noise as well.

      - **Both halves of the carrier are normalised to unit RMS, and the output by the
        square root of the sample rate.** Neither is cosmetic. The differentiator turns
        each cycle into one impulse of fixed area, so an untrimmed carrier is 3 dB
        quieter at 55 Hz than at 220 — a pitch control acting as a volume control. And
        what a channel collects is power *density*, so the same carrier is thinner
        everywhere at a higher rate: before the rate term, a 300 Hz tone peaked at 1.28
        at 8 kHz against 0.55 at 44.1. With both in, five rates land within 1%.

      The carrier switches to noise when the voice stops being voiced, decided from the
      share of the envelope sum above 2.5 kHz — 0.03 or less on vowels and nasals, 0.56
      to 0.91 on the voiceless fricatives and stop bursts, which is two orders of
      magnitude and does not need a clever detector. `/z/` and stop bursts sit in the
      crossfade, correctly: one is a fricative with voicing in it, the other is a
      transient on its way into a vowel.

      What it costs: 96 biquads a sample, by a distance the most expensive thing in the
      library, against five for the entire vocal tract. Only 272 floats of state, though —
      no delay lines — and `-DBM_WITH_EFFECTS=0` removes all of it.

      Recorded rather than fixed: on high-pitched voices it runs 1 to 3 dB hot and its
      peaks reach the host limiter — Deep at −0.9 dB against Duchess at +2.4. A high
      fundamental puts at most one harmonic in each low channel, so more of the bank is
      driven independently and more of it lines up. Correcting it would need a function
      of the input's pitch, which is exactly the thing the vocoder is built not to know.

- [x] **Tempo is a control, and it does something.** Song mode showed a tempo but had no
      way to change it, and changing it would not have mattered: nothing outside the
      readout read `song.tempo`. Grep confirmed it - stored, saved, loaded, displayed, and
      inert. The shipped songs each had their holds written against their own declared
      tempo, so the number was accurate documentation and nothing else.

      It now means one thing: **the tempo the score's `[hold]` values are written at.**
      Two sliders, tempo in BPM and the quarter in milliseconds, because both are numbers
      people arrive with - a tempo is what a song *is*, a quarter in ms is what actually
      gets typed into a `[hold]`. They are one value; `tempo` is what is stored, and
      setting the quarter stores 60000/quarter without rounding it to a tidy BPM, because
      rounding would make the quarter you just asked for snap to something else.

      Changing it **rewrites every `[hold]` in the score** by the same ratio. Retiming the
      text rather than scaling at playback is what keeps the file self-describing: the
      header and the holds always agree, so reloading gives back exactly what was heard.
      Clamped to the 1..10000 ms the markup parser accepts, so a drag to an extreme cannot
      produce a score that will no longer sing.

      Applied once the gesture is over rather than per frame - a slider reports a change
      every frame it is dragged, and rescaling sixty times a second walks 260 down a
      millisecond at a time. Rounding is only harmless when it happens once.

      Measured end to end: Daisy Bell runs 11.88 s at 116 BPM, 9.93 s at 160 and 15.15 s
      at 80. Not a linear scale, because consonants keep their own length - which is what
      singing does too, and why `[hold]` was vowels-only from the start.

- [x] **`WIN_MIN_H` is measured rather than guessed**, and it had been guessed. The layout
      is computed top-down, so its height does not depend on the window: anything shorter
      than the total simply loses the bottom of it. The old 740 was about twelve pixels
      short of what the layout already needed, so the minimum window size had been
      quietly clipping the status line - the one row that says what the program is doing.
      Rendered at a range of heights to find where the last row comes back whole: 786.

- [x] **A resonance above the representable band is bypassed, not moved.** Found while
      trying to render a song at 7 kHz to match a reference recording's band limit: the
      output came back with a peak of 458. Swept, it was not a cliff but a slope - peak
      0.6 at 22050, 0.6 at 11025, 2.1 at 8000, 458 at 7000, 1968 at 6000. Finite, and all
      of it wrong.

      Not instability. The frequency clamp already keeps every pole inside the unit
      circle. It is the *normalisation*: these sections are unity-gain at DC, which is
      what lets five chain without a makeup stage, and that is only harmless while the
      poles stay away from Nyquist. As a pole approaches it the gain at the resonance
      grows relative to DC without bound, and five of those multiply.

      Isolated by phoneme class, which is what named it: at 8 kHz a vowel and a fricative
      both ran away and a nasal did not - the nasal has no high formant.

      | formant / rate | 0.31 | 0.34 | 0.37 | 0.40 | 0.42 | 0.44 | 0.47 |
      |---|---|---|---|---|---|---|---|
      | peak | 0.68 | 0.67 | 0.71 | 0.69 | 0.86 | 1.15 | 2.14 |

      Flat to 0.40 and then away, so the threshold is 0.40 - measured, not a round number
      that happens to look like one. Above it the section is bypassed rather than clamped
      down to fit, which is also the honest answer: a formant the rate cannot represent
      should be absent, not folded onto the top of the band where it colours everything
      below it.

      Inert at every rate this project ships - the highest formant in the table is about
      3020 Hz and `mouth` tops out at 1.4, against a threshold of 8820 at 22050 - so
      `BENCmouth Retro`'s golden reference does not move, and 22050 and 16000 render
      bit-identically to before. Every rate from 4 kHz up now sits between 0.55 and 0.64
      where they used to reach four figures.

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

  This used to end by proposing a look-ahead normalisation pass in the host layer as
  "the real fix", on the reasoning that peak level is a property of the sentence as much
  as of the voice and no constant gain is right for both a word and a paragraph. That was
  built, measured and **not shipped**, and the answer is worth more than the proposal was.

  It worked. A 4 ms look-ahead with a sliding-window minimum and a 120 ms release held
  the ceiling at 0.841 against the soft clip's 0.85 knee, so the clip never engaged at
  all; on voices that never reach the ceiling it was bit-for-bit inert, 0 of ~110,000
  samples differing, once its own latency was compensated. Duchess through the vocoder
  went from 797 clipped samples to none.

  It was inaudible. The premise was that memoryless clipping of an isolated plosive
  leaves an impulse-shaped error - and it does, measurably: 75 samples of 113,965 on the
  test sentence, the error 27.4 dB below the signal, worst single sample 0.368. But
  listened to against the unlimited render, the difference is not reliably identifiable;
  what does come across is the level, 0.1 to 0.4 dB softer in the peaks, which is the
  limiter doing arithmetic rather than an artifact going away.

  So the trade was 4 ms of latency, ~4 KB of state, a sliding-window deque, a CLI flag
  and a slightly quieter output, against a defect nobody can hear on this material. 75
  clipped samples in five seconds measures worse than it sounds, which is roughly what a
  synthesizer with this much deliberate buzz in it should do.

  What would change the answer: material that clips far more than this does, or an
  effect that pushes harder than the vocoder. If either turns up, the design above is
  known to work and is worth rebuilding rather than rethinking.

  Two things went wrong while measuring, recorded because both are easy to repeat.
  Energy above 7 kHz is useless as a distortion metric here - it is dominated by the
  voice's own fricatives and reported the clipped and limited paths as identical when
  they are not; the error signal itself is the thing to measure. And a look-ahead
  limiter that takes its target from the incoming sample alone does not work: a peak is
  often one sample wide, so the very next sample asks for no reduction, the gain turns
  round and has recovered by the time the peak reaches the output. 1.367 came out as
  0.965. It needs the minimum across the whole window.

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
