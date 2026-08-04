/*
 * BENCmouth - voiced excitation source
 * See bm_glottis.h for the contract.
 */

#include "bm_glottis.h"
#include "bm_math.h"

/* Open quotient outside this range stops behaving like a glottal pulse. */
#define BM_MIN_OQ 0.05f
#define BM_MAX_OQ 0.95f

/* Spectral tilt is specified as attenuation at this frequency. */
#define BM_TILT_REFERENCE_HZ 3000.0f

/* Flutter is a sum of three slow oscillators at mutually incommensurate rates,
 * so the pattern never audibly repeats. Rates are our own choice; what matters
 * is only that no ratio between them is a simple fraction. */
static const float BM_FLUTTER_RATE[3] = { 3.1f, 6.9f, 11.3f };

/* Peak flutter deviation in Hz at f0 = 100 and flutter = 1. Scales with f0,
 * because a 3 Hz wobble is subtle at 200 Hz and obvious at 80 Hz. */
#define BM_FLUTTER_DEPTH_HZ 4.0f

/* Rate used when a voice asks for vibrato without naming one. Measured singer
 * vibrato clusters between 5 and 7 Hz almost regardless of voice type, and the
 * ear is remarkably intolerant of rates outside it - slower reads as a wobble,
 * faster as a tremble. */
#define BM_VIBRATO_DEFAULT_HZ 5.5f

/* ------------------------------------------------------------------ *
 * Alternate excitations
 *
 * Ratio and amplitude per partial. The organ set is drawbar-ish: the
 * fundamental, its octave, the twelfth, and so on, which is a pipe rank
 * stacked the way an organ stop is. The bell set is the measured partial
 * series of a church bell, and the numbers are why it sounds like one - a
 * minor third at 1.19 above a prime at 1 is the interval that makes a bell
 * sound sad, and it is not a harmonic of anything.
 * ------------------------------------------------------------------ */

static const float BM_ORGAN[BM_NPARTIALS][2] = {
    { 1.00f, 1.00f },   /* fundamental        */
    { 2.00f, 0.55f },   /* octave             */
    { 3.00f, 0.35f },   /* twelfth            */
    { 4.00f, 0.22f },   /* two octaves        */
    { 6.00f, 0.12f },   /* nineteenth         */
    { 8.00f, 0.08f }    /* three octaves      */
};

static const float BM_BELL[BM_NPARTIALS][2] = {
    { 0.50f, 0.40f },   /* hum      - an octave below the strike note */
    { 1.00f, 1.00f },   /* prime                                      */
    { 1.19f, 0.55f },   /* tierce   - the minor third, and the reason */
    { 1.50f, 0.45f },   /* quint                                      */
    { 2.00f, 0.60f },   /* nominal  - what you think the pitch is     */
    { 2.66f, 0.25f }    /* superquint                                 */
};

/* Chosen by measurement, not by summing the amplitude columns.
 *
 * Six sines summed are not as loud as their coefficients suggest - they are
 * partly out of phase with each other - and the glottal pulse they have to
 * match is a sparse spike train whose RMS is nothing like its peak. Measured
 * over a sustained 150 Hz tone: folds 0.258 RMS, and these divisors put the
 * pipe and the bell within a few tenths of a dB of it. Moving between sources
 * should change the timbre and not the volume. */
#define BM_ORGAN_NORM (1.0f / 3.34f)
#define BM_BELL_NORM  (1.0f / 3.95f)

void bm_glottis_set_source(bm_glottis *g, float source)
{
    if (g == 0) return;
    g->source = bm_clampf(source, 0.0f, 2.0f);
}

/* One sample of the additive stack, and the partial phases advanced.
 *
 * Both tables are walked every time rather than only the one in use, because
 * `source` crossfades: at 1.5 the output is half pipe and half bell, and both
 * sets of phases have to keep running or the one that is fading in would
 * arrive mid-cycle. */
static void partials_tick(bm_glottis *g, float f0, float *organ, float *bell)
{
    int k;

    *organ = 0.0f;
    *bell  = 0.0f;

    for (k = 0; k < BM_NPARTIALS; k++) {
        float ph = g->partial_phase[k];

        /* One phase per slot, shared by the two tables. They differ in ratio,
         * so the slot advances at whichever ratio the mix is actually using -
         * interpolated, so moving the control does not step the phase. */
        float bmix  = (g->source > 1.0f) ? (g->source - 1.0f) : 0.0f;
        float ratio = BM_ORGAN[k][0] * (1.0f - bmix) + BM_BELL[k][0] * bmix;
        float s     = bm_sinf(BM_TWO_PI * ph);

        *organ += s * BM_ORGAN[k][1];
        *bell  += s * BM_BELL[k][1];

        ph += ratio * f0 / g->sample_rate;
        while (ph >= 1.0f) ph -= 1.0f;
        g->partial_phase[k] = ph;
    }

    *organ *= BM_ORGAN_NORM;
    *bell  *= BM_BELL_NORM;
}

void bm_glottis_init(bm_glottis *g, float sample_rate)
{
    if (g == 0) return;
    g->sample_rate = (sample_rate > 0.0f) ? sample_rate : 1.0f;
    g->open_quotient = 0.5f;
    g->tilt_coeff = 0.0f;
    g->flutter = 0.0f;
    g->f0 = 0.0f;
    g->phase_inc = 0.0f;
    g->vibrato = 0.0f;
    g->vibrato_rate = BM_VIBRATO_DEFAULT_HZ;
    g->source = 0.0f;
    bm_glottis_reset(g);
}

void bm_glottis_reset(bm_glottis *g)
{
    if (g == 0) return;
    /* Start at 1.0 rather than 0.0 so the very first tick wraps and fires a
     * closure immediately; starting mid-pulse would emit a partial period. */
    g->phase = 1.0f;
    g->tilt_z = 0.0f;
    g->flutter_phase[0] = 0.0f;
    g->flutter_phase[1] = 0.0f;
    g->flutter_phase[2] = 0.0f;
    /* Phase only. Depth and rate are the speaker's, not the utterance's, and
     * survive a reset the same way open quotient does. */
    g->vibrato_phase = 0.0f;
    {
        int k;
        /* Spread rather than all at zero: six sines starting in phase sum to a
         * single large spike on the first sample, which is an audible tick at
         * the start of every utterance. */
        for (k = 0; k < BM_NPARTIALS; k++) {
            g->partial_phase[k] = (float)k / (float)BM_NPARTIALS;
        }
    }
}

void bm_glottis_set_vibrato(bm_glottis *g, float semitones, float rate_hz)
{
    if (g == 0) return;
    /* Two octaves is already well past anything musical; the bound exists so a
     * typo in a voice file cannot drive the pitch to the Nyquist limit. */
    g->vibrato = bm_clampf(semitones, 0.0f, 24.0f);
    g->vibrato_rate = (rate_hz > 0.0f) ? bm_clampf(rate_hz, 0.1f, 40.0f)
                                       : BM_VIBRATO_DEFAULT_HZ;
}

void bm_glottis_set(bm_glottis *g, float f0, float open_quotient,
                    float tilt_db, float flutter)
{
    float fc;

    if (g == 0) return;

    g->f0 = (f0 > 0.0f) ? f0 : 0.0f;
    g->open_quotient = bm_clampf(open_quotient, BM_MIN_OQ, BM_MAX_OQ);
    g->flutter = bm_clampf(flutter, 0.0f, 1.0f);

    /* Tilt: a one-pole lowpass whose cutoff is placed so that its attenuation
     * at 3 kHz equals the requested dB. From |H(f)|^2 = 1/(1+(f/fc)^2),
     * fc = f_ref / sqrt(10^(tilt/10) - 1). */
    if (tilt_db <= 0.0f) {
        g->tilt_coeff = 0.0f;               /* bypass */
    } else {
        /* bm_db_to_linear(2*tilt) is 10^(tilt/10), so this is 10^(tilt/10)-1. */
        float ratio = bm_db_to_linear(tilt_db * 2.0f) - 1.0f;
        if (ratio < 1e-6f) {
            g->tilt_coeff = 0.0f;
        } else {
            fc = BM_TILT_REFERENCE_HZ / bm_sqrtf(ratio);
            if (fc > g->sample_rate * 0.45f) fc = g->sample_rate * 0.45f;
            g->tilt_coeff = 1.0f - bm_expf(-BM_TWO_PI * fc / g->sample_rate);
        }
    }
}

float bm_glottis_tick(bm_glottis *g)
{
    float f0, tau, y, flut;
    int   k;

    if (g == 0) return 0.0f;

    if (g->f0 <= 0.0f) {
        g->phase = 1.0f;
        return 0.0f;
    }

    /* Flutter: slow quasi-random pitch drift. Without it a sustained vowel
     * sounds synthetic in a way that no amount of formant accuracy fixes. */
    flut = 0.0f;
    if (g->flutter > 0.0f) {
        for (k = 0; k < 3; k++) {
            flut += bm_sinf(BM_TWO_PI * g->flutter_phase[k]);
            g->flutter_phase[k] += BM_FLUTTER_RATE[k] / g->sample_rate;
            if (g->flutter_phase[k] >= 1.0f) g->flutter_phase[k] -= 1.0f;
        }
        flut *= g->flutter * BM_FLUTTER_DEPTH_HZ * (g->f0 / 100.0f) / 3.0f;
    }

    f0 = g->f0 + flut;

    /* Applied as a ratio, after flutter. Semitones are a ratio by definition,
     * and adding hertz instead would make the same setting sound like a tremor
     * on a bass voice and like nothing at all on a soprano. */
    if (g->vibrato > 0.0f) {
        float v = bm_sinf(BM_TWO_PI * g->vibrato_phase);
        g->vibrato_phase += g->vibrato_rate / g->sample_rate;
        if (g->vibrato_phase >= 1.0f) g->vibrato_phase -= 1.0f;
        f0 *= bm_exp2f(g->vibrato * v * (1.0f / 12.0f));
    }

    if (f0 < 1.0f) f0 = 1.0f;
    g->phase_inc = f0 / g->sample_rate;

    g->phase += g->phase_inc;
    if (g->phase >= 1.0f) g->phase -= 1.0f;

    /* Flow derivative across the open phase, zero while closed.
     *
     * flow    u(tau)  = tau^2 - tau^3   (zero at both ends of the open phase)
     * deriv   u'(tau) = 2*tau - 3*tau^2
     *
     * u'(0) = 0, peaks at +1/3, and reaches exactly -1 at closure. The step
     * from -1 back to zero is the closure discontinuity - it is the dominant
     * source of high-frequency energy in voiced speech, and it does alias
     * somewhat. Spectral tilt is the intended remedy. */
    if (g->phase < g->open_quotient) {
        tau = g->phase / g->open_quotient;
        y   = 2.0f * tau - 3.0f * tau * tau;
    } else {
        y = 0.0f;
    }

    /* Crossfade into the alternate excitations. Skipped entirely at 0, which
     * is what keeps a voice that never asked for this paying nothing for it -
     * and what keeps BENCmouth Retro bit-for-bit where it was. */
    if (g->source > 0.0f) {
        float organ, bell;

        partials_tick(g, f0, &organ, &bell);
        if (g->source <= 1.0f) {
            y = y + (organ - y) * g->source;
        } else {
            y = organ + (bell - organ) * (g->source - 1.0f);
        }
    }

    /* Tilt last, so it shapes whatever the source turned out to be rather than
     * only the glottal path. */
    if (g->tilt_coeff > 0.0f) {
        g->tilt_z += g->tilt_coeff * (y - g->tilt_z);
        y = g->tilt_z;
    }

    return y;
}
