/*
 * BENCmouth - phoneme inventory
 * See bm_phonemes.h for the contract.
 *
 * Field order for the initializer macros below:
 *   name, cls, flags, steady_ms, transition_ms, closure_ms, burst_ms,
 *   freq[3], bw[3], freq_end[3],
 *   av, ah, af, par_amp[5], par_bypass,
 *   nasal_zero_f,
 *   burst_amp[5], burst_bypass
 *
 * These are starting values, not final ones. They come from published
 * measurement data - Peterson & Barney for the vowels, conventional
 * place-of-articulation loci for the consonants - and are the raw material for
 * tuning by ear, which is the only way this gets genuinely good.
 */

#include "bm_phonemes.h"

#include <stddef.h>

/* Vowel bandwidths. Narrow F1 keeps vowels from sounding muffled; F3 wide
 * because it carries less identity and a sharp F3 rings unpleasantly. */
#define BW1 60.0f
#define BW2 90.0f
#define BW3 150.0f

/* Source levels, dB, where 60 is full scale at the source. Vowels sit at 48
 * because the cascade adds roughly 2.7x on top - see the note in
 * tools/vowel_demo.c about deliberately not normalizing that away. */
#define AV_VOWEL  48.0f
#define AV_SONOR  46.0f   /* nasals, liquids, glides: slightly backed off */
#define AV_VFRIC  38.0f   /* voiced fricatives: voicing under the noise */
#define AV_VSTOP  34.0f   /* voice bar during a voiced closure */

#define NO_AMPS  { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }

#define PH_VOWEL(nm, ms, f1, f2, f3)                                     \
    { nm, BM_CLS_VOWEL, BM_PH_VOICED, ms, 45u, 0u, 0u,                   \
      { f1, f2, f3 }, { BW1, BW2, BW3 }, { f1, f2, f3 },                 \
      AV_VOWEL, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f }

#define PH_DIPH(nm, ms, f1, f2, f3, e1, e2, e3)                          \
    { nm, BM_CLS_DIPHTHONG, BM_PH_VOICED, ms, 45u, 0u, 0u,               \
      { f1, f2, f3 }, { BW1, BW2, BW3 }, { e1, e2, e3 },                 \
      AV_VOWEL, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f }

#define PH_NASAL(nm, f1, f2, f3, zero)                                   \
    { nm, BM_CLS_NASAL, BM_PH_VOICED, 65u, 40u, 0u, 0u,                  \
      { f1, f2, f3 }, { 90.0f, 110.0f, 180.0f }, { f1, f2, f3 },         \
      AV_SONOR, 0.0f, 0.0f, NO_AMPS, 0.0f, zero, NO_AMPS, 0.0f }

#define PH_SONOR(nm, cls_, ms, f1, f2, f3)                               \
    { nm, cls_, BM_PH_VOICED, ms, 55u, 0u, 0u,                           \
      { f1, f2, f3 }, { 70.0f, 100.0f, 160.0f }, { f1, f2, f3 },         \
      AV_SONOR, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f }

#define PH_FRIC(nm, vcd, ms, f1, f2, f3, av_, af_, a1, a2, a3, a4, a5, byp) \
    { nm, BM_CLS_FRICATIVE, vcd, ms, 40u, 0u, 0u,                        \
      { f1, f2, f3 }, { 150.0f, 200.0f, 250.0f }, { f1, f2, f3 },        \
      av_, 0.0f, af_, { a1, a2, a3, a4, a5 }, byp, 0.0f, NO_AMPS, 0.0f }

#define PH_STOP(nm, vcd, clo, brst, f1, f2, f3, av_, b1, b2, b3, b4, b5, bbyp) \
    { nm, BM_CLS_STOP, vcd, 12u, 35u, clo, brst,                         \
      { f1, f2, f3 }, { 120.0f, 150.0f, 200.0f }, { f1, f2, f3 },        \
      av_, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, { b1, b2, b3, b4, b5 }, bbyp }

#define PH_AFFR(nm, vcd, clo, brst, f1, f2, f3, av_, b1, b2, b3, b4, b5, bbyp) \
    { nm, BM_CLS_AFFRICATE, vcd, 12u, 35u, clo, brst,                    \
      { f1, f2, f3 }, { 150.0f, 200.0f, 250.0f }, { f1, f2, f3 },        \
      av_, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, { b1, b2, b3, b4, b5 }, bbyp }

static const bm_phoneme BM_PHONEMES[] = {

    /* ---- silence and phrase boundaries ---------------------------
     *
     * SIL is a plain pause with no prosodic meaning. The three punctuation
     * entries are pauses that also tell the prosody planner what kind of
     * boundary it is looking at - a question has to be distinguishable from a
     * statement, and collapsing both to SIL threw that away before the planner
     * ever saw it.
     *
     * They are named for the punctuation that produced them, which makes
     * `bm -t` legible: "HH AH0 L OW1 . AA1 R Y UW1 DH EH1 R ?". Real phonemes
     * are all letters, so there is no collision. */
    { "SIL", BM_CLS_SILENCE, 0u, 80u, 30u, 0u, 0u,
      { 500.0f, 1500.0f, 2500.0f }, { BW1, BW2, BW3 },
      { 500.0f, 1500.0f, 2500.0f },
      0.0f, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f },

    { ",", BM_CLS_SILENCE, 0u, 110u, 30u, 0u, 0u,
      { 500.0f, 1500.0f, 2500.0f }, { BW1, BW2, BW3 },
      { 500.0f, 1500.0f, 2500.0f },
      0.0f, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f },

    { ".", BM_CLS_SILENCE, 0u, 240u, 30u, 0u, 0u,
      { 500.0f, 1500.0f, 2500.0f }, { BW1, BW2, BW3 },
      { 500.0f, 1500.0f, 2500.0f },
      0.0f, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f },

    { "?", BM_CLS_SILENCE, 0u, 240u, 30u, 0u, 0u,
      { 500.0f, 1500.0f, 2500.0f }, { BW1, BW2, BW3 },
      { 500.0f, 1500.0f, 2500.0f },
      0.0f, 0.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f },

    /* ---- monophthongs -------------------------------------------- */
    PH_VOWEL("AA", 130u, 730.0f, 1090.0f, 2440.0f),  /* odd, father  */
    PH_VOWEL("AE", 135u, 660.0f, 1720.0f, 2410.0f),  /* at           */
    PH_VOWEL("AH", 100u, 640.0f, 1190.0f, 2390.0f),  /* hut, schwa   */
    PH_VOWEL("AO", 130u, 570.0f,  840.0f, 2410.0f),  /* ought        */
    PH_VOWEL("EH", 110u, 530.0f, 1840.0f, 2480.0f),  /* Ed           */
    PH_VOWEL("ER", 130u, 490.0f, 1350.0f, 1690.0f),  /* hurt         */
    PH_VOWEL("IH",  95u, 390.0f, 1990.0f, 2550.0f),  /* it           */
    PH_VOWEL("IY", 120u, 270.0f, 2290.0f, 3010.0f),  /* eat          */
    PH_VOWEL("UH",  95u, 440.0f, 1020.0f, 2240.0f),  /* hood         */
    PH_VOWEL("UW", 120u, 300.0f,  870.0f, 2240.0f),  /* two          */

    /* ---- diphthongs ---------------------------------------------- */
    PH_DIPH("AW", 170u, 730.0f, 1090.0f, 2440.0f, 300.0f,  870.0f, 2240.0f),
    PH_DIPH("AY", 170u, 730.0f, 1090.0f, 2440.0f, 270.0f, 2290.0f, 3010.0f),
    PH_DIPH("EY", 160u, 530.0f, 1840.0f, 2480.0f, 270.0f, 2290.0f, 3010.0f),
    PH_DIPH("OW", 160u, 570.0f,  840.0f, 2410.0f, 300.0f,  870.0f, 2240.0f),
    PH_DIPH("OY", 180u, 570.0f,  840.0f, 2410.0f, 270.0f, 2290.0f, 3010.0f),

    /* ---- nasals --------------------------------------------------
     * The zero, not the pole, is what makes these sound nasal. It moves up
     * as the point of closure moves back through the mouth. */
    PH_NASAL("M",  250.0f, 1100.0f, 2150.0f, 1000.0f),
    PH_NASAL("N",  250.0f, 1700.0f, 2600.0f, 1700.0f),
    PH_NASAL("NG", 250.0f, 2000.0f, 2600.0f, 3000.0f),

    /* ---- liquids and glides --------------------------------------
     * The strikingly low F3 on R is the whole signature of American /r/;
     * get it wrong and the phoneme simply is not there. */
    PH_SONOR("L", BM_CLS_LIQUID, 70u, 350.0f, 1100.0f, 2600.0f),
    PH_SONOR("R", BM_CLS_LIQUID, 70u, 350.0f, 1100.0f, 1600.0f),
    PH_SONOR("W", BM_CLS_GLIDE,  65u, 300.0f,  610.0f, 2200.0f),
    PH_SONOR("Y", BM_CLS_GLIDE,  60u, 260.0f, 2070.0f, 3020.0f),

    /* ---- fricatives ----------------------------------------------
     * Voiceless ones have no voicing at all; the voiced pair adds a glottal
     * source under the noise. Sibilants get most of their energy from the
     * top of the parallel branch, the weak fricatives from the bypass path,
     * which is what makes /s/ cut through and /f/ nearly vanish. */
    /* The af values here were set by measurement, not by taste: tools/level_check
     * renders each phoneme in isolation and reports its level against a
     * reference vowel, and these are tuned so the inventory lands in the
     * relative intensities natural speech has. The sibilants in particular
     * started more than 20 dB too hot, which is inaudible as "wrong spectrum"
     * and very audible as a synthesizer that spits. */
    PH_FRIC("F",  0u,           110u, 400.0f, 1100.0f, 2200.0f,
            0.0f,     42.0f,  0.0f,  0.0f,  0.0f, 30.0f, 34.0f, 40.0f),
    PH_FRIC("V",  BM_PH_VOICED,  80u, 400.0f, 1100.0f, 2200.0f,
            AV_VFRIC, 36.0f,  0.0f,  0.0f,  0.0f, 26.0f, 30.0f, 34.0f),
    PH_FRIC("TH", 0u,           105u, 400.0f, 1400.0f, 2200.0f,
            0.0f,     42.0f,  0.0f,  0.0f,  0.0f, 28.0f, 32.0f, 38.0f),
    PH_FRIC("DH", BM_PH_VOICED,  75u, 400.0f, 1400.0f, 2200.0f,
            AV_VFRIC, 33.0f,  0.0f,  0.0f,  0.0f, 24.0f, 28.0f, 32.0f),
    PH_FRIC("S",  0u,           120u, 400.0f, 1400.0f, 2500.0f,
            0.0f,     31.0f,  0.0f,  0.0f, 20.0f, 48.0f, 58.0f,  0.0f),
    PH_FRIC("Z",  BM_PH_VOICED,  90u, 400.0f, 1400.0f, 2500.0f,
            AV_VFRIC, 34.0f,  0.0f,  0.0f, 18.0f, 42.0f, 50.0f,  0.0f),
    PH_FRIC("SH", 0u,           125u, 400.0f, 1800.0f, 2500.0f,
            0.0f,     34.0f,  0.0f,  0.0f, 50.0f, 56.0f, 44.0f,  0.0f),
    PH_FRIC("ZH", BM_PH_VOICED,  90u, 400.0f, 1800.0f, 2500.0f,
            AV_VFRIC, 36.0f,  0.0f,  0.0f, 44.0f, 48.0f, 38.0f,  0.0f),

    /* /h/ is aspiration only - the noise is shaped by whatever vowel follows,
     * which the frame generator arranges by gliding formants toward it. */
    { "HH", BM_CLS_ASPIRATE, 0u, 70u, 30u, 0u, 0u,
      { 500.0f, 1500.0f, 2500.0f }, { 200.0f, 250.0f, 300.0f },
      { 500.0f, 1500.0f, 2500.0f },
      0.0f, 48.0f, 0.0f, NO_AMPS, 0.0f, 0.0f, NO_AMPS, 0.0f },

    /* ---- stops ---------------------------------------------------
     * Closure is silence (or a faint voice bar when voiced), then a brief
     * burst whose spectrum encodes place of articulation. The formant targets
     * are the loci that neighbouring vowels bend toward - most of the cue to
     * which stop it was lives in those transitions, not in the burst. */
    /* The bilabials are shaped, not flat. An earlier pass pushed almost all of
     * their burst energy through the bypass path - unfiltered broadband noise
     * around the resonators - which does not sound like a plosive release, it
     * sounds like hiss. Their energy belongs low, near the F1/F2 loci, because
     * a bilabial burst is a diffuse *low* frequency event; the high bands and
     * the flat path both stay modest. Compare the bypass figures across the six
     * stops before changing these: if one is far out of line with the others,
     * that is the bug. */
    PH_STOP("P", 0u,           70u, 12u, 250.0f,  800.0f, 2000.0f,
            0.0f,     58.0f, 60.0f, 50.0f, 38.0f, 30.0f, 28.0f),
    PH_STOP("B", BM_PH_VOICED, 55u, 10u, 250.0f,  800.0f, 2000.0f,
            AV_VSTOP, 53.0f, 55.0f, 44.0f, 32.0f, 25.0f, 26.0f),
    PH_STOP("T", 0u,           70u, 14u, 250.0f, 1750.0f, 2600.0f,
            0.0f,      0.0f,  0.0f, 34.0f, 50.0f, 52.0f, 20.0f),
    PH_STOP("D", BM_PH_VOICED, 55u, 11u, 250.0f, 1750.0f, 2600.0f,
            AV_VSTOP,  0.0f,  0.0f, 33.0f, 47.0f, 49.0f, 21.0f),
    PH_STOP("K", 0u,           70u, 18u, 250.0f, 1900.0f, 2400.0f,
            0.0f,      0.0f, 30.0f, 52.0f, 48.0f, 34.0f, 16.0f),
    PH_STOP("G", BM_PH_VOICED, 55u, 14u, 250.0f, 1900.0f, 2400.0f,
            AV_VSTOP,  0.0f, 33.0f, 53.0f, 49.0f, 37.0f, 21.0f),

    /* ---- affricates ----------------------------------------------
     * A stop closure followed by a fricative release, so the burst is much
     * longer than a plain stop's and carries a postalveolar spectrum. */
    PH_AFFR("CH", 0u,           65u, 75u, 400.0f, 1800.0f, 2500.0f,
            0.0f,      0.0f,  0.0f, 48.0f, 54.0f, 42.0f,  0.0f),
    PH_AFFR("JH", BM_PH_VOICED, 50u, 60u, 400.0f, 1800.0f, 2500.0f,
            AV_VSTOP,  0.0f,  0.0f, 45.0f, 49.0f, 39.0f,  0.0f)
};

#define BM_PHONEME_TABLE_SIZE ((int)(sizeof BM_PHONEMES / sizeof BM_PHONEMES[0]))

/* Keeps the header's constant honest. If a phoneme is added or removed, this
 * fails the build and points straight at BM_PHONEME_COUNT - which anything
 * holding stored phoneme indices, notably the compiled dictionary, depends on. */
typedef char bm_phoneme_count_matches_table[
    (BM_PHONEME_TABLE_SIZE == BM_PHONEME_COUNT) ? 1 : -1];

/* ------------------------------------------------------------------ */

static int name_matches(const char *table_name, const char *query, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        char c = query[i];
        /* CMUdict marks stress with a trailing digit; the phoneme identity is
         * the letters alone. */
        if (c >= '0' && c <= '9') break;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (table_name[i] == '\0' || table_name[i] != c) return 0;
    }
    return table_name[i] == '\0';
}

const bm_phoneme *bm_phoneme_lookup(const char *name, size_t len)
{
    int i;

    if (name == 0) return 0;

    if (len == 0) {
        while (name[len] != '\0') len++;
    }
    if (len == 0) return 0;

    for (i = 0; i < BM_PHONEME_TABLE_SIZE; i++) {
        if (name_matches(BM_PHONEMES[i].name, name, len)) return &BM_PHONEMES[i];
    }
    return 0;
}

const bm_phoneme *bm_phoneme_silence(void)
{
    return &BM_PHONEMES[0];
}

bm_boundary bm_phoneme_boundary(const bm_phoneme *p)
{
    if (p == 0 || p->cls != BM_CLS_SILENCE) return BM_BOUND_NONE;
    switch (p->name[0]) {
    case ',': return BM_BOUND_COMMA;
    case '.': return BM_BOUND_PERIOD;
    case '?': return BM_BOUND_QUESTION;
    default:  return BM_BOUND_NONE;
    }
}

int bm_phoneme_count(void)
{
    return BM_PHONEME_TABLE_SIZE;
}

const bm_phoneme *bm_phoneme_at(int index)
{
    if (index < 0 || index >= BM_PHONEME_TABLE_SIZE) return 0;
    return &BM_PHONEMES[index];
}

/* Public, and declared in bencmouth.h rather than here: an editor needs to find
 * the vowel in a syllable, and the alternative is a second list of which
 * phonemes are vowels living outside this file. */
int bm_phoneme_is_vowel(const char *token, size_t len)
{
    const bm_phoneme *p;

    if (token == 0) return 0;
    if (len == 0) {
        while (token[len] != '\0') len++;
    }
    /* bm_phoneme_lookup already ignores a trailing stress digit, which is what
     * makes "IY1" and "IY" the same question. */
    p = bm_phoneme_lookup(token, len);
    return (p != 0 && (p->cls == BM_CLS_VOWEL || p->cls == BM_CLS_DIPHTHONG));
}
