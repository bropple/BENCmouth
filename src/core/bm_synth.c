/*
 * BENCmouth - cascade/parallel formant synthesizer
 * See bm_synth.h for the topology.
 */

#include "bm_synth.h"
#include "bm_math.h"

/* Frame amplitudes are dB with 60 as full scale. Below this the contribution is
 * inaudible and we may as well skip the branch entirely. */
#define BM_AMP_FLOOR_DB 0.0f
#define BM_FULL_SCALE_DB 60.0f

float bm_amp_from_db(float db)
{
    if (db <= BM_AMP_FLOOR_DB) return 0.0f;
    return bm_db_to_linear(db - BM_FULL_SCALE_DB);
}

void bm_synth_init(bm_synth *s, float sample_rate)
{
    int i;

    if (s == 0) return;

    s->sample_rate = (sample_rate > 0.0f) ? sample_rate : 1.0f;

    bm_glottis_init(&s->glottis, s->sample_rate);
    bm_noise_init(&s->noise, s->sample_rate, 0u);

    /* Park every resonator somewhere harmless so a tick before the first
     * set_frame produces silence rather than uninitialized coefficients. */
    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_set(&s->cascade[i], 1000.0f, 100.0f, s->sample_rate);
        bm_resonator_set(&s->parallel[i], 1000.0f, 100.0f, s->sample_rate);
        s->amp_parallel[i] = 0.0f;
    }
    bm_resonator_set(&s->nasal_pole, 270.0f, 100.0f, s->sample_rate);
    bm_antiresonator_set(&s->nasal_zero, 270.0f, 100.0f, s->sample_rate);

    s->amp_voicing = 0.0f;
    s->amp_aspiration = 0.0f;
    s->amp_frication = 0.0f;
    s->amp_bypass = 0.0f;
    s->flutter = 0.0f;
    s->gain = 1.0f;

    /* One-pole coefficient for a ~0.8 ms time constant, so an amplitude change
     * settles in roughly 2.5 ms. Fast enough that a plosive burst still sounds
     * like a burst, slow enough that the frame-boundary step stops clicking. */
    s->amp_smooth = 1.0f - bm_expf(-1.0f / (0.0008f * s->sample_rate));
    if (s->amp_smooth > 1.0f) s->amp_smooth = 1.0f;

    bm_synth_reset(s);
}

void bm_synth_reset(bm_synth *s)
{
    int i;

    if (s == 0) return;

    bm_glottis_reset(&s->glottis);
    bm_noise_reset(&s->noise);

    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_reset(&s->cascade[i]);
        bm_resonator_reset(&s->parallel[i]);
        s->cur_parallel[i] = 0.0f;
    }
    bm_resonator_reset(&s->nasal_pole);
    bm_resonator_reset(&s->nasal_zero);

    /* Start silent rather than at the current targets, so an utterance fades
     * up from nothing instead of opening with the same step we just removed. */
    s->cur_voicing = 0.0f;
    s->cur_aspiration = 0.0f;
    s->cur_frication = 0.0f;
    s->cur_bypass = 0.0f;
}

void bm_synth_set_frame(bm_synth *s, const bm_frame *f)
{
    int i;

    if (s == 0 || f == 0) return;

    bm_glottis_set(&s->glottis, f->f0, f->open_quotient, f->tilt, s->flutter);

    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_set(&s->cascade[i],  f->freq[i], f->bw[i], s->sample_rate);
        bm_resonator_set(&s->parallel[i], f->freq[i], f->bw[i], s->sample_rate);
        s->amp_parallel[i] = bm_amp_from_db(f->par_amp[i]);
    }

    bm_resonator_set(&s->nasal_pole, f->nasal_pole_f, f->nasal_pole_bw,
                     s->sample_rate);
    bm_antiresonator_set(&s->nasal_zero, f->nasal_zero_f, f->nasal_zero_bw,
                         s->sample_rate);

    s->amp_voicing    = bm_amp_from_db(f->av);
    s->amp_aspiration = bm_amp_from_db(f->ah);
    s->amp_frication  = bm_amp_from_db(f->af);
    s->amp_bypass     = bm_amp_from_db(f->par_bypass);
}

void bm_synth_set_flutter(bm_synth *s, float flutter)
{
    if (s == 0) return;
    s->flutter = bm_clampf(flutter, 0.0f, 1.0f);
}

void bm_synth_set_gain(bm_synth *s, float gain)
{
    if (s == 0) return;
    /* Zero is legitimate (mute); the upper bound only stops a typo in a voice
     * file from producing a filter that deafens someone. */
    s->gain = bm_clampf(gain, 0.0f, 8.0f);
}

float bm_synth_tick(bm_synth *s)
{
    float noise, cascade_in, parallel_in, out, sign, k;
    int   i;

    if (s == 0) return 0.0f;

    /* Chase the frame's amplitude targets at sample rate. Without this every
     * frame boundary is a gain step, and a gain step is a click. */
    k = s->amp_smooth;
    s->cur_voicing    += k * (s->amp_voicing    - s->cur_voicing);
    s->cur_aspiration += k * (s->amp_aspiration - s->cur_aspiration);
    s->cur_frication  += k * (s->amp_frication  - s->cur_frication);
    s->cur_bypass     += k * (s->amp_bypass     - s->cur_bypass);
    for (i = 0; i < BM_NFORMANTS; i++) {
        s->cur_parallel[i] += k * (s->amp_parallel[i] - s->cur_parallel[i]);
    }

    /* One noise generator feeds both aspiration and frication. They are the
     * same physical turbulence; drawing two independent samples would decorate
     * the output with noise that is not there in real speech. */
    noise = bm_noise_tick(&s->noise);

    cascade_in = s->cur_voicing * bm_glottis_tick(&s->glottis)
               + s->cur_aspiration * noise;

    /* Nasal zero before the pole: the zero is what makes a nasal sound nasal.
     * A pole on its own only sounds muffled. */
    out = bm_antiresonator_tick(&s->nasal_zero, cascade_in);
    out = bm_resonator_tick(&s->nasal_pole, out);
    for (i = 0; i < BM_NFORMANTS; i++) {
        out = bm_resonator_tick(&s->cascade[i], out);
    }

    /* Test the smoothed value, not the target: a branch that switches on the
     * target would cut the parallel branch off while it is still decaying and
     * reintroduce the very step this smoothing exists to remove. */
    if (s->cur_frication > 1e-6f || s->cur_bypass > 1e-6f) {
        parallel_in = s->cur_frication * noise;

        out += s->cur_bypass * parallel_in;

        sign = 1.0f;
        for (i = 0; i < BM_NFORMANTS; i++) {
            float band = bm_resonator_tick(&s->parallel[i], parallel_in);
            out += sign * s->cur_parallel[i] * band;
            sign = -sign;
        }
    } else {
        /* Keep the parallel filters running even when silent, so that turning
         * frication on mid-utterance does not start from a cold state and
         * click. Cheap insurance: five resonators at zero input. */
        for (i = 0; i < BM_NFORMANTS; i++) {
            (void)bm_resonator_tick(&s->parallel[i], 0.0f);
        }
    }

    return out * s->gain;
}
