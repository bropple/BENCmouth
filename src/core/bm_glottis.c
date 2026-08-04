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

    if (g->tilt_coeff > 0.0f) {
        g->tilt_z += g->tilt_coeff * (y - g->tilt_z);
        y = g->tilt_z;
    }

    return y;
}
