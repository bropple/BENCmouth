/*
 * BENCmouth - post-synthesis effects
 *
 * See bencmouth.h for what the parameters mean and why they are their own
 * struct rather than more fields on bm_voice. This header is the running
 * state: the carrier phase, the comb's delay line, and the coefficients
 * resolved from the parameters once rather than per sample.
 *
 * The whole stage compiles away under -DBM_WITH_EFFECTS=0, including
 * comb_buf - which is the only buffer in the library big enough to matter on a
 * microcontroller, at BM_COMB_LEN floats.
 */

#ifndef BM_EFFECTS_H
#define BM_EFFECTS_H

#include "bencmouth.h"

typedef struct bm_effects_state {
    bm_effects p;
    float sample_rate;

    /* Nonzero when any effect is doing something. The tick returns its input
     * untouched otherwise, which is what makes "all fields zero" a bypass in
     * the exact sense rather than the approximate one. */
    int   active;

    float ring_phase;

#if BM_WITH_EFFECTS
    float    comb_buf[BM_COMB_LEN];
    unsigned comb_at;
    unsigned comb_delay;
    float    comb_fb;
    float    comb_wet;      /* the mix */
    float    comb_norm;     /* 1 - feedback: brings the teeth back to unity */

    float    chorus_buf[BM_CHORUS_LEN];
    unsigned chorus_at;
    float    chorus_phase;  /* LFO, 0..1 */
    float    chorus_inc;    /* LFO advance per sample */
    float    chorus_wet;
    float    chorus_base;   /* centre delay, samples */
    float    chorus_depth;  /* sweep either side of it, samples */
#endif

    float ring_trim;
    float drive_gain;
    float drive_trim;

    float    out_level;

    unsigned crush_step;
    unsigned crush_count;
    float    crush_held;
} bm_effects_state;

void  bm_effects_state_init(bm_effects_state *s, float sample_rate);

/* Clears the delay line and the carrier phase. Parameters survive - they
 * belong to the patch, not to the utterance. */
void  bm_effects_state_reset(bm_effects_state *s);

void  bm_effects_state_set(bm_effects_state *s, const bm_effects *e);

/* One sample through the chain. Returns `x` unchanged when nothing is on. */
float bm_effects_tick(bm_effects_state *s, float x);

#endif /* BM_EFFECTS_H */
