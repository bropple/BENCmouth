/*
 * BENCmouth - voiced excitation source
 *
 * Produces the derivative of glottal volume velocity, not the flow itself.
 * That is deliberate and it matters: lip radiation is approximately a
 * differentiator, so a source that is already differentiated folds the
 * radiation characteristic in for free and the synthesizer needs no separate
 * radiation stage. It also puts the sharp discontinuity where physics puts it,
 * at glottal closure, which is what actually excites the vocal tract.
 *
 * The pulse shape follows the polynomial-flow family described by Klatt: flow
 * rises as t^2 and falls back to zero as t^3 across the open phase, so its
 * derivative is a smooth positive rise followed by an abrupt negative-going
 * closure. Normalized so the closure excursion is exactly -1 regardless of
 * pitch or open quotient - without that, changing F0 would change loudness.
 */

#ifndef BM_GLOTTIS_H
#define BM_GLOTTIS_H

typedef struct bm_glottis {
    float sample_rate;

    float phase;          /* position within the current period, 0..1 */
    float phase_inc;      /* advance per sample */
    float open_quotient;  /* fraction of the period the glottis is open */

    float tilt_coeff;     /* one-pole spectral tilt; 0 disables */
    float tilt_z;

    float flutter;        /* 0..1 */
    float f0;             /* requested, before flutter */
    float flutter_phase[3];
} bm_glottis;

void bm_glottis_init(bm_glottis *g, float sample_rate);
void bm_glottis_reset(bm_glottis *g);

/* Sets pitch and voice quality. `tilt_db` is extra attenuation at 3 kHz
 * (0 = no tilt, larger = softer and less buzzy). `flutter` is 0..1. */
void bm_glottis_set(bm_glottis *g, float f0, float open_quotient,
                    float tilt_db, float flutter);

/* One sample of the flow derivative, peak closure excursion -1. */
float bm_glottis_tick(bm_glottis *g);

#endif /* BM_GLOTTIS_H */
