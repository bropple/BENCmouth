/*
 * BENCmouth - noise source for aspiration and frication
 *
 * One generator serves both. Aspiration and frication are the same physical
 * phenomenon - turbulence at a constriction - differing only in where the
 * constriction is and how the result is filtered, so they share a source and
 * are separated by amplitude and by which branch they feed.
 *
 * Deterministic by construction: an xorshift PRNG, not rand(). The core does
 * not link libc, and reproducible output makes rendering testable.
 */

#ifndef BM_NOISE_H
#define BM_NOISE_H

#include <stdint.h>

typedef struct bm_noise {
    uint32_t state;
    float    lp_coeff;
    float    lp_z;
} bm_noise;

/* `seed` may be any value; zero is replaced with a fixed nonzero constant
 * because xorshift is absorbing at zero. */
void bm_noise_init(bm_noise *n, float sample_rate, uint32_t seed);
void bm_noise_reset(bm_noise *n);

/* One sample, roughly uniform over [-1, 1] before filtering. */
float bm_noise_tick(bm_noise *n);

#endif /* BM_NOISE_H */
