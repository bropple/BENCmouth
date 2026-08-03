/*
 * BENCmouth - phrase-level prosody
 * See bm_prosody.h for what this replaces and why.
 */

#include "bm_prosody.h"
#include "bm_math.h"

#include <stddef.h>

/* All of these are fractions of the voice's f0_range, so a voice with a wide
 * range gets a correspondingly dramatic contour and a narrow one stays flat.
 * They are ratios rather than absolute semitones for exactly that reason: one
 * knob should scale the whole personality of the intonation. */
#define DECLINATION   0.50f   /* pitch lost from phrase start to phrase end   */
#define ACCENT        0.85f   /* lift on a primary-stressed syllable          */
#define ACCENT_SECOND 0.40f   /* on a secondary-stressed one                  */
#define FINAL_FALL    1.30f   /* extra drop into a full stop                  */
#define FINAL_RISE    1.70f   /* rise into a question mark                    */
#define FINAL_CONTINUE 0.35f  /* small lift into a comma: more to come        */

/* Fraction of a phrase over which the final contour plays out. Roughly the
 * last syllable or two, which is where English puts its boundary tones. */
#define FINAL_SPAN 0.34f

/* Phrase-final lengthening on the last vowel before a boundary. A large,
 * well-attested effect - listeners use it to hear phrase ends even when the
 * pitch cue is absent. */
#define FINAL_LENGTHEN 1.35f

static int is_vowel(const bm_phoneme *p)
{
    return p != 0 && (p->cls == BM_CLS_VOWEL || p->cls == BM_CLS_DIPHTHONG);
}

static float semitones(float base, float st)
{
    /* 2^(st/12) - equal temperament, which is how pitch intervals are heard. */
    return base * bm_exp2f(st / 12.0f);
}

void bm_prosody_plan(const bm_phoneme *const *seq,
                     const unsigned char *stress,
                     int count,
                     const bm_voice *voice,
                     float *f0_out,
                     float *dur_out)
{
    float base, range, strength;
    int   start = 0, i;

    if (seq == 0 || voice == 0 || f0_out == 0 || dur_out == 0 || count <= 0) return;

    base = (voice->f0_base > 20.0f) ? voice->f0_base : 120.0f;
    range = (voice->f0_range > 0.0f) ? voice->f0_range : 0.0f;
    strength = bm_clampf(voice->prosody, 0.0f, 1.0f);
    range *= strength;

    for (i = 0; i < count; i++) {
        f0_out[i] = base;
        dur_out[i] = 1.0f;
    }

    while (start < count) {
        bm_boundary btype = BM_BOUND_PERIOD;
        int end = start, last_vowel = -1, span, k;

        /* A phrase runs up to the next boundary phoneme, or the end of input -
         * which is treated as a full stop, because an utterance that simply
         * stops should still sound finished. */
        while (end < count && bm_phoneme_boundary(seq[end]) == BM_BOUND_NONE) end++;
        if (end < count) btype = bm_phoneme_boundary(seq[end]);

        span = end - start;
        if (span <= 0) { start = end + 1; continue; }

        for (k = start; k < end; k++) {
            float t = (span > 1) ? (float)(k - start) / (float)(span - 1) : 0.0f;
            float st = -DECLINATION * range * t;

            if (is_vowel(seq[k])) {
                if (stress != 0 && stress[k] == 1u) st += ACCENT * range;
                else if (stress != 0 && stress[k] == 2u) st += ACCENT_SECOND * range;
                last_vowel = k;
            }

            if (t > 1.0f - FINAL_SPAN) {
                float u = (t - (1.0f - FINAL_SPAN)) / FINAL_SPAN;
                switch (btype) {
                case BM_BOUND_QUESTION: st += FINAL_RISE * range * u;     break;
                case BM_BOUND_COMMA:    st += FINAL_CONTINUE * range * u; break;
                default:                st -= FINAL_FALL * range * u;     break;
                }
            }

            f0_out[k] = semitones(base, st);
        }

        /* Lengthen the last vowel of the phrase, and hold the boundary
         * silence at the pitch the phrase ended on so the contour does not
         * jump back to base across the pause. */
        if (last_vowel >= 0 && strength > 0.0f) {
            dur_out[last_vowel] = 1.0f + (FINAL_LENGTHEN - 1.0f) * strength;
        }
        if (end < count) {
            f0_out[end] = f0_out[end > start ? end - 1 : start];
        }

        start = end + 1;
    }
}
