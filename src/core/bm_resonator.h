/*
 * BENCmouth - two-pole resonator and two-zero antiresonator
 *
 * The primitive the whole synthesizer is built from. A formant is one
 * resonator; the cascade branch is five of them in series; the nasal zero and
 * the parallel branch need the antiresonator.
 *
 * Both are the difference equations from Klatt (1980), which are the standard
 * choice for formant synthesis because they are cheap, unconditionally stable
 * for bandwidths above zero, and normalized to unity gain at DC - so chaining
 * five of them does not require a makeup gain stage.
 *
 *   resonator:      y[n] = a*x[n] + b*y[n-1] + c*y[n-2]
 *   antiresonator:  y[n] = a*x[n] + b*x[n-1] + c*x[n-2]
 *
 * Same struct serves both; the two history slots hold past outputs for the
 * resonator and past inputs for the antiresonator. Do not switch a live
 * instance between the two without resetting it - the history means different
 * things and the transient will be audible.
 */

#ifndef BM_RESONATOR_H
#define BM_RESONATOR_H

#include "bm_fixed.h"

/* Coefficients are always computed in float - see bm_fixed.h - and stored in
 * whichever representation the sample loop uses. */
typedef struct bm_resonator {
#if BM_FIXED_POINT
    bm_q  a, b, c;
    bm_q  z1, z2;
#else
    float a, b, c;   /* difference equation coefficients */
    float z1, z2;    /* history: outputs (resonator) or inputs (antiresonator) */
#endif
} bm_resonator;

/* Sets resonator coefficients for a formant at `freq` Hz with 3 dB bandwidth
 * `bw` Hz. Both are clamped to a stable, meaningful range: freq to
 * [0, 0.49*sample_rate], bw to at least 1 Hz. Clamping rather than rejecting is
 * deliberate - parameter interpolation can transiently overshoot, and a click
 * is a better failure mode than a NaN that poisons every later sample. */
void bm_resonator_set(bm_resonator *r, float freq, float bw, float sample_rate);

/* Antiresonator (transmission zero pair) at `freq` Hz, width `bw` Hz. */
void bm_antiresonator_set(bm_resonator *r, float freq, float bw, float sample_rate);

/* Clears history without touching coefficients. */
void bm_resonator_reset(bm_resonator *r);

/* One sample through the two-pole resonator. */
float bm_resonator_tick(bm_resonator *r, float x);

/* One sample through the two-zero antiresonator. */
float bm_antiresonator_tick(bm_resonator *r, float x);

#if BM_FIXED_POINT
/* The integer path the synthesizer uses directly, so that a whole cascade runs
 * without converting to float and back at every stage. The float wrappers above
 * exist for tests and for callers outside the sample loop. */
bm_q bm_resonator_tick_q(bm_resonator *r, bm_q x);
bm_q bm_antiresonator_tick_q(bm_resonator *r, bm_q x);
#endif

#endif /* BM_RESONATOR_H */
