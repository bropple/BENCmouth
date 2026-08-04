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

#define BM_VERSION_MAJOR 0
#define BM_VERSION_MINOR 1
#define BM_VERSION_PATCH 0

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
 * reasoning. `bm -R 12345 -w found.voice` captures one worth keeping.
 *
 * `name` points at static storage; rename it yourself if you keep it. */
void bm_voice_random(bm_voice *voice, uint32_t seed);

/* The frequency multiplier this voice applies to formant `index` (0-based).
 * Blends `throat` and `mouth` according to which cavity dominates that
 * formant. */
float bm_voice_formant_scale(const bm_voice *voice, int index);

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

#ifndef BM_ENGINE_RESERVED
#define BM_ENGINE_RESERVED 65536
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
