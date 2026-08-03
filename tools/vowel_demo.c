/*
 * BENCmouth - vowel demo
 *
 * The step-3 milestone from ARCHITECTURE.md: drive the DSP with hand-written
 * frames and confirm that a sustained vowel sounds like that vowel. No phoneme
 * layer, no prosody, no text - just formant targets straight into bm_synth.
 *
 * Formant values are the classic Peterson & Barney (1952) adult-male averages,
 * which are published measurement data rather than anyone's implementation.
 *
 *   ./vowel_demo out.wav
 */

#include "bm_synth.h"
#include "bm_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE   22050u
#define FRAME_HZ      100
#define VOWEL_SECONDS 0.55
#define GAP_SECONDS   0.12
#define FADE_SECONDS  0.020

/* F4 and F5 vary little between vowels and mostly contribute presence rather
 * than identity, so they are held fixed. */
#define F4_HZ 3500.0f
#define F5_HZ 4500.0f

static const struct {
    const char *name;
    float f1, f2, f3;
} VOWELS[] = {
    { "/i/ as in heed",  270.0f, 2290.0f, 3010.0f },
    { "/e/ as in head",  530.0f, 1840.0f, 2480.0f },
    { "/a/ as in hod",   730.0f, 1090.0f, 2440.0f },
    { "/o/ as in hawed", 570.0f,  840.0f, 2410.0f },
    { "/u/ as in who'd", 300.0f,  870.0f, 2240.0f }
};
#define NVOWELS ((int)(sizeof VOWELS / sizeof VOWELS[0]))

static const float BANDWIDTH[BM_NFORMANTS] = {
    60.0f, 90.0f, 150.0f, 200.0f, 250.0f
};

static void build_frame(bm_frame *f, int vowel, float f0)
{
    int i;

    memset(f, 0, sizeof *f);

    f->f0 = f0;
    /* 60 dB would be full scale *at the source*, but the cascade contributes
     * gain of its own - around 2.7x for these formants - so driving the source
     * at full scale overshoots. How much gain the cascade adds depends on the
     * vowel, and those differences are perceptually real, so the synthesizer
     * deliberately does not normalize them away. Leaving headroom is the
     * caller's job, and will become the frame layer's job once it exists. */
    f->av = 48.0f;
    f->ah = 0.0f;              /* no aspiration */
    f->af = 0.0f;              /* no frication */
    f->open_quotient = 0.50f;
    f->tilt = 6.0f;            /* a little softening; 0 is buzzier */

    f->freq[0] = VOWELS[vowel].f1;
    f->freq[1] = VOWELS[vowel].f2;
    f->freq[2] = VOWELS[vowel].f3;
    f->freq[3] = F4_HZ;
    f->freq[4] = F5_HZ;
    for (i = 0; i < BM_NFORMANTS; i++) {
        f->bw[i] = BANDWIDTH[i];
        f->par_amp[i] = 0.0f;  /* parallel branch silent for vowels */
    }
    f->par_bypass = 0.0f;

    /* Nasal pole and zero coincident cancels them exactly - the pair is
     * DC-normalized and one is the algebraic inverse of the other - which is
     * how a non-nasal sound switches the nasal branch off without a flag. */
    f->nasal_pole_f  = 270.0f;
    f->nasal_pole_bw = 100.0f;
    f->nasal_zero_f  = 270.0f;
    f->nasal_zero_bw = 100.0f;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "render/vowels.wav";

    const size_t samples_per_frame = SAMPLE_RATE / (unsigned)FRAME_HZ;
    const size_t vowel_samples = (size_t)(VOWEL_SECONDS * SAMPLE_RATE);
    const size_t gap_samples   = (size_t)(GAP_SECONDS * SAMPLE_RATE);
    const size_t fade_samples  = (size_t)(FADE_SECONDS * SAMPLE_RATE);
    const size_t total = (size_t)NVOWELS * (vowel_samples + gap_samples);

    bm_synth  synth;
    bm_frame  frame;
    float    *out;
    bm_wav_report report;
    size_t    pos = 0;
    int       v;

    out = (float *)calloc(total, sizeof *out);
    if (out == 0) { fprintf(stderr, "out of memory\n"); return 1; }

    bm_synth_init(&synth, (float)SAMPLE_RATE);
    bm_synth_set_flutter(&synth, 0.35f);

    for (v = 0; v < NVOWELS; v++) {
        size_t n;

        bm_synth_reset(&synth);
        printf("  %-18s F1=%4.0f  F2=%4.0f  F3=%4.0f\n",
               VOWELS[v].name, (double)VOWELS[v].f1,
               (double)VOWELS[v].f2, (double)VOWELS[v].f3);

        for (n = 0; n < vowel_samples; n++) {
            float env = 1.0f;

            if (n % samples_per_frame == 0) {
                /* Gently falling pitch across the vowel. A dead-flat F0 reads
                 * as a machine even when the formants are perfect. */
                float t  = (float)n / (float)vowel_samples;
                float f0 = 122.0f - 14.0f * t;
                build_frame(&frame, v, f0);
                bm_synth_set_frame(&synth, &frame);
            }

            /* Raised-cosine fades. Starting or stopping voicing abruptly puts a
             * step into the filter state and clicks. */
            if (n < fade_samples) {
                float t = (float)n / (float)fade_samples;
                env = 0.5f - 0.5f * (float)cos(3.14159265358979 * (double)t);
            } else if (n > vowel_samples - fade_samples) {
                float t = (float)(vowel_samples - n) / (float)fade_samples;
                env = 0.5f - 0.5f * (float)cos(3.14159265358979 * (double)t);
            }

            out[pos++] = env * bm_synth_tick(&synth);
        }

        pos += gap_samples;
    }

    if (bm_wav_write(path, out, total, SAMPLE_RATE, &report) != 0) {
        fprintf(stderr, "failed to write %s\n", path);
        free(out);
        return 1;
    }

    printf("\n  wrote %s  (%.2f s, peak %.3f%s)\n",
           path, (double)total / (double)SAMPLE_RATE,
           (double)report.peak,
           report.limited ? ", LIMITER ENGAGED" : "");

    free(out);
    return 0;
}
