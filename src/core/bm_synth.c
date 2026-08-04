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

#if BM_FIXED_POINT
#define BM_AMP(db)   bm_q_from_float(bm_amp_from_db(db))
#define BM_ZERO      ((bm_q)0)
#define BM_SMOOTH(f) bm_q_from_float(f)
typedef bm_q bm_signal;
#define BM_SIG_FROM_F(x) bm_q_from_float(x)
#define BM_SIG_TO_F(x)   bm_q_to_float(x)
#define BM_SIG_MUL(a,b)  bm_q_mul((a), (b))
#define BM_SIG_ADD(a,b)  bm_q_add((a), (b))
#define BM_SIG_NEG(a)    ((bm_q)-(a))
#else
#define BM_AMP(db)   bm_amp_from_db(db)
#define BM_ZERO      0.0f
#define BM_SMOOTH(f) (f)
typedef float bm_signal;
#define BM_SIG_FROM_F(x) (x)
#define BM_SIG_TO_F(x)   (x)
#define BM_SIG_MUL(a,b)  ((a) * (b))
#define BM_SIG_ADD(a,b)  ((a) + (b))
#define BM_SIG_NEG(a)    (-(a))
#endif

void bm_synth_init(bm_synth *s, float sample_rate)
{
    int i;

    if (s == 0) return;

    s->sample_rate = (sample_rate > 0.0f) ? sample_rate : 1.0f;

    bm_glottis_init(&s->glottis, s->sample_rate);
    bm_noise_init(&s->noise, s->sample_rate, 0u);
    bm_effects_state_init(&s->effects, s->sample_rate);

    /* Park every resonator somewhere harmless so a tick before the first
     * set_frame produces silence rather than uninitialized coefficients. */
    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_set(&s->cascade[i], 1000.0f, 100.0f, s->sample_rate);
        bm_resonator_set(&s->parallel[i], 1000.0f, 100.0f, s->sample_rate);
        s->amp_parallel[i] = BM_ZERO;
    }
    bm_resonator_set(&s->nasal_pole, 270.0f, 100.0f, s->sample_rate);
    bm_antiresonator_set(&s->nasal_zero, 270.0f, 100.0f, s->sample_rate);

    s->amp_voicing = BM_ZERO;
    s->amp_aspiration = BM_ZERO;
    s->amp_frication = BM_ZERO;
    s->amp_bypass = BM_ZERO;
    s->flutter = 0.0f;
    s->gain = 1.0f;

    /* One-pole coefficient for a ~0.8 ms time constant, so an amplitude change
     * settles in roughly 2.5 ms. Fast enough that a plosive burst still sounds
     * like a burst, slow enough that the frame-boundary step stops clicking. */
    {
        float k = 1.0f - bm_expf(-1.0f / (0.0008f * s->sample_rate));
        if (k > 1.0f) k = 1.0f;
        s->amp_smooth = BM_SMOOTH(k);
    }

    bm_synth_reset(s);
}

void bm_synth_reset(bm_synth *s)
{
    int i;

    if (s == 0) return;

    bm_glottis_reset(&s->glottis);
    bm_noise_reset(&s->noise);
    bm_effects_state_reset(&s->effects);

    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_reset(&s->cascade[i]);
        bm_resonator_reset(&s->parallel[i]);
        s->cur_parallel[i] = BM_ZERO;
    }
    bm_resonator_reset(&s->nasal_pole);
    bm_resonator_reset(&s->nasal_zero);

    /* Start silent rather than at the current targets, so an utterance fades
     * up from nothing instead of opening with the same step we just removed. */
    s->cur_voicing = BM_ZERO;
    s->cur_aspiration = BM_ZERO;
    s->cur_frication = BM_ZERO;
    s->cur_bypass = BM_ZERO;
}

void bm_synth_set_frame(bm_synth *s, const bm_frame *f)
{
    int i;

    if (s == 0 || f == 0) return;

    bm_glottis_set(&s->glottis, f->f0, f->open_quotient, f->tilt, s->flutter);

    for (i = 0; i < BM_NFORMANTS; i++) {
        bm_resonator_set(&s->cascade[i],  f->freq[i], f->bw[i], s->sample_rate);
        bm_resonator_set(&s->parallel[i], f->freq[i], f->bw[i], s->sample_rate);
        s->amp_parallel[i] = BM_AMP(f->par_amp[i]);
    }

    bm_resonator_set(&s->nasal_pole, f->nasal_pole_f, f->nasal_pole_bw,
                     s->sample_rate);
    bm_antiresonator_set(&s->nasal_zero, f->nasal_zero_f, f->nasal_zero_bw,
                         s->sample_rate);

    s->amp_voicing    = BM_AMP(f->av);
    s->amp_aspiration = BM_AMP(f->ah);
    s->amp_frication  = BM_AMP(f->af);
    s->amp_bypass     = BM_AMP(f->par_bypass);
}

void bm_synth_set_flutter(bm_synth *s, float flutter)
{
    if (s == 0) return;
    s->flutter = bm_clampf(flutter, 0.0f, 1.0f);
}

void bm_synth_set_effects(bm_synth *s, const bm_effects *effects)
{
    if (s == 0 || effects == 0) return;
    bm_effects_state_set(&s->effects, effects);
}

void bm_synth_set_vibrato(bm_synth *s, float semitones, float rate_hz)
{
    if (s == 0) return;
    /* Straight through to the source. It is kept on the synth's interface
     * rather than exposing the glottis, because the glottis is an
     * implementation detail of this file's topology and the engine above has
     * no business knowing there is one. */
    bm_glottis_set_vibrato(&s->glottis, semitones, rate_hz);
}

void bm_synth_set_gain(bm_synth *s, float gain)
{
    if (s == 0) return;
    /* Zero is legitimate (mute); the upper bound only stops a typo in a voice
     * file from producing a filter that deafens someone. */
    s->gain = bm_clampf(gain, 0.0f, 8.0f);
}

/* One step of the amplitude smoother, in whichever representation. */
#if BM_FIXED_POINT
#define BM_CHASE(cur, target, k) \
    ((cur) = bm_q_add((cur), bm_q_mul((k), bm_q_add((target), -(cur)))))
#define BM_TICK_RES(r, x)  bm_resonator_tick_q((r), (x))
#define BM_TICK_ANTI(r, x) bm_antiresonator_tick_q((r), (x))
/* Comparable to 1e-6 in float, in Q16 units. */
#define BM_SIG_EPS ((bm_q)1)
#else
#define BM_CHASE(cur, target, k) ((cur) += (k) * ((target) - (cur)))
#define BM_TICK_RES(r, x)  bm_resonator_tick((r), (x))
#define BM_TICK_ANTI(r, x) bm_antiresonator_tick((r), (x))
#define BM_SIG_EPS 1e-6f
#endif

float bm_synth_tick(bm_synth *s)
{
    bm_signal noise, cascade_in, parallel_in, out;
    int   i;

    if (s == 0) return 0.0f;

    /* Chase the frame's amplitude targets at sample rate. Without this every
     * frame boundary is a gain step, and a gain step is a click. */
    BM_CHASE(s->cur_voicing,    s->amp_voicing,    s->amp_smooth);
    BM_CHASE(s->cur_aspiration, s->amp_aspiration, s->amp_smooth);
    BM_CHASE(s->cur_frication,  s->amp_frication,  s->amp_smooth);
    BM_CHASE(s->cur_bypass,     s->amp_bypass,     s->amp_smooth);
    for (i = 0; i < BM_NFORMANTS; i++) {
        BM_CHASE(s->cur_parallel[i], s->amp_parallel[i], s->amp_smooth);
    }

    /* One noise generator feeds both aspiration and frication. They are the
     * same physical turbulence; drawing two independent samples would decorate
     * the output with noise that is not there in real speech.
     *
     * The two sources stay in float - there is one of each per sample against
     * twelve resonators, and their polynomial and PRNG arithmetic gains nothing
     * from conversion. They are converted once on the way in. */
    noise = BM_SIG_FROM_F(bm_noise_tick(&s->noise));

    cascade_in = BM_SIG_ADD(
        BM_SIG_MUL(s->cur_voicing, BM_SIG_FROM_F(bm_glottis_tick(&s->glottis))),
        BM_SIG_MUL(s->cur_aspiration, noise));

    /* Nasal zero before the pole: the zero is what makes a nasal sound nasal.
     * A pole on its own only sounds muffled. */
    out = BM_TICK_ANTI(&s->nasal_zero, cascade_in);
    out = BM_TICK_RES(&s->nasal_pole, out);
    for (i = 0; i < BM_NFORMANTS; i++) {
        out = BM_TICK_RES(&s->cascade[i], out);
    }

    /* Test the smoothed value, not the target: a branch that switches on the
     * target would cut the parallel branch off while it is still decaying and
     * reintroduce the very step this smoothing exists to remove. */
    if (s->cur_frication > BM_SIG_EPS || s->cur_bypass > BM_SIG_EPS) {
        parallel_in = BM_SIG_MUL(s->cur_frication, noise);

        out = BM_SIG_ADD(out, BM_SIG_MUL(s->cur_bypass, parallel_in));

        for (i = 0; i < BM_NFORMANTS; i++) {
            bm_signal band = BM_TICK_RES(&s->parallel[i], parallel_in);
            bm_signal term = BM_SIG_MUL(s->cur_parallel[i], band);
            /* Successive parallel formants alternate sign; in phase they fill
             * the spectral valleys and the result sounds muffled. */
            out = BM_SIG_ADD(out, (i & 1) ? BM_SIG_NEG(term) : term);
        }
    } else {
        /* Keep the parallel filters running even when silent, so that turning
         * frication on mid-utterance does not start from a cold state and
         * click. Cheap insurance: five resonators at zero input. */
        for (i = 0; i < BM_NFORMANTS; i++) {
            (void)BM_TICK_RES(&s->parallel[i], (bm_signal)0);
        }
    }

    /* Effects last, after the voice's own trim - see bm_synth.effects. With
     * nothing switched on bm_effects_tick returns its argument, so this line
     * is exactly `* s->gain` for every voice that asked for no effects, which
     * is what keeps the Retro reference byte-identical. */
    return bm_effects_tick(&s->effects, BM_SIG_TO_F(out) * s->gain);
}
