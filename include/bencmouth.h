/*
 * BENCmouth - a formant speech synthesizer
 *
 * Public API. This header is freestanding-safe: it includes only <stddef.h> and
 * <stdint.h>, declares no I/O, and the library behind it performs no dynamic
 * allocation. All state lives in caller-provided storage.
 *
 * Two ways to drive it:
 *
 *   1. Queue text or phonemes with bm_speak_*(), then pull PCM with bm_read()
 *      until bm_is_speaking() returns 0. This is a pull model on purpose - every
 *      real audio API is callback-driven, and a pull model drops straight into
 *      one without an intermediate thread or ring buffer.
 *
 *   2. Use bm_text_to_phonemes() alone if you only want the front end.
 */

#ifndef BENCMOUTH_H
#define BENCMOUTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The one place a version number is written down. The GUI's ⓘ panel reads it,
 * and tools/check_version.sh holds a release tag to it - v0.2.0 and v0.2.1
 * both shipped an About box saying 0.1.3, because nothing compared the two. */
#define BM_VERSION_MAJOR 0
#define BM_VERSION_MINOR 2
#define BM_VERSION_PATCH 4

/* ------------------------------------------------------------------ *
 * Compile-time configuration
 *
 * Because there is no malloc, queue capacities are fixed at build time.
 * Override any of these with -D to trade RAM against maximum utterance
 * length; the defaults suit a hosted build with room to spare.
 * ------------------------------------------------------------------ */

#ifndef BM_MAX_PHONEMES
#define BM_MAX_PHONEMES 512     /* phonemes buffered per utterance */
#endif

#ifndef BM_MAX_TEXT
#define BM_MAX_TEXT 1024        /* bytes of normalized text buffered */
#endif

/* Number of formants in the cascade branch. Five is the usual choice for
 * 16 kHz and above; three is enough below ~10 kHz and much cheaper. */
#ifndef BM_NFORMANTS
#define BM_NFORMANTS 5
#endif

/* Output sample type. Float is the default and the reference path. The
 * typedef is the seam for a future fixed-point build - note that flipping it
 * is necessary but not sufficient, since the DSP inner loops need Q-format
 * multiply macros as well. Keep that in mind before scattering bare '*'
 * operators through the synthesis code. */
#ifndef BM_SAMPLE_FLOAT
#define BM_SAMPLE_FLOAT 1
#endif

#if BM_SAMPLE_FLOAT
typedef float bm_sample;
#else
typedef int16_t bm_sample;
#endif

/* ------------------------------------------------------------------ */

typedef enum bm_result {
    BM_OK             =  0,
    BM_ERR_ARG        = -1,  /* null pointer or nonsense parameter */
    BM_ERR_OVERFLOW   = -2,  /* input exceeds a BM_MAX_* capacity */
    BM_ERR_BUSY       = -3,  /* still speaking; drain or reset first */
    BM_ERR_UNSUPPORTED= -4   /* e.g. unknown phoneme in bm_speak_phonemes */
} bm_result;

/* ------------------------------------------------------------------ *
 * Voice
 *
 * A voice is plain data with no pointers into mutable state, so it can live
 * in .rodata, be embedded in firmware, or be serialized. This is the struct
 * you edit to make BENCmouth sound like BENCmouth rather than like a generic
 * formant synthesizer - it is the personality, and it is where the character
 * of the thing will actually come from.
 * ------------------------------------------------------------------ */

typedef struct bm_voice {
    const char *name;

    float f0_base;       /* Hz. ~120 masculine, ~210 feminine, ~90 for a growl */
    float f0_range;      /* semitones of intonation excursion; 0 = monotone robot */
    float f0_flutter;    /* 0..1, quasi-random F0 drift; a little kills the buzz */

    /* Vibrato: a *periodic* pitch modulation, which is a different thing from
     * flutter and is why it is a separate pair of numbers rather than a mode.
     * Flutter is three incommensurate oscillators summed so the pattern never
     * repeats - it exists to stop a sustained vowel sounding like a test tone.
     * Vibrato is one oscillator you are meant to hear, and hearing it is the
     * point: it is what a held note needs to read as sung rather than beeped.
     *
     * Depth is in semitones because that is how the excursion is heard - a
     * 3 Hz wobble is huge at 80 Hz and inaudible at 400. Rate at 0 means the
     * default below, so a voice file can ask for vibrato with one key. */
    float vibrato;       /* semitones of peak deviation; 0 = off */
    float vibrato_rate;  /* Hz; 0 selects the default, about 5.5 */

    /* What excites the vocal tract, 0..2.
     *
     * The excitation was fixed for a long time, and CLASSIC-VOICES.md listed
     * every instrument-source novelty voice as unreachable because of it. This
     * is that limitation lifted:
     *
     *   0   the glottal flow derivative - a pair of vocal folds
     *   1   a harmonic stack - drawbar ratios, sustained, no closure
     *   2   an inharmonic stack - the partials of a struck bell
     *
     * Continuous rather than a three-way switch, and it crossfades: 0.5 is half
     * folds and half pipe, 1.5 is a pipe with a bell's inharmonicity creeping
     * in. A discrete selector would have been easier to implement and worse to
     * use, because the interesting settings turn out to be between the corners.
     *
     * The bell ratios are the measured partials of a church bell - hum at 0.5,
     * prime at 1, tierce at 1.19, quint at 1.5, nominal at 2 - which is what
     * makes the result read as a bell rather than as a detuned chord. Being
     * inharmonic is the whole point: there is no fundamental for the ear to
     * resolve, so it hears a strike instead of a pitch.
     *
     * The tract does not change. A bell-sourced voice still has formants and
     * still says words; it is a bell being made to speak, which is exactly what
     * the classic novelty voices were. */
    float source;

    float speed;         /* phoneme duration multiplier; 1.0 = nominal */

    /* Vocal tract shape, as multipliers on formant frequencies.
     *
     * Two axes rather than one overall scale, because a single scale moves
     * every formant together and loses most of the range that distinguishes
     * one speaker from another. Roughly: `throat` governs the pharyngeal
     * cavity and therefore F1, `mouth` governs the oral cavity and therefore
     * F3 and above, and F2 answers to both. 1.0 is neutral; below 1.0
     * lengthens the cavity (deeper, larger), above shortens it.
     *
     * Two knobs a person can turn beats five independent numbers - the point
     * is to be tunable by ear, not to be maximally expressive. */
    float throat;
    float mouth;

    /* Voice quality, per Klatt & Klatt 1990. */
    float breathiness;   /* dB of aspiration mixed into voiced segments */
    float tilt;          /* dB of spectral downtilt; higher = softer, less buzzy */
    float open_quotient; /* 0..1 fraction of the period the glottis is open */

    /* How much voicing is traded for turbulence, 0..1.
     *
     * Distinct from `breathiness`, which *adds* aspiration alongside phonation
     * and leaves the vocal folds working. Whispering is not breathy speech: the
     * folds do not vibrate at all, so there is no fundamental, and the formants
     * are excited by glottal turbulence instead. At 1 the voiced branch is
     * silent and the cascade is driven by noise alone.
     *
     * The voiced/voiceless distinction goes with it - a whispered "bat" and
     * "pat" really are near-identical, and that is the effect being modelled,
     * not a defect in it. */
    float whisper;

    /* Output level, linear. 1.0 is nominal.
     *
     * Needed because peak level is not a property of the phoneme table alone.
     * A voice with no pitch flutter drives the cascade with a perfectly
     * periodic pulse train, the resonators ring in lockstep, and peaks build
     * constructively - BENCmouth Monotone reached 0.96 where voices with
     * flutter sit near 0.55. This is the per-voice trim for that.
     *
     * Applied at the synthesizer output rather than as an offset to the source
     * amplitudes: those are dB values with a floor at 0 meaning silence, so a
     * negative trim applied there would silence quiet-but-audible branches
     * outright instead of attenuating them. */
    float gain;

    /* ---- naturalness controls ----------------------------------------
     *
     * Every one of these defaults to the cruder behaviour, and that is a
     * deliberate contract, not an accident of ordering: a naturalness feature
     * that cannot be switched off is a feature that has silently taken the
     * retro voice away. Anything added here later must come with an "off"
     * setting that reproduces what BENCmouth did before it existed.
     */

    /* How far phonemes fall short of their targets under the influence of
     * their neighbours. Real articulators never arrive - in connected speech
     * a vowel between two consonants never reaches where it was heading.
     * 0 hits every target exactly, which is crisp, mechanical, and exactly
     * what an early-eighties synthesizer sounds like. */
    float coarticulation;

    /* Phrase-level intonation, 0..1. At 0 the pitch contour is what BENCmouth
     * had before bm_prosody.c existed: a single linear decline across the whole
     * utterance plus a flat bump on stressed phonemes. Above 0 the contour is
     * planned per phrase, with pitch accents, boundary tones that distinguish a
     * question from a statement, and phrase-final lengthening.
     *
     * Scales together with f0_range, which is the excursion in semitones. */
    float prosody;

    /* How formant transitions are spaced, 0..1.
     *
     * At 0 a glide from 300 Hz to 2300 Hz moves linearly in hertz, which is
     * what BENCmouth always did and is not how the ear hears it: linear-in-Hz
     * spends most of its time in the top of the range, so the transition sounds
     * like it lurches up and then dawdles. At 1 the glide is geometric - equal
     * ratios per unit time - which is how pitch and formant movement are
     * actually perceived. */
    float formant_glide;

    /* How much a formant's bandwidth follows its frequency, 0..1.
     *
     * The phoneme table gives one bandwidth per formant per phoneme class, so
     * every vowel currently gets a 60 Hz first formant whether F1 sits at 270
     * for /i/ or 730 for /a/. Real bandwidths track frequency - measured B1 is
     * nearer 45 Hz for /i/ and 90 Hz for /a/ - because the losses that set them
     * scale with it. At 1 the bandwidth is scaled by (F / F_reference)^0.7,
     * which puts those two at 39 and 78.
     *
     * This also matters for voices: throat and mouth move formants without
     * touching the table, so a large speaker used to get a small speaker's
     * bandwidths. */
    float bandwidth_track;

    /* How much prosodic variation is removed, 0..1.
     *
     * This one runs the other way from its neighbours - 0 is still the setting
     * that changes nothing, but here 1 is *less* natural rather than more. It
     * exists because "monotone" turned out not to mean what the parameters said
     * it meant.
     *
     * `f0_range` is documented as "0 = monotone robot", and for a voice using
     * the phrase-level planner it is. For a voice with `prosody` at 0 it was
     * read by nothing at all: the older contour applied its declination and its
     * stressed-syllable bump regardless, so BENCmouth Monotone swung 28.5% in
     * pitch - 4.3 semitones, the same spread as BENCmouth Retro - and stretched
     * stressed vowels to 1.61x the length of unstressed ones.
     *
     * At 1 there is no declination, no pitch accent, and every vowel takes its
     * nominal length whatever stress it carries. The pitch is one number for
     * the whole utterance and the emphasis is the same everywhere.
     *
     * An absolute `[note]` is exempt: a sung note is that note, and flattening
     * it would make song mode and this parameter mutually exclusive for no
     * reason. */
    float flatten;
} bm_voice;

/* Fills in a sane default voice. Start here, then perturb. */
void bm_voice_default(bm_voice *voice);

/* ------------------------------------------------------------------ *
 * Voice presets
 *
 * Named voices compiled into the library. "retro" is BENCmouth Retro, the
 * original voice, and it is pinned: tests assert its output does not drift as
 * the synthesizer gains capability.
 * ------------------------------------------------------------------ */

/* Looks up a preset by name, case-insensitive. Returns NULL if unknown. The
 * returned pointer is to static storage and outlives any caller. */
const bm_voice *bm_voice_preset(const char *name);

/* Enumeration, for CLI help and tests. */
int             bm_voice_preset_count(void);

/* Number of dictionary entries compiled in, or 0 in a build without one.
 *
 * Public because a caller cannot otherwise tell, and the difference is
 * audible: without the dictionary every word goes through the letter-to-sound
 * rules, which get "robot" as R AA B AA T. A front end that shows which build
 * it is saves someone diagnosing a pronunciation that is working as designed. */
int             bm_dict_count(void);
const bm_voice *bm_voice_preset_at(int index);

/* The effects chain a preset is meant to be heard through, as a name to pass to
 * bm_effects_preset, or NULL if it wants none - which is most of them.
 *
 * Some voices are not a voice without their chain. Sentry is a plain neutral
 * tract and nothing else; everything that makes it a sentry is the ring
 * modulator. Keeping the pairing here rather than inside bm_voice is what lets
 * the chain still be selected on its own and tried on somebody else, which is
 * most of what an effects menu is for.
 *
 * A front end that ignores this gets the voice dry. That is a legitimate thing
 * to want, and it is why this is a separate call rather than something
 * bm_voice_preset does behind your back. */
const char     *bm_voice_chain(const bm_voice *voice);

/* Applies one `key = value` setting to a voice, for voice files and CLI
 * overrides. `key_len` may be 0 for a NUL-terminated key. Returns BM_ERR_ARG
 * for an unknown key so that a typo in a voice file is reported rather than
 * silently ignored. */
bm_result bm_voice_set_param(bm_voice *voice, const char *key, size_t key_len,
                             float value);

/* Fills `voice` with a randomly generated but plausible voice, determined
 * entirely by `seed` so the same seed always gives the same voice.
 *
 * Exists because the parameter space is large and mostly uninteresting, and
 * the good corners of it are found by accident far more often than by
 * reasoning. `bm -R 12345 -w found.bmvoice` captures one worth keeping.
 *
 * `name` points at static storage; rename it yourself if you keep it. */
void bm_voice_random(bm_voice *voice, uint32_t seed);

/* The frequency multiplier this voice applies to formant `index` (0-based).
 * Blends `throat` and `mouth` according to which cavity dominates that
 * formant. */
float bm_voice_formant_scale(const bm_voice *voice, int index);

/* ------------------------------------------------------------------ *
 * Effects
 *
 * A stage after the synthesizer, not part of it. This is a separate struct
 * rather than more fields on bm_voice, and the separation is the design:
 *
 *   A voice is a claim about a speaker - the length of their throat, how their
 *   vocal folds close, how hard they push. An effect is something done to the
 *   sound afterwards. Ring modulation is not a property of anyone's larynx.
 *
 * Keeping them apart also makes them compose. Any effect works on any voice, so
 * a metallic ring on the deep voice and the same ring on the child are one
 * dropdown apart instead of being two more entries in a preset table that would
 * otherwise have to hold every combination.
 *
 * The chain runs ring -> comb -> chorus -> drive -> vocoder -> crush, and that
 * order is deliberate: ring modulation on the clean voice keeps its sidebands
 * distinct, the comb adds the resonance, the chorus multiplies whatever has been
 * built so far into several detuned copies, and the drive then saturates the lot
 * - which is what makes a robot sound angry rather than merely mechanical. Crush
 * is last because it is the digital layer, applied to a finished sound.
 *
 * The vocoder sits between drive and crush because it is the last thing done to
 * the voice as a voice. It keeps only the band-by-band loudness of whatever
 * reaches it, so everything before it survives to the output as spectrum and
 * nothing else - a ring modulator ahead of it moves energy between bands and is
 * audible; a chorus ahead of it detunes copies of a signal that is about to be
 * discarded, and is very nearly not. Drive ahead of it is the useful one: the
 * harmonics it adds are what open the top bands.
 *
 * Chorus before drive rather than after, because a chorus after distortion
 * smears the harmonics the distortion just made and the result is mud; every
 * guitar rig in existence puts modulation ahead of the amp for the same
 * reason.
 *
 * Compile with -DBM_WITH_EFFECTS=0 to remove the stage entirely. That is worth
 * doing on a microcontroller: it drops the code *and* the comb delay line,
 * which is the only sizeable buffer in the whole library. The API stays, and
 * does nothing, so an embedded build gives up the feature and not the
 * interface.
 * ------------------------------------------------------------------ */

#ifndef BM_WITH_EFFECTS
#define BM_WITH_EFFECTS 1
#endif

/* Comb delay line length, in samples. Must be a power of two - the read index
 * wraps by masking. This sets the lowest comb frequency available: at 22050 Hz
 * a 2048-sample line reaches down to about 11 Hz, which is far below anything
 * useful, so the practical limit is the parameter range and not this. */
#ifndef BM_COMB_LEN
#define BM_COMB_LEN 2048
#endif

/* Chorus delay line, same rules. Shorter because a chorus wants tens of
 * milliseconds rather than the comb's hundreds: 1024 samples is 46 ms at
 * 22050 Hz and 21 ms at 48 kHz, and both comfortably clear the ~20 ms a
 * three-tap chorus needs. */
/* Echo delay line. The only buffer here sized by what it is *for* rather than
 * by what fits: an echo is heard as a repeat rather than as a resonance from
 * about 80 ms, and stops sounding like a room and starts sounding like a
 * musical device somewhere past 300. 8192 samples is 372 ms at 22050 Hz, which
 * covers that whole span, and it is 32 KB - by a wide margin the largest thing
 * the engine holds. Halve it and the longest echo halves with it; nothing else
 * changes. */
#ifndef BM_ECHO_LEN
#define BM_ECHO_LEN 8192
#endif

/* Reverb scratch: four feedback combs and two allpasses, laid end to end in one
 * block. Not a power of two and not masked - the lines are prime-ish lengths on
 * purpose, so their repeats do not coincide and reinforce into a pitch, and
 * rounding them up to powers of two would undo exactly that. */
#ifndef BM_REVERB_LEN
#define BM_REVERB_LEN 3072
#endif

#ifndef BM_CHORUS_LEN
#define BM_CHORUS_LEN 1024
#endif

/* Vocoder channels. Sixteen bands from 150 Hz to 6.3 kHz, which works out at a
 * third of an octave each - and third-octave spacing is chosen for a reason
 * about hearing rather than about filters: across the speech range a third of
 * an octave is close to a critical band, so one channel holds about as much
 * spectrum as the ear resolves in one place. Below that the channels are
 * reporting detail nobody can hear being lost; well above it the vowels start
 * blurring into each other.
 *
 * Sixteen is a choice within that, not a derivation. Hardware vocoders ran
 * anywhere from ten channels to a little over twenty; sixteen puts the span
 * that matters for speech at a third of an octave apiece and stops.
 *
 * No delay lines - this is filters and envelopes, 272 floats in all. Sizeable
 * in operations rather than in memory: 96 biquads per sample, which is the most
 * expensive thing in the library by some distance and still nothing on a
 * desktop. -DBM_WITH_EFFECTS=0 removes it with the rest of the stage. */
#ifndef BM_VOCODER_BANDS
#define BM_VOCODER_BANDS 16
#endif

typedef struct bm_effects {
    const char *name;

    /* Ring modulation: the signal multiplied by a sine carrier, which replaces
     * every component with a pair of sidebands either side of the carrier. The
     * result is inharmonic, which is exactly why it does not sound like a
     * person - the ear has no fundamental to lock onto. */
    float ring;          /* 0..1 wet mix */
    float ring_hz;       /* carrier frequency */

    /* How far the carrier wanders, 0..1, as a fraction of ring_hz either side
     * of it. 0 holds it exactly where ring_hz says, which is what it always
     * did.
     *
     * This exists because a ring modulator on a voice with no flutter, no
     * vibrato and no intonation produces a signal in which literally nothing
     * changes - carrier and fundamental both fixed, so the whole thing is
     * strictly periodic - and a stimulus that never changes is the one the ear
     * stops attending to. The effect measures exactly as strong at the end of
     * an utterance as at the start; it does not sound it. Moving the carrier
     * keeps the sideband pattern arriving somewhere new, which is the same
     * reason a chorus has an LFO and a real voice has flutter. */
    float ring_drift;

    /* A resonant comb: the signal added to a delayed copy of itself with
     * feedback, giving a series of evenly spaced peaks. This is what "speaking
     * through a metal tube" is, physically and here. */
    float comb;          /* 0..1 wet mix; also sets the feedback */
    float comb_hz;       /* spacing between resonances */

    /* Three copies of the signal, each read from a delay line whose length is
     * being swept by an LFO, and each swept a third of a cycle out of step with
     * the others.
     *
     * A moving delay is a pitch shift - that is what the Doppler effect is - so
     * three of them moving differently really are three detuned voices, which
     * a fixed delay is not. The comb above sounds like a tube for exactly that
     * reason: it does not move, so nothing is detuned and the copies stay in
     * unison.
     *
     * This is what the detuned-chorus novelty voice needed. It had been noted
     * as a host-layer job - sum three engines - and doing it here means it
     * composes with everything else and lives in a file. */
    float chorus;        /* 0..1 wet mix; also sets the sweep depth */
    float chorus_hz;     /* LFO rate; 0 selects the default, about 0.5 */

    /* Waveshaping. A cubic soft clip with pre-gain, so quiet parts pass and
     * loud parts fold - which generates harmonics that were not there and is
     * the single largest contributor to a voice sounding aggressive rather than
     * merely synthetic. Output level is compensated, so this changes the
     * timbre and not the loudness. */
    float drive;         /* 0..1 */

    /* A channel vocoder, in the original sense: the signal is split into bands,
     * the loudness of each band is measured, and those measurements are used to
     * shape a carrier generated here. What comes out has the *articulation* of
     * the voice and the *pitch* of the carrier, because everything about the
     * input except its band-by-band loudness is thrown away.
     *
     * That is the difference between this and everything else in the chain. The
     * other effects alter the voice; this one measures it and then builds
     * something new to those measurements. It is also why the controls are what
     * they are - there is nothing to set about the input, only about the thing
     * being driven.
     *
     * `vocoder_hz` is the carrier's pitch, and since it does not move, neither
     * does the output's: the input's intonation is one of the things discarded.
     * A vocoder is a monotone by construction, which is most of why it sounds
     * like a machine.
     *
     * The carrier turns itself into noise when the input stops being voiced,
     * decided from the same band measurements. Without that an /s/ arrives as a
     * buzz at the carrier pitch, because a pitched carrier has nothing
     * broadband to offer the bands where an /s/ lives - the oldest complaint
     * there is about vocoders, and the oldest fix. */
    float vocoder;       /* 0..1 wet mix */
    float vocoder_hz;    /* carrier pitch; 0 selects the default, about 110 */

    /* Sample-rate reduction: hold every Nth sample. The aliasing it produces
     * is the point - it is the sound of a converter that could not keep up,
     * and it is most of what "old digital" means to the ear. */
    float crush;         /* 0..1 */

    /* Echo: a single delayed copy, fed back on itself so it repeats.
     *
     * Distinct from `comb`, which is also a delay line and sounds nothing like
     * this. The difference is entirely the length. Below about 30 ms the ear
     * fuses the copy with the original and hears a resonance - that is what the
     * comb is - and above about 80 ms it separates them and hears a repeat.
     * There is no third mechanism here, only a delay either side of the point
     * where perception changes its mind.
     *
     * `echo` is the wet mix and also sets the feedback, so one knob takes it
     * from a single slap to a long tail. `echo_ms` is the spacing; 0 selects a
     * default around 180 ms. */
    float echo;          /* 0..1 */
    float echo_ms;       /* delay in milliseconds, up to what BM_ECHO_LEN holds */

    /* Reverb: four feedback combs into two allpasses, the Schroeder
     * arrangement, which is the oldest and still the most economical way to
     * turn one impulse into a diffuse tail.
     *
     * Not the same thing as a long echo, and the difference is countable. An
     * echo gives you one repeat per delay time; this gives four at once, at
     * lengths chosen not to share factors, and then smears each of those
     * through allpasses that disperse an impulse in time without colouring it.
     * The repeats multiply rather than add, so within a few hundred
     * milliseconds they stop being countable at all, which is what a room does.
     *
     * `reverb_size` moves the feedback, which is the decay time: small values
     * are a tiled room, large ones a hall. */
    float reverb;        /* 0..1 wet mix */
    float reverb_size;   /* 0..1; 0 selects a default mid-sized room */

    /* Output level, linear, applied after everything.
     *
     * This is not a duplicate of bm_voice.gain, and the difference is the whole
     * reason it exists. Voice gain is applied *before* the chain, which is
     * correct - `drive` is a threshold effect, and a drive stage that saw an
     * untrimmed signal would fold hard on a loud voice and do nothing on a
     * quiet one. But that same ordering makes the gain slider almost inert once
     * drive is up: turning it up just drives harder into a shaper that is
     * already saturating.
     *
     * So: input trim, chain, output level, which is the topology of every
     * overdrive pedal ever built, arrived at for the same reason.
     *
     * 0 means unity rather than silence, so a bm_effects with every field zero
     * is still an exact bypass. */
    float level;
} bm_effects;

/* All zero: a true bypass, sample for sample. */
void bm_effects_default(bm_effects *effects);

/* Named effect presets, looked up the same loose way voices are. */
const bm_effects *bm_effects_preset(const char *name);
int               bm_effects_preset_count(void);
const bm_effects *bm_effects_preset_at(int index);

/* One `key = value` setting, as bm_voice_set_param does for voices. Returns
 * BM_ERR_ARG for an unknown key. */
bm_result bm_effects_set_param(bm_effects *effects, const char *key,
                               size_t key_len, float value);

/* ------------------------------------------------------------------ *
 * Synthesis parameter frame
 *
 * One frame is the complete instantaneous state of the synthesizer, updated
 * at the frame rate (nominally 100 Hz) and interpolated between updates. The
 * phoneme layer's whole job is to produce a stream of these; the DSP layer's
 * whole job is to turn them into samples. Keeping that boundary sharp is what
 * lets you test either half without the other.
 *
 * Exposed publicly so callers can bypass the linguistic front end entirely
 * and drive the synthesizer directly - useful for singing, sound effects,
 * analysis-by-synthesis, and for debugging the front end by ear.
 * ------------------------------------------------------------------ */

typedef struct bm_frame {
    float f0;                    /* fundamental, Hz; 0 = unvoiced */

    /* Source amplitudes, dB (0 = silent, ~60 = full scale) */
    float av;                    /* voicing */
    float ah;                    /* aspiration (breath, /h/) */
    float af;                    /* frication */

    /* Source shape */
    float open_quotient;         /* 0..1 */
    float tilt;                  /* dB */

    /* Cascade branch: the vocal tract transfer function for voiced sound */
    float freq[BM_NFORMANTS];    /* formant centre frequencies, Hz */
    float bw[BM_NFORMANTS];      /* formant bandwidths, Hz */

    /* Nasal pole/zero pair. The zero is what actually makes a nasal sound
     * nasal; a pole alone just sounds muffled. */
    float nasal_pole_f, nasal_pole_bw;
    float nasal_zero_f, nasal_zero_bw;

    /* Parallel branch: per-formant amplitudes, dB, for frication. Fricatives
     * and stop bursts have antiresonances the cascade branch cannot produce,
     * so they get their own summed parallel path. */
    float par_amp[BM_NFORMANTS];
    float par_bypass;            /* dB, flat path around the resonators */
} bm_frame;

/* ------------------------------------------------------------------ *
 * Engine
 *
 * Opaque, but statically allocatable: declare a bm_engine_storage wherever
 * you like (stack, .bss, a pool) and hand it to bm_engine_init. The library
 * static-asserts internally that the real struct fits.
 * ------------------------------------------------------------------ */

typedef struct bm_engine bm_engine;

/* The union has to be at least as large as the real struct, which the library
 * static-asserts. Raised from 65536 when echo and reverb arrived: their delay
 * lines are 44 KB between them, and the engine went from 31 KB to 76 KB.
 *
 * 77,752 with the vocoder in it, which added 1,088 bytes of filter state and no
 * delay line at all.
 *
 * That is a desktop number and it is meant to be. The embedded path has barely
 * moved: -DBM_WITH_EFFECTS=0 removes every one of these buffers along with the
 * stage that reads them, and the engine measures 19,120 bytes - so a
 * microcontroller build sets BM_ENGINE_RESERVED to 24576 and is done. Tuning
 * BM_ECHO_LEN and BM_REVERB_LEN down is the middle option.
 *
 * The 16 bytes that build did gain are the two vocoder parameters, twice over.
 * bm_effects is the settings, not the machinery, and it is not compiled out -
 * a build without the stage still accepts and stores a chain, so a voice file
 * written on a desktop loads on a microcontroller and simply does less. */
#ifndef BM_ENGINE_RESERVED
#define BM_ENGINE_RESERVED 131072
#endif

typedef union bm_engine_storage {
    unsigned char bytes[BM_ENGINE_RESERVED];
    double  align_double;        /* force worst-case alignment */
    void   *align_pointer;
} bm_engine_storage;

/* Actual bytes used, for callers that want to size a pool exactly. */
size_t bm_engine_size(void);

typedef struct bm_config {
    uint32_t sample_rate;        /* Hz; 22050 is the sweet spot for 5 formants */
    uint32_t frame_rate;         /* parameter updates/sec; 100 is standard */
    bm_voice voice;

    /* Bypassed by default, and exactly bypassed - with every field zero the
     * output is sample-for-sample what it was before the stage existed. */
    bm_effects effects;

    /* Enables inline markup in bm_speak_text(). Off by default, and that
     * default matters: with markup off, "[pitch 80]" is ordinary text and gets
     * spoken as the words "pitch eighty". Turning it on changes brackets from
     * characters into commands, which would silently swallow text for any
     * caller who did not ask for it. See BM_TEXT_MARKUP. */
    int markup;

    /* Consult the dictionary when one is compiled in. On by default, because a
     * dictionary that is present and unused is a surprise; turning it off is
     * how you hear the rules on their own. See BM_TEXT_NO_DICT. */
    int use_dict;
} bm_config;

void bm_config_default(bm_config *config);

/* Initializes engine state in `storage` and returns a handle through `out`.
 * `config` is copied; it need not outlive the call. */
bm_result bm_engine_init(bm_engine_storage *storage,
                         const bm_config   *config,
                         bm_engine        **out);

/* Drops any queued speech and returns the DSP state to silence. */
void bm_engine_reset(bm_engine *engine);

/* Swaps the voice mid-stream. Takes effect at the next phoneme boundary,
 * so it will not click. */
bm_result bm_engine_set_voice(bm_engine *engine, const bm_voice *voice);

/* Swaps the effects chain mid-stream. Takes effect on the next sample; the
 * comb's delay line and the carrier phase are kept, so a change while speaking
 * is a change of setting and not a restart. */
bm_result bm_engine_set_effects(bm_engine *engine, const bm_effects *effects);

/* Turns the dictionary on or off on a live engine. Takes effect on the next
 * bm_speak_text(); an utterance already queued is unaffected. Harmless in a
 * build without a dictionary, where the rules are the only path either way. */
bm_result bm_engine_set_dictionary(bm_engine *engine, int enabled);

/* ------------------------------------------------------------------ *
 * Speaking
 * ------------------------------------------------------------------ */

/* Queues text. Runs normalization, dictionary lookup, letter-to-sound rules,
 * and prosody. `len` may be 0 for a NUL-terminated string. */
bm_result bm_speak_text(bm_engine *engine, const char *text, size_t len);

/* Queues a phoneme string directly, bypassing the front end. Accepts
 * whitespace-separated ARPABET with optional trailing stress digits, e.g.
 * "HH AH0 L OW1". Bracketed inline commands are not part of this layer. */
bm_result bm_speak_phonemes(bm_engine *engine, const char *phonemes, size_t len);

/* Renders up to `max_samples` mono samples. Returns the count actually
 * written, which is less than requested only when the utterance ends. Zero
 * means nothing is queued. Never blocks, never allocates - safe to call from
 * an audio callback. */
size_t bm_read(bm_engine *engine, bm_sample *out, size_t max_samples);

/* Nonzero while audio remains to be rendered. */
int bm_is_speaking(const bm_engine *engine);

/* ------------------------------------------------------------------ *
 * Lower layers, exposed for testing and for callers who want them
 * ------------------------------------------------------------------ */

/* Text to whitespace-separated ARPABET. Pure function, no engine required,
 * which makes the front end testable as a plain string transform - a large
 * practical win, since front-end bugs are much easier to read than hear. */
bm_result bm_text_to_phonemes(const char *text, size_t text_len,
                              char *out, size_t out_cap, size_t *out_len);

/* ------------------------------------------------------------------ *
 * Inline markup
 *
 * Bracketed commands embedded in text, off unless asked for:
 *
 *   [pitch 90]    base pitch in Hz for everything after it
 *   [speed 1.4]   rate multiplier
 *   [pause 400]   milliseconds of silence, inserted here
 *   [reset]       back to the voice's own settings
 *
 *   bm_speak_text(e, "normally. [pitch 70][speed 0.8] and now slowly.", 0);
 *
 * Commands survive into the phoneme string rather than being resolved away, so
 * `bm -t` shows them and bm_speak_phonemes() honours them too. That keeps the
 * phoneme string the single interface between the front end and the
 * synthesizer instead of adding a side channel around it.
 *
 * Compile with -DBM_WITH_MARKUP=0 to remove the parser entirely; the flag
 * below then does nothing and brackets stay ordinary characters. Nothing else
 * changes, so an embedded build gives up the feature and not the API.
 * ------------------------------------------------------------------ */

#define BM_TEXT_MARKUP 0x01u

/* Skip the dictionary and send every word through the letter-to-sound rules.
 *
 * A switch rather than a rebuild because the difference is the interesting
 * part: the rules are what a build for a microcontroller has, and hearing what
 * they do to a word - "robot" as R AA B AA T - is how you find the words that
 * need an exception. No effect in a build without a dictionary, where the
 * rules are the only path anyway. */
#define BM_TEXT_NO_DICT 0x02u

/* As bm_text_to_phonemes(), with behaviour flags. Passing 0 is identical to
 * the plain call, which is what that call does. */
bm_result bm_text_to_phonemes_ex(const char *text, size_t text_len,
                                 char *out, size_t out_cap, size_t *out_len,
                                 unsigned flags);

/* Drives the DSP directly from a caller-supplied frame, ignoring any queued
 * speech. Renders exactly one frame period worth of samples. */
size_t bm_render_frame(bm_engine *engine, const bm_frame *frame,
                       bm_sample *out, size_t max_samples);

/* Human-readable name for a result code. Static storage; do not free. */
const char *bm_strerror(bm_result result);

#ifdef __cplusplus
}
#endif

#endif /* BENCMOUTH_H */
