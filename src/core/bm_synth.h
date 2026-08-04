/*
 * BENCmouth - cascade/parallel formant synthesizer
 *
 * The DSP top level. Takes a bm_frame of parameters and produces samples.
 * Knows nothing about phonemes, text, or time - it is a pure function of the
 * current frame plus filter state, which is what makes it testable by hand.
 *
 *   voicing --+                                                      cascade
 *             +--> [nasal zero] -> [nasal pole] -> [F1..F5 in series] ------+
 *   aspiration+                                                            |
 *                                                                          +--> out
 *                     +--> [F1] --+                                        |
 *   frication --------+--> [F2] --+ alternating signs, summed ---> parallel+
 *                     +--> ...  --+
 *                     +--> bypass-+
 *
 * Voiced sound goes through the cascade, which is what the vocal tract
 * physically is - resonances in series - and gets correct relative formant
 * amplitudes for free. Frication takes the parallel branch because fricatives
 * and stop bursts have antiresonances a cascade cannot produce, and because
 * each noise band needs its own amplitude. Successive parallel formants are
 * summed with alternating sign; in phase they would fill in the spectral
 * valleys between formants and the result sounds muffled.
 *
 * There is no explicit lip-radiation stage. The glottal source emits the flow
 * derivative, which already carries the +6 dB/octave radiation tilt - see
 * bm_glottis.h.
 */

#ifndef BM_SYNTH_H
#define BM_SYNTH_H

#include "bencmouth.h"
#include "bm_effects.h"
#include "bm_glottis.h"
#include "bm_noise.h"
#include "bm_fixed.h"
#include "bm_resonator.h"

typedef struct bm_synth {
    float sample_rate;

    bm_glottis   glottis;
    bm_noise     noise;

    bm_resonator cascade[BM_NFORMANTS];
    bm_resonator nasal_pole;
    bm_resonator nasal_zero;
    bm_resonator parallel[BM_NFORMANTS];

    /* Linear amplitudes, resolved from the frame's dB values once per frame
     * rather than once per sample. These are targets.
     *
     * Held in the sample loop's own representation so that the whole cascade
     * runs without converting: a fixed-point build that converted at every
     * stage would quantize twenty-odd times per sample and end up noisier than
     * the float path it was meant to replace. */
#if BM_FIXED_POINT
    bm_q amp_voicing;
    bm_q amp_aspiration;
    bm_q amp_frication;
    bm_q amp_parallel[BM_NFORMANTS];
    bm_q amp_bypass;
#else
    float amp_voicing;
    float amp_aspiration;
    float amp_frication;
    float amp_parallel[BM_NFORMANTS];
    float amp_bypass;
#endif

    /* Smoothed amplitudes, chasing the targets at sample rate.
     *
     * Parameters only arrive at the frame rate, so applying them directly
     * makes every amplitude change a step discontinuity 10 ms wide - and a
     * step in gain is a click. It is loudest on stop bursts, which go from
     * silence to full scale between one sample and the next, but every hard
     * consonant gets a transient spike from it. Chasing the target with a
     * short time constant removes the step while leaving bursts fast enough to
     * still read as plosive. */
#if BM_FIXED_POINT
    bm_q cur_voicing;
    bm_q cur_aspiration;
    bm_q cur_frication;
    bm_q cur_parallel[BM_NFORMANTS];
    bm_q cur_bypass;
    bm_q amp_smooth;
#else
    float cur_voicing;
    float cur_aspiration;
    float cur_frication;
    float cur_parallel[BM_NFORMANTS];
    float cur_bypass;
    float amp_smooth;
#endif

    /* Persistent source behaviour, set from the voice rather than per frame -
     * these are properties of a speaker, not of an instant. */
    float flutter;
    float gain;

    /* Applied after `gain`, so the chain always sees a level the voice has
     * already trimmed. It matters for `drive`, which is a threshold effect: if
     * the drive stage saw the untrimmed signal, the same setting would fold
     * hard on a loud voice and do nothing on a quiet one. */
    bm_effects_state effects;
} bm_synth;

void bm_synth_init(bm_synth *s, float sample_rate);

/* Clears all filter and source state. Coefficients survive. */
void bm_synth_reset(bm_synth *s);

/* Recomputes coefficients from `frame`. Call at the frame rate; the cost is
 * about a dozen transcendentals, which at 100 Hz is nothing. */
void bm_synth_set_frame(bm_synth *s, const bm_frame *frame);

/* Sets pitch flutter, 0..1. Persists across frames. */
void bm_synth_set_flutter(bm_synth *s, float flutter);

/* Sets periodic pitch modulation: peak deviation in semitones, and a rate in
 * Hz where 0 selects the default. Persists across frames. */
void bm_synth_set_vibrato(bm_synth *s, float semitones, float rate_hz);

/* Sets the effects chain. Persists across frames and across resets. */
void bm_synth_set_effects(bm_synth *s, const bm_effects *effects);

/* Sets the output level multiplier. 1.0 is nominal; see bm_voice.gain for why
 * this is per voice rather than a constant. Persists across frames. */
void bm_synth_set_gain(bm_synth *s, float gain);

/* One output sample. */
float bm_synth_tick(bm_synth *s);

/* Converts a frame amplitude in dB to a linear multiplier, where 60 dB is
 * full scale and anything at or below 0 dB is exactly silent. Exposed because
 * the frame-building layers need the same mapping. */
float bm_amp_from_db(float db);

#endif /* BM_SYNTH_H */
