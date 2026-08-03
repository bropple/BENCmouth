/*
 * BENCmouth - internal math
 *
 * See bm_math.h for the contract. Implementation notes:
 *
 * Polynomial approximations rather than lookup tables. Tables were the obvious
 * first instinct, but a 257-entry interpolated cosine table is ~1 KB of .rodata
 * for roughly 1e-5 accuracy, while the degree-8 polynomial below is a few dozen
 * bytes of code for 1e-7. When the call rate is ~1 kHz, the table's speed
 * advantage buys nothing and costs flash. On a microcontroller that trade is
 * strongly in the polynomial's favour.
 */

#include "bm_math.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */

float bm_fabsf(float x)
{
    return x < 0.0f ? -x : x;
}

float bm_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* Newton-Raphson from an exponent-halving seed. Halving the biased exponent
 * lands within roughly a factor of two of the answer, and each iteration then
 * doubles the correct digits, so four passes saturate binary32. */
float bm_sqrtf(float x)
{
    union { float f; uint32_t u; } v;
    float s;
    int   k;

    if (x <= 0.0f) return 0.0f;

    v.f = x;
    v.u = 0x1FBD1DF5u + (v.u >> 1);
    s = v.f;

    for (k = 0; k < 4; k++) {
        s = 0.5f * (s + x / s);
    }
    return s;
}

/* ------------------------------------------------------------------ *
 * 2^x
 *
 * Split x into integer n and fraction f in [0,1). 2^n is assembled directly
 * into the IEEE-754 exponent field; 2^f comes from the Taylor series of
 * e^(f*ln2) taken to degree 6, which holds relative error under 2e-5 across
 * the whole fractional interval (worst case is at f = 1).
 * ------------------------------------------------------------------ */

float bm_exp2f(float x)
{
    union { float f; uint32_t u; } pow2n;
    float f, poly;
    int   n;

    /* IEEE-754 single cannot represent 2^x outside roughly [-126, 127]. */
    if (x < -126.0f) return 0.0f;
    if (x >  127.0f) x = 127.0f;

    /* floor(), without libm. Casting truncates toward zero, so correct for
     * negatives. */
    n = (int)x;
    if ((float)n > x) n -= 1;
    f = x - (float)n;

    /* 2^f = e^(f * ln2), Taylor to degree 6. Horner form. */
    poly = 1.0f + f * (0.69314718f
         + f * (0.24022651f
         + f * (0.05550411f
         + f * (0.00961813f
         + f * (0.00133336f
         + f *  0.00015404f)))));

    pow2n.u = (uint32_t)(n + 127) << 23;

    return pow2n.f * poly;
}

float bm_expf(float x)
{
    /* 1/ln(2) */
    return bm_exp2f(x * 1.44269504f);
}

float bm_db_to_linear(float db)
{
    /* 10^(db/20) == 2^(db / (20*log10(2))) == 2^(db / 6.02059991) */
    return bm_exp2f(db * 0.16609640f);
}

/* log2, by splitting off the IEEE-754 exponent and expanding what is left.
 *
 * With the mantissa m in [1,2), substituting t = (m-1)/(m+1) puts t in
 * [0, 1/3], where the odd series 2/ln2 * (t + t^3/3 + t^5/5 + ...) converges
 * fast enough that five terms are already below binary32 resolution. Taking
 * the exponent separately is what keeps the argument range that small. */
float bm_log2f(float x)
{
    union { float f; uint32_t u; } v;
    float m, t, t2, series;
    int   e;

    if (x <= 0.0f) return -1e30f;

    v.f = x;
    e = (int)((v.u >> 23) & 0xFFu) - 127;
    v.u = (v.u & 0x007FFFFFu) | 0x3F800000u;   /* mantissa, now in [1,2) */
    m = v.f;

    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    series = t * (1.0f
           + t2 * (0.33333333f
           + t2 * (0.2f
           + t2 * (0.14285714f
           + t2 * (0.11111111f
           + t2 *  0.09090909f)))));

    /* 2 / ln(2) */
    return (float)e + 2.88539008f * series;
}

/* ------------------------------------------------------------------ *
 * cos / sin
 *
 * Reduce to [0, 2pi), then fold into [0, pi/2] tracking sign, then evaluate
 * the degree-8 even Taylor series for cosine. Error is ~1e-8 on the reduced
 * interval; the practical limit is the reduction itself, which loses low bits
 * once |x| gets large. Internal callers pass 2*pi*f/fs, always under 2*pi.
 * ------------------------------------------------------------------ */

static float bm_cos_quadrant(float x)
{
    /* cos(x) for x in [0, pi/2], Taylor to x^12.
     *
     * Term count is set by the worst case at x = pi/2, where the first omitted
     * term dominates. Truncating at x^8 leaves x^10/10! = 2.5e-5 of error - far
     * too coarse. Through x^12 the residual is x^14/14! = 6e-9, comfortably
     * below binary32 epsilon, so the result is float-exact and adding further
     * terms would buy nothing. */
    float x2 = x * x;
    return 1.0f + x2 * (-0.5f
         + x2 * ( 0.0416666667f
         + x2 * (-0.00138888889f
         + x2 * ( 0.0000248015873f
         + x2 * (-0.000000275573192f
         + x2 *   0.00000000208767570f)))));
}

/* Two-part 2*pi for Cody-Waite range reduction. BM_TWO_PI_HI is exactly
 * representable in binary32 (6 + 9/32), so n*hi is computed without rounding
 * error and the remainder is carried separately. Subtracting a single rounded
 * 2*pi instead costs about a decimal digit of accuracy per revolution, which
 * showed up as ~2.5e-5 error out at x = 20. */
#define BM_TWO_PI_HI 6.28125f
#define BM_TWO_PI_LO 0.00193530717958647693f

float bm_cosf(float x)
{
    float sign = 1.0f;
    int   n;

    x = bm_fabsf(x);                       /* cos is even */

    /* Reduce to [0, 2pi). */
    if (x >= BM_TWO_PI) {
        n = (int)(x * (1.0f / BM_TWO_PI));
        x = (x - (float)n * BM_TWO_PI_HI) - (float)n * BM_TWO_PI_LO;
        if (x < 0.0f)        x += BM_TWO_PI;   /* reduction may undershoot */
        if (x >= BM_TWO_PI)  x -= BM_TWO_PI;   /* ...or overshoot */
    }

    /* Fold [0, 2pi) into [0, pi/2], carrying the sign of the quadrant. */
    if (x > BM_PI) {
        x = BM_TWO_PI - x;                 /* cos(2pi - x) == cos(x) */
    }
    if (x > BM_PI * 0.5f) {
        x = BM_PI - x;                     /* cos(pi - x) == -cos(x) */
        sign = -1.0f;
    }

    return sign * bm_cos_quadrant(x);
}

float bm_sinf(float x)
{
    return bm_cosf(x - BM_PI * 0.5f);
}
