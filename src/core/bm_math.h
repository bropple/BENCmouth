/*
 * BENCmouth - internal math
 *
 * Replaces the handful of libm functions the core needs, so that core/ links
 * freestanding. Not general-purpose: these are accurate enough for filter
 * coefficient computation and nothing is guaranteed outside the documented
 * argument ranges.
 *
 * Speed is deliberately not the priority. Coefficients are recomputed at the
 * frame rate (~100 Hz, ~12 per frame), so these run on the order of a thousand
 * times a second - accuracy and code size matter, throughput does not.
 */

#ifndef BM_MATH_H
#define BM_MATH_H

#define BM_PI      3.14159265358979323846f
#define BM_TWO_PI  6.28318530717958647693f

/* 2^x. Full float range. Measured relative error below 7e-6 over [-30, 30].
 * Saturates to 0 below -126 and to a large finite value above 127. */
float bm_exp2f(float x);

/* e^x, via bm_exp2f. Same accuracy. */
float bm_expf(float x);

/* cos(x), x in radians. Measured absolute error below 1e-6 over [-20, 20],
 * which is at the binary32 noise floor for arguments of that size. Precision
 * degrades further out as range reduction loses low bits; all internal callers
 * pass arguments below 2*pi, where the polynomial alone is float-exact. */
float bm_cosf(float x);

/* sin(x), x in radians. Same caveats as bm_cosf. */
float bm_sinf(float x);

/* Decibels to a linear amplitude multiplier: 10^(db/20).
 * bm_db_to_linear(0) == 1.0, bm_db_to_linear(-inf) underflows to 0. */
float bm_db_to_linear(float db);

/* sqrt(x). Returns 0 for x <= 0. Accurate to within a few ulp. */
float bm_sqrtf(float x);

/* log2(x) for x > 0. Returns a large negative value for x <= 0 rather than
 * trapping, because the callers are interpolating frequencies and a guard
 * branch at every sample is worse than a defined nonsense answer at none.
 * Measured absolute error below 2e-6 over [1e-6, 1e6]. */
float bm_log2f(float x);

/* |x|, without pulling in libm. */
float bm_fabsf(float x);

/* Clamps x to [lo, hi]. Returns lo if the interval is inverted. */
float bm_clampf(float x, float lo, float hi);

#endif /* BM_MATH_H */
