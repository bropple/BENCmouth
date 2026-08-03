/*
 * BENCmouth - noise source
 * See bm_noise.h for the contract.
 */

#include "bm_noise.h"
#include "bm_math.h"

/* Real turbulent noise is not spectrally flat - it rolls off gently at the top.
 * A single pole here approximates that, and also takes the hardest edge off the
 * PRNG, which is white right up to Nyquist and sounds like hiss without it. */
#define BM_NOISE_LP_HZ 5000.0f

/* xorshift is absorbing at zero, so a zero seed would silence the generator. */
#define BM_NOISE_DEFAULT_SEED 0x2545F491u

void bm_noise_init(bm_noise *n, float sample_rate, uint32_t seed)
{
    float fc;

    if (n == 0) return;

    n->state = (seed != 0u) ? seed : BM_NOISE_DEFAULT_SEED;

    if (sample_rate <= 0.0f) sample_rate = 1.0f;
    fc = BM_NOISE_LP_HZ;
    if (fc > sample_rate * 0.45f) fc = sample_rate * 0.45f;
    n->lp_coeff = 1.0f - bm_expf(-BM_TWO_PI * fc / sample_rate);
    n->lp_z = 0.0f;
}

void bm_noise_reset(bm_noise *n)
{
    if (n == 0) return;
    n->state = BM_NOISE_DEFAULT_SEED;
    n->lp_z = 0.0f;
}

float bm_noise_tick(bm_noise *n)
{
    uint32_t x;
    float    y;

    if (n == 0) return 0.0f;

    /* xorshift32. Cheap, no multiply, and its period is far longer than any
     * utterance. */
    x = n->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    n->state = x;

    /* Top 16 bits mapped to [-1, 1). The low bits of xorshift are the weakest,
     * so take from the top. */
    y = (float)(x >> 16) * (1.0f / 32768.0f) - 1.0f;

    n->lp_z += n->lp_coeff * (y - n->lp_z);
    return n->lp_z;
}
