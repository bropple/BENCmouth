# Classic voices

The voices that shipped with desktop machines in the eighties and early nineties are
the reason a lot of people have any opinion about speech synthesis at all. This is an
account of which of them BENCmouth can reach, which it cannot, and — the part that
turned out to be more interesting — *why* the ones it cannot reach are out of range.

## The rule followed

These are tuned toward **archetypes**, from the published acoustics of the voice types
involved: formant frequencies and bandwidths for adult male, adult female and child
tracts, measured open quotients and spectral tilts for breathy versus pressed
phonation, measured vibrato rates.

Nothing was disassembled, decompiled, or measured out of a shipped synthesizer, and no
parameter table was lifted from one. That is the same rule the rest of this project
follows — papers and published data in, other people's source code out — and it is
also why the presets carry their own names rather than anybody else's.

## What the engine reaches

Six presets, all selectable as `bm -v NAME`, in the GUI dropdown, and as `.voice`
files under `voices/`.

| Preset | Archetype | What carries it |
|---|---|---|
| **Compact** | The all-in-one machine on the desk, 1984 | Source, not tract: `tilt 2.0` and `open_quotient 0.42` give the hard buzz, `f0_flutter 0.10` holds the pitch dead still. Naturalness controls all off — it is a Retro-family voice. |
| **Announcer** | The deep one | `throat 0.74 / mouth 0.80` lengthens the tract; `tilt 10.0` is what turns a low voice from a buzz into a rumble, and does more work here than the pitch. |
| **Operator** | The professional female voice | A female tract is roughly 15% shorter, and measurably breathier — Klatt & Klatt (1990) found both open quotient and aspiration higher. `breathiness 4.0` carries more of the impression than `f0_base 200` does. |
| **Cadet** | The child | `throat 1.26 / mouth 1.32`, further than any adult setting, because a child's tract really is that much shorter. Pitch alone lands somewhere between "child" and "adult on helium". |
| **Whisper** | The whisper | `whisper 1.0` — voicing fully traded for turbulence. See below. |
| **Rattled** | The one that came loose | `f0_flutter 0.85` *and* `vibrato 1.6 st @ 8.5 Hz`. Both, because aperiodic drift alone reads as a bad recording and periodic modulation alone reads as singing. |

Measured formant placement for /a/, last frame of `AA1 AA1 AA1`:

| Preset | F1 | F2 | F3 |
|---|---|---|---|
| BENCmouth Retro | 730 | 1090 | 2440 |
| Announcer | 540 | 849 | 1937 |
| Compact | 745 | 1140 | 2577 |
| Operator | 832 | 1271 | 2869 |
| Cadet | 920 | 1416 | 3206 |

Against measured speech, adult male /a/ sits near 730/1090 and adult female near
850/1220, so Operator lands where it should. Cadet is closer to an older child than a
small one; the two sliders are there to push it.

Every preset peaks between 0.40 and 0.61 against a limiter threshold of 0.85, so none
of them clips.

### Two engine parameters existed for this

**`whisper` (0..1)** was added because the archetype was genuinely unreachable
before it. `breathiness` *adds* aspiration alongside phonation and leaves the folds
working; whispering is not breathy speech — the folds do not vibrate at all, so there
is no fundamental, and the formants are excited by glottal turbulence instead. At 1
the voiced branch is silent.

The calibration was measured rather than chosen. Aspiration and voicing at the same dB
are nowhere near the same loudness through this synthesizer, because noise drives five
cascaded resonators far harder than a pulse train does. A 6 dB trade put whispered
vowels 10 dB *above* the unvoiced fricatives in the same sentence and peaked the
utterance at 1.83. At 16 dB, whispered vowels land at the same RMS as `/s/ /sh/ /f/
/th/` — 0.0342 against 0.0348. That equality is the acoustic signature of whispering,
and it is why whispered speech sounds as though it is made almost entirely of
consonants: it is.

`tests/test_voices.c` holds it to the definition rather than to a level: the best
normalized autocorrelation over 70–200 Hz drops from 0.99 to 0.33 with whisper on. The
fundamental is gone, which is the whole claim.

**`vibrato` / `vibrato_rate`** were added for two reasons at once — the wobbling
novelty archetype above, and sung notes, which need it far more. Depth is in semitones
because that is how the excursion is heard: a 3 Hz wobble is enormous at 80 Hz and
inaudible at 400. The default rate is 5.5 Hz, where measured singer vibrato clusters
almost regardless of voice type.

Both default to off in every existing preset, so the BENCmouth Retro golden reference
is byte-for-byte unchanged. That is the contract, and it held.

## What the engine does not reach, and why

Grouped by cause, because the causes are different and only some of them are
interesting.

### 1. Voices whose source is a musical instrument — **now reachable**

*This section used to say the excitation was fixed and that a bell was therefore out of
range. It is not fixed any more.*

The problem was real and it was one line of architecture: `bm_glottis.c` produced the
derivative of glottal volume velocity and nothing else. A bell is not a glottal pulse
train under any setting of open quotient or tilt.

`bm_voice.source` selects it, 0..2 — folds, pipe, bell — and **crossfades**, because
the interesting settings turn out to be between the corners. A discrete three-way
switch would have been easier to write and worse to use.

The bell table is the measured partial series of a church bell:

| ratio | name | |
|---|---|---|
| 0.50 | hum | an octave below the strike note |
| 1.00 | prime | |
| 1.19 | tierce | a minor third — **not a harmonic of anything** |
| 1.50 | quint | |
| 2.00 | nominal | what you think the pitch is |
| 2.66 | superquint | |

The tierce is why it reads as a bell rather than as a detuned chord, and the
inharmonicity is the whole mechanism: with no resolvable fundamental the ear hears a
strike instead of a pitch. Measured at a steady 150 Hz, energy at 0.5× and 1.19× the
fundamental rises from 14 and 25 dB on the glottal source to 58 and 61 dB on the bell —
a 44 dB shift into frequencies a harmonic source cannot produce at all. The pipe stays
harmonic at both, which is the control.

All three sources are level-matched to within 0.1% RMS, measured rather than derived:
six sines summed are not as loud as their coefficients suggest, and the glottal pulse
they have to match is a sparse spike train whose RMS is nothing like its peak.

**The vocal tract is untouched.** A bell-sourced voice still has formants and still says
the words, which is exactly what the classic instrument voices were — not an instrument
playing instead of speech, but an instrument doing the speaking. `voices/Carillon.voice`
and `voices/Harmonium.voice`.

Two details worth recording. The partials need their own phase accumulators rather than
being derived from the fundamental's: `sin(2π · phase · ratio)` looks equivalent and is
not, because `phase` wraps at 1 and a non-integer ratio then steps discontinuously at
every wrap — a click at the fundamental frequency, which is a buzz exactly where the
source is meant to be smooth. And they start spread around the cycle rather than all at
zero, because six sines starting in phase sum to one large spike on the first sample.

### 2. Voices whose character is modulation — **now reachable**

*This section used to say the engine could not do this and deliberately would not. It
can now, and the way the objection was resolved is the interesting part.*

The robot-alien end of the classic set gets its identity from ring modulation applied
to the output — a carrier multiplied into the speech, which replaces every component
with a pair of sidebands either side of the carrier. The result is inharmonic, and
that is exactly why it does not sound like a person: the ear has no fundamental to
lock onto.

The original objection stands and was never about difficulty: **an effect is not a
property of a speaker.** Ring modulation is not a fact about anybody's larynx, and
putting it in `bm_voice` would have been the first field in that struct that was not
a claim about a vocal tract.

The resolution was to give effects their own struct. `bm_effects` sits beside
`bm_voice` in `bm_config`, with its own presets, its own `set_param`, and its own
`bm_engine_set_effects()`. That keeps the distinction the objection was protecting —
and it turned out to be the more useful arrangement anyway, because the two now
**compose**. Any effect works on any voice, so a metallic ring on the announcer and
the same ring on the child are one dropdown apart, rather than two more entries in a
preset table that would otherwise have to hold every combination.

Four effects ship, in a fixed chain:

| Stage | What it is | What it is for |
|---|---|---|
| `ring` / `ring_hz` | multiply by a sine carrier | inharmonic sidebands — the metallic, inhuman edge |
| `comb` / `comb_hz` | feedback comb, evenly spaced resonances | speaking through a metal tube, literally |
| `drive` | cubic soft clip with pre-gain | harmonics that were not there — *aggression* |
| `crush` | hold every Nth sample | aliasing; the sound of a converter that could not keep up |

Order matters and it is `ring → comb → drive → crush`: ring modulation on the clean
voice keeps its sidebands distinct, the comb adds the resonance, and the drive then
saturates everything above it, which is what makes a robot sound angry rather than
merely mechanical. Crush is last because it is the digital layer, applied to a
finished sound.

Two presets are the point of the exercise. **Sentinel** is the metallic sentry — ring
for the inharmonic edge, a low comb, only a little drive, because it is meant to sound
inhuman rather than angry. **Enforcer** is the aggressive one, and drive carries it at
0.88 with a tight comb and only a trace of ring; too much ring turns menace into
novelty. `voices/Sentry.voice` and `voices/Aggressor.voice` pair each with a voice.

`-DBM_WITH_EFFECTS=0` removes the whole stage, and that is worth doing on a
microcontroller: it drops the code *and* the comb delay line, which is 8 KB and the
only sizeable buffer in the library. The engine measures 27,184 bytes with effects and
18,976 without.

Three things had to be measured rather than chosen, and all three were wrong on the
first attempt:

- **The drive level compensation.** A distortion that is merely louder is not
  distortion. Measured RMS jumped 2.6x by a drive of only 0.2 and then *fell* as
  compression took over, so a straight-line trim was hopeless — the correction needs
  a sharp knee near zero and almost none after. `1/(1 + 4.5·drive^¼)` holds the level
  flat to within 3% across the whole range. The fourth root is two `sqrt` calls, not a
  `pow()` the core does not have.
- **The comb normalisation.** A feedback comb has a peak gain of `1/(1-fb)` at
  resonance, so the wet signal is scaled by `(1-fb)`. Folding that into the *mix*
  instead — which is what the first version did — left the output 78% dry at the
  full setting, and the comb measured 2 dB of tooth-to-notch depth where it should
  have had 13. It is now 25x (1.158 against 0.046 on a rendered spectrum).
- **An output level after the chain.** Voice `gain` is applied *before* the effects,
  which is correct, because `drive` is a threshold effect and a drive stage that saw
  an untrimmed signal would fold hard on a loud voice and do nothing on a quiet one.
  But that same ordering makes the gain slider almost inert once drive is up. So:
  input trim, chain, output level — the topology of every overdrive pedal ever built,
  arrived at for the same reason.

**One thing measurement caught that listening reported first.** The `Aggressor` voice
file was originally built on the `Announcer` preset, and came back as "aggressive, but
muffled so much it's nearly unrecognizable". The instinct is to blame the drive; the
band energies say otherwise:

| | <500 Hz | 500–2 kHz |
|---|---|---|
| BENCmouth (a legible voice) | 91.2% | 8.6% |
| Announcer at 82 Hz, through Enforcer | 94.6% | 3.6% |
| the same voice **dry** | 98.6% | 1.1% |

Announcer is a rumble *by design* — `tilt 10` and a long tract — and dropping its pitch
to 82 made it more so. Nothing above 500 Hz survived to carry the words, and the drive
was the only stage putting any brightness back at all. The fix was in the voice, not
the chain: a near-neutral tract and `tilt 2`, which lands at 86.7% / 12.3% and is now
slightly brighter than the default voice.

The general lesson is worth keeping: **a heavily driven signal inherits its
intelligibility from whatever it was driven from.** The waveshaper adds harmonics; it
cannot invent a formant band that was not there.

Every preset is level-matched to within 1.7 dB of the dry signal and clear of the
limiter; `tests/test_effects.c` asserts both, along with the one that actually
matters — that an all-zero chain is **bit-for-bit** the unprocessed signal, 0 samples
differing out of 30,140. That is what let a whole new stage land in the signal path
without moving BENCmouth Retro's golden reference by a bit.

### 3. Voices that are several voices at once

At least one classic novelty voice is a chorus — the same utterance rendered two or
three times at slightly different pitches and summed, which is what gives it the
distinctive detuned-alien quality.

One `bm_engine` renders one voice. But this gap is not real: **the host can already do
it.** Three engines, three copies of the voice with `f0_base` a few hertz apart, three
`bm_read()` calls summed and scaled. The engine performs no allocation and holds all
its state in caller-supplied storage precisely so that having three of them is
unremarkable. Roughly 30 lines in `src/host/`, none of it in core.

### 4. Voices that are not voices

Two of the best-known classic novelty voices announce everything on a fixed melody —
one funereal, one cheerful. These are not voice settings at all. They are *scores*: a
sequence of notes with the text mapped onto them.

BENCmouth already does this, and it is what song mode and the `.bmsong` format are
for:

```
[hold 300][note E4] B AE1 D  [note E4] N UW1 Z
```

So this category is reachable today, just not through the voice dropdown — which is
the correct place for it not to be.

### 5. The concatenative voices

The higher-quality voices of that era, and every mainstream voice since, are not
synthesized in this sense at all. They are recorded human speech, cut into diphones or
larger units and reassembled. There is no vocal tract model, no formants, no source —
there is a database of audio and a unit-selection algorithm.

BENCmouth cannot approach them and never will, because it is not the same kind of
program. A formant synthesizer's entire state is a few hundred bytes of parameters and
it runs on a microcontroller; a unit-selection voice is tens of megabytes of audio.
That trade is the point of the project, not a shortfall in it.

This is worth stating plainly because it is the honest answer to "can we replicate the
classic voices": the *formant* ones, yes, to the extent that any two independently
tuned formant synthesizers can sound alike. The recorded ones, no, and no amount of
parameter work changes it.

### Still out of range

**A detuned chorus.** One `bm_engine` renders one voice, and a chorus is several
slightly-detuned copies of the same utterance summed. A comb filter is a fixed delay and
sounds like a tube, not like three singers. This remains a host-layer job: three engines,
three copies of the voice a few hertz apart, three `bm_read()` calls summed — the engine
allocates nothing and holds all its state in caller-supplied storage precisely so that
having three of them is unremarkable. Roughly 30 lines, none of it in core, not yet
written.

**Struck decay.** The bell source sustains for as long as the phoneme does. A real bell
is struck and decays, and getting that would mean an envelope retriggered per syllable —
which the source cannot see, because it is handed a pitch and an amplitude and knows
nothing about phonemes. The per-phoneme amplitude envelope gives each syllable an onset,
which carries some of it.

## Summary

| Classic voice family | Reachable | Why not / how |
|---|---|---|
| Small formant voices — male, female, child, deep | **Yes** | Six presets, shipped |
| Whisper | **Yes** | Needed a new `whisper` parameter |
| Unstable / wobbling | **Yes** | Needed `vibrato`; flutter alone was not enough |
| Instrument-source (bells, organ) | **Yes** | `source` selects folds, pipe or bell, and crossfades |
| Ring-modulated robot | **Yes** | `bm_effects`, kept separate from `bm_voice` so effects compose with voices |
| Aggressive / distorted | **Yes** | `drive`; the harmonics it invents are what "aggressive" is |
| Detuned chorus | Not shipped | Still needs several engines summed; one engine renders one voice |
| Fixed-melody announcements | **Yes** | It is a `.bmsong`, not a voice |
| Concatenative / recorded | No | Different kind of program entirely |

## The part that needs ears

Every number above is measured, and none of it is *heard* — the tuning was reasoned
from published acoustics and verified against spectra and levels, not against a
listener. Formant placement being correct and a voice sounding right are related but
not the same thing, and the second one is a judgement no measurement makes.

`make bm && sh tools/classic-voices.sh` renders all six through the same sentence,
with BENCmouth and BENCmouth Retro alongside for reference.
