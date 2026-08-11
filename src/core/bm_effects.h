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

/* Two-pole sections per vocoder channel, in each of the two banks.
 *
 * Three, and the third one earns its keep. A channel's skirts are the steepest
 * spectrum it can report: where the voice falls off faster than the filter
 * does, a channel stops measuring its own band and starts measuring how much of
 * its loud neighbours has leaked in, and the vocoder reproduces a flatter, and
 * therefore brighter, spectrum than it was given. Measured against the voice it
 * came from, over a sentence:
 *
 *   sections   skirts        error slope   scatter   output crest
 *   2 (4-pole) 12 dB/oct     +1.9 dB/oct   +-6 dB      16.8 dB
 *   3 (6-pole) 18 dB/oct     +0.7 dB/oct   +-3 dB      13.2 dB
 *   4 (8-pole) 24 dB/oct     +0.7 dB/oct   +-4 dB      11.8 dB
 *
 * The fourth section buys nothing on the slope, which is where the argument
 * for it would have to come from, and costs a third more arithmetic. Three also
 * happens to land the output's crest factor on the voice's own 13.2 dB, which
 * is what lets the vocoder be levelled against the dry signal without its peaks
 * arriving somewhere different. */
#define BM_VOC_SECTIONS 3

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

    /* Vocoder. Three coefficients per band, shared by the analysis and the
     * synthesis filter - both banks are the same bank, which is what makes the
     * measurement and the thing built from it line up. */
    float    voc_b0[BM_VOCODER_BANDS];
    float    voc_a1[BM_VOCODER_BANDS];
    float    voc_a2[BM_VOCODER_BANDS];
    float    voc_g[BM_VOCODER_BANDS];    /* what each band contributes */

    /* Three biquad sections per band per path, two state words each. */
    float    voc_ana[BM_VOCODER_BANDS][BM_VOC_SECTIONS * 2];
    float    voc_syn[BM_VOCODER_BANDS][BM_VOC_SECTIONS * 2];
    float    voc_env[BM_VOCODER_BANDS];

    int      voc_bands;      /* how many centres the sample rate can carry */
    int      voc_hi_from;    /* first band counted as "high" for the carrier */
    float    voc_att;        /* envelope follower, rising */
    float    voc_rel;        /* and falling */
    float    voc_phase;      /* carrier, 0..1 */
    float    voc_inc;        /* its advance per sample */
    float    voc_saw_z;      /* previous saw, for the differentiator */
    float    voc_pulse_g;    /* brings the pulse train to unit RMS */
    float    voc_out;        /* output gain, once the rate is known */
    uint32_t voc_rng;        /* the carrier's noise, when it is noise */
    float    voc_wet;
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
