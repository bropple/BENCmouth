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

/* Four combs in parallel, then two allpasses in series. Schroeder's original
 * counts, and they have survived sixty years of people trying fewer: three
 * combs leave audible gaps in the tail, and one allpass does not diffuse an
 * impulse enough to stop it sounding like a slap. */
#define BM_REVERB_COMBS 4
#define BM_REVERB_APS   2
#define BM_REVERB_LINES (BM_REVERB_COMBS + BM_REVERB_APS)

/* The longest tail the engine will render past the end of an utterance. Two
 * seconds is generous for anything here and is a backstop rather than a target:
 * a feedback close enough to 1 will ask for minutes, and rendering minutes of
 * decaying silence because a slider went to the top is not a service. */
#define BM_EFFECTS_TAIL_MAX_MS 2000u

typedef struct bm_effects_state {
    bm_effects p;
    float sample_rate;

    /* Nonzero when any effect is doing something. The tick returns its input
     * untouched otherwise, which is what makes "all fields zero" a bypass in
     * the exact sense rather than the approximate one. */
    int   active;

    float ring_phase;
    float ring_lfo;         /* carrier drift LFO, 0..1 */
    float ring_lfo_inc;     /* its advance per sample */
    float ring_drift;       /* fraction of ring_hz to wander either side */

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

    float    echo_buf[BM_ECHO_LEN];
    unsigned echo_at;
    unsigned echo_delay;
    float    echo_fb;
    float    echo_wet;
    float    echo_trim;

    /* One block, sliced into six lines. Slicing rather than six arrays because
     * the lengths are not powers of two and are tuned relative to each other -
     * keeping them adjacent makes it obvious that the total is the budget, and
     * changing one means changing what follows it. */
    float    verb_buf[BM_REVERB_LEN];
    unsigned verb_at[BM_REVERB_LINES];
    unsigned verb_off[BM_REVERB_LINES];   /* where each line starts in verb_buf */
    unsigned verb_len[BM_REVERB_LINES];
    float    verb_damp_z[BM_REVERB_COMBS];/* the lowpass in each comb's loop */
    float    verb_fb;
    float    verb_wet;
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

/* How long the chain goes on making sound after its input stops, in
 * milliseconds. 0 when nothing rings.
 *
 * The engine renders this much silence past the last frame. It used to render a
 * flat 100 ms, which is right for the resonators - they are done in a few tens
 * of milliseconds - and wrong by an order of magnitude for anything with
 * feedback in it: a 330 ms echo was cut off inside its first repeat, and the
 * rendered file was exactly as long with the effect as without it.
 *
 * A function of the parameters rather than the running state, so the engine can
 * ask before it starts rather than having to watch the output decay. */
unsigned bm_effects_tail_ms(const bm_effects *e);

#endif /* BM_EFFECTS_H */
