/*
 * BENCmouth - two-pole resonator and two-zero antiresonator
 * See bm_resonator.h for the contract.
 */

#include "bm_resonator.h"
#include "bm_math.h"

/* Poles this close to the unit circle already ring for a long time; going
 * closer buys nothing audible and costs numerical headroom. */
#define BM_MIN_BANDWIDTH_HZ 1.0f

/* Leave a margin below Nyquist. A pole exactly at Nyquist is stable but the
 * response there is degenerate, and interpolation overshoot can push past it. */
#define BM_MAX_FREQ_FRACTION 0.49f

/* Ceiling on the antiresonator's DC-normalization gain, and the divisor below
 * which we stop dividing at all. See bm_antiresonator_set for why this exists. */
#define BM_MAX_MAKEUP_GAIN    1000.0f
#define BM_MIN_MAKEUP_DIVISOR 0.001f

/* Computes the shared pole-pair coefficients. Both the resonator and the
 * antiresonator start here; the antiresonator then inverts them. */
static void bm_pole_pair(float freq, float bw, float sample_rate,
                         float *out_a, float *out_b, float *out_c)
{
    float nyquist_limit, theta, r, rcos, rsin, one_minus;

    if (sample_rate <= 0.0f) sample_rate = 1.0f;

    nyquist_limit = sample_rate * BM_MAX_FREQ_FRACTION;
    freq = bm_clampf(freq, 0.0f, nyquist_limit);
    if (bw < BM_MIN_BANDWIDTH_HZ) bw = BM_MIN_BANDWIDTH_HZ;

    /* Pole radius: r = e^(-pi*bw/fs). Bandwidth maps to distance from the unit
     * circle, so a wider formant decays faster. */
    theta = BM_TWO_PI * freq / sample_rate;
    r     = bm_expf(-BM_PI * bw / sample_rate);
    rcos  = r * bm_cosf(theta);
    rsin  = r * bm_sinf(theta);

    *out_b = 2.0f * rcos;
    *out_c = -(r * r);

    /* a is chosen so the DC gain a/(1-b-c) is exactly 1, which is what lets
     * five resonators chain without a makeup gain stage.
     *
     * Algebraically a = 1 - b - c = 1 - 2r*cos(t) + r^2, but evaluating it that
     * way catastrophically cancels when the pole sits near DC: for a 1 Hz
     * resonance at 22 kHz the true value is ~2e-8 while the subtraction yields
     * 1.2e-7, a 6x error, and at 0 Hz it collapses to exactly zero. The
     * equivalent form below is the squared distance from the pole to z=1 and
     * never subtracts two nearly-equal quantities, so it stays accurate at both
     * ends of the band. */
    one_minus = 1.0f - rcos;
    *out_a    = one_minus * one_minus + rsin * rsin;
}

void bm_resonator_set(bm_resonator *r, float freq, float bw, float sample_rate)
{
    if (r == 0) return;
    bm_pole_pair(freq, bw, sample_rate, &r->a, &r->b, &r->c);
}

void bm_antiresonator_set(bm_resonator *r, float freq, float bw, float sample_rate)
{
    float a, b, c, inv_a;

    if (r == 0) return;

    bm_pole_pair(freq, bw, sample_rate, &a, &b, &c);

    /* Invert the resonator to place zeros where its poles were, then rescale so
     * DC gain stays 1: a' + b' + c' == (1 - b - c)/a == 1.
     *
     * The makeup gain 1/a is unbounded as the zero approaches DC, because a
     * zero at DC and unity gain at DC are contradictory requests. Unbounded is
     * not a stability problem - this branch is all-zero and therefore always
     * stable - but a narrow zero placed near DC yields coefficients in the
     * millions, and an interpolation transient that swings the nasal zero
     * downward would produce a colossal click rather than a nasal.
     *
     * So the makeup gain saturates. Real nasal zeros live between roughly 300
     * and 1500 Hz, where 1/a is of order 1 and the cap never engages; it exists
     * purely so that a degenerate request degrades into a loud filter instead
     * of an explosive one. */
    if (a < BM_MIN_MAKEUP_DIVISOR) {
        inv_a = BM_MAX_MAKEUP_GAIN;
    } else {
        inv_a = 1.0f / a;
        if (inv_a > BM_MAX_MAKEUP_GAIN) inv_a = BM_MAX_MAKEUP_GAIN;
    }
    r->a =  inv_a;
    r->b = -b * inv_a;
    r->c = -c * inv_a;
}

void bm_resonator_reset(bm_resonator *r)
{
    if (r == 0) return;
    r->z1 = 0.0f;
    r->z2 = 0.0f;
}

float bm_resonator_tick(bm_resonator *r, float x)
{
    /* History holds past *outputs*. */
    float y = r->a * x + r->b * r->z1 + r->c * r->z2;
    r->z2 = r->z1;
    r->z1 = y;
    return y;
}

float bm_antiresonator_tick(bm_resonator *r, float x)
{
    /* History holds past *inputs* - this is an all-zero filter. */
    float y = r->a * x + r->b * r->z1 + r->c * r->z2;
    r->z2 = r->z1;
    r->z1 = x;
    return y;
}
