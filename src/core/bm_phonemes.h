/*
 * BENCmouth - phoneme inventory
 *
 * The full ARPABET set used by CMUdict, plus silence. Each entry is a set of
 * articulatory targets: where the formants want to go, which sources are
 * active, and how long the phoneme takes. The frame generator interpolates
 * between these; nothing here is heard directly.
 *
 * This table and bm_voice are between them the entire personality of the
 * synthesizer. Everything else is mechanism. Expect to spend far more time
 * tuning these numbers by ear than writing the code that reads them.
 *
 * Formant values start from published measurement data - Peterson & Barney
 * (1952) for vowels, standard place-of-articulation loci for consonants - and
 * are ours to adjust from there.
 */

#ifndef BM_PHONEMES_H
#define BM_PHONEMES_H

#include "bencmouth.h"

#include <stddef.h>

/* Number of entries in the inventory. A compile-time constant so that generated
 * data holding phoneme indices - the compiled dictionary - can be checked
 * against it; bm_phonemes.c asserts it matches the table, so the two cannot
 * drift apart silently. */
#define BM_PHONEME_COUNT 40

/* Only F1..F3 distinguish phonemes. F4 and F5 contribute presence rather than
 * identity and are held fixed across the inventory. */
#define BM_PH_NTARGETS 3
#define BM_F4_HZ 3500.0f
#define BM_F5_HZ 4500.0f
#define BM_F4_BW 200.0f
#define BM_F5_BW 250.0f

/* Nasal pole sits here whenever the nasal branch is active. */
#define BM_NASAL_POLE_HZ 270.0f
#define BM_NASAL_BW      100.0f

typedef enum bm_phoneme_class {
    BM_CLS_SILENCE = 0,
    BM_CLS_VOWEL,
    BM_CLS_DIPHTHONG,
    BM_CLS_NASAL,
    BM_CLS_LIQUID,
    BM_CLS_GLIDE,
    BM_CLS_FRICATIVE,
    BM_CLS_AFFRICATE,
    BM_CLS_STOP,
    BM_CLS_ASPIRATE
} bm_phoneme_class;

#define BM_PH_VOICED 0x01u

typedef struct bm_phoneme {
    const char    *name;          /* ARPABET, uppercase, no stress digit */
    unsigned char  cls;
    unsigned char  flags;

    /* Nominal timing at speed 1.0. The frame generator scales these by voice
     * speed and by stress. */
    unsigned short steady_ms;
    unsigned short transition_ms; /* time to glide into this target */
    unsigned short closure_ms;    /* stops and affricates: silent occlusion */
    unsigned short burst_ms;      /* stops and affricates: release */

    float freq[BM_PH_NTARGETS];
    float bw[BM_PH_NTARGETS];
    /* Diphthongs glide from freq to freq_end across the steady segment. For
     * every other class these are equal. */
    float freq_end[BM_PH_NTARGETS];

    float av, ah, af;             /* source amplitudes, dB */
    float par_amp[BM_NFORMANTS];  /* parallel branch, dB */
    float par_bypass;

    /* Nasal zero frequency, or 0 for no nasalization. The pole is always at
     * BM_NASAL_POLE_HZ; when the zero is off, the generator places it on top of
     * the pole so the pair cancels. */
    float nasal_zero_f;

    /* Burst spectrum for stops and affricates, used only during burst_ms. */
    float burst_amp[BM_NFORMANTS];
    float burst_bypass;
} bm_phoneme;

/* Lookup by ARPABET name. `len` may be 0 for a NUL-terminated string. Any
 * trailing stress digit is ignored, so "AH0" and "AH" both resolve. Returns
 * NULL if unknown. */
const bm_phoneme *bm_phoneme_lookup(const char *name, size_t len);

/* Silence, for pauses and utterance boundaries. Never NULL. */
const bm_phoneme *bm_phoneme_silence(void);

/* Iteration, for tests and tooling. */
int               bm_phoneme_count(void);
const bm_phoneme *bm_phoneme_at(int index);

#endif /* BM_PHONEMES_H */
