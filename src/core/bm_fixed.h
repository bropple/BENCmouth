/*
 * BENCmouth - fixed-point arithmetic for the sample loop
 *
 * Enabled with -DBM_FIXED_POINT=1. Off by default: float is the reference
 * implementation, it is what every voice was tuned against, and on anything
 * with an FPU it is both faster and more accurate.
 *
 * The case for having it at all is the low end. A Cortex-M4F has an FPU and
 * does not need this; a Cortex-M0 or M3 does not, and soft-float there costs
 * roughly an order of magnitude. At 22050 Hz the sample loop runs about fifty
 * multiplies - twelve resonators, two sources, the mixing - so that difference
 * is between comfortable and impossible.
 *
 * Scope
 * -----
 * Only the per-sample path is converted. Coefficients are computed once per
 * frame, a hundred times a second, from transcendental functions that would be
 * miserable in fixed point and cost nothing in float. They are computed in
 * float and stored as Q16.
 *
 * Format
 * ------
 * Q18 in int32: eighteen fractional bits, giving a resolution of 3.8e-6 and a
 * range of +-8192. Both bounds matter, and the choice between them was measured
 * rather than guessed - rendering a sentence both ways and comparing:
 *
 *     Q16   40.3 dB SNR   headroom +-32768
 *     Q18   52.9 dB SNR   headroom +-8192     <- chosen
 *     Q20   65.6 dB SNR   headroom +-2048     saturates the stress test
 *     Q22   77.0 dB SNR   headroom +-512      saturates badly
 *
 * Q18 is the most precision available that still clears the worst excursion the
 * test suite produces, with room to spare. Q20 sounds better and starts
 * clipping signals that are legitimate.
 *
 *   - Resolution sets how precisely a pole can be placed. A 60 Hz bandwidth at
 *     22050 Hz puts the pole radius at 0.99146; quantizing its coefficient
 *     shifts the realised bandwidth by well under a hertz.
 *
 *   - Range has to cover the loudest thing the filters produce, not the loudest
 *     thing they are asked to output. tests/test_resonator.c drives a 1 Hz
 *     bandwidth resonator with full-scale noise and sees excursions near 3900,
 *     so a format that saturated at 1.0 - or at 2048 - would clip on signals
 *     that are perfectly legitimate inside the cascade.
 *
 * Products are accumulated in int64. That is one instruction on any ARM with
 * SMULL, which is everything from the M3 up.
 *
 * The helpers are `static inline` rather than plain `static` so that a
 * translation unit which uses only some of them does not warn about the rest -
 * and the strict-warnings build treats that as an error.
 */

#ifndef BM_FIXED_H
#define BM_FIXED_H

#include <stdint.h>

#ifndef BM_FIXED_POINT
#define BM_FIXED_POINT 0
#endif

#if BM_FIXED_POINT

typedef int32_t bm_q;

#define BM_Q_BITS 18
#define BM_Q_ONE  ((bm_q)1 << BM_Q_BITS)

/* Saturating bounds. Reaching these means something upstream is wrong, but
 * wrapping would turn a loud sound into a loud different sound, which is far
 * harder to recognise than clipping. */
#define BM_Q_MAX  ((bm_q)0x7FFFFFFF)
#define BM_Q_MIN  ((bm_q)(-0x7FFFFFFF - 1))

static inline bm_q bm_q_mul(bm_q a, bm_q b)
{
    /* Round to nearest rather than truncating. A plain shift rounds toward
     * negative infinity, which biases every product downward - and with twelve
     * multiplies in series through the cascade that bias accumulates into
     * something audible rather than cancelling. */
    int64_t p = (((int64_t)a * (int64_t)b) + ((int64_t)1 << (BM_Q_BITS - 1)))
                >> BM_Q_BITS;
    if (p > (int64_t)BM_Q_MAX) return BM_Q_MAX;
    if (p < (int64_t)BM_Q_MIN) return BM_Q_MIN;
    return (bm_q)p;
}

static inline bm_q bm_q_add(bm_q a, bm_q b)
{
    int64_t s = (int64_t)a + (int64_t)b;
    if (s > (int64_t)BM_Q_MAX) return BM_Q_MAX;
    if (s < (int64_t)BM_Q_MIN) return BM_Q_MIN;
    return (bm_q)s;
}

static inline bm_q bm_q_from_float(float x)
{
    float v = x * (float)BM_Q_ONE;
    if (v >=  2147483520.0f) return BM_Q_MAX;
    if (v <= -2147483520.0f) return BM_Q_MIN;
    return (bm_q)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

static inline float bm_q_to_float(bm_q x)
{
    return (float)x * (1.0f / (float)BM_Q_ONE);
}

#endif /* BM_FIXED_POINT */

#endif /* BM_FIXED_H */
