/*
 * BENCmouth - per-phoneme loudness audit
 *
 * Renders every phoneme in isolation and reports peak and RMS relative to a
 * reference vowel. Relative loudness across the inventory is not cosmetic:
 * fricatives that sit too high above vowels is one of the most recognisable
 * ways synthetic speech sounds wrong, and it is invisible in a spectrum plot.
 *
 * Rough targets, from the relative intensities of natural speech:
 *
 *   vowels                 0 dB (reference)
 *   nasals, liquids     -4..-8
 *   /s/ /sh/            -8..-14
 *   voiced fricatives  -14..-20
 *   /f/ /th/           -22..-30
 *   stop bursts        -10..-18
 */

#include "bm_frames.h"
#include "bm_synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 22050u
#define FRAME_HZ    100.0f

static void measure(const char *name, const bm_voice *voice,
                    double *out_peak, double *out_rms)
{
    bm_frame_gen gen;
    bm_synth     synth;
    bm_frame     frame;
    double       peak = 0.0, sumsq = 0.0;
    size_t       n = 0, spf;

    bm_frame_gen_init(&gen, FRAME_HZ, voice);
    if (bm_frame_gen_set_phonemes(&gen, name, 0) != BM_OK) {
        *out_peak = *out_rms = 0.0;
        return;
    }

    bm_synth_init(&synth, (float)SAMPLE_RATE);
    bm_synth_set_flutter(&synth, 0.0f);
    bm_synth_set_gain(&synth, voice->gain);

    spf = SAMPLE_RATE / (unsigned)FRAME_HZ;
    while (bm_frame_gen_next(&gen, &frame)) {
        size_t i;
        bm_synth_set_frame(&synth, &frame);
        for (i = 0; i < spf; i++) {
            double y = (double)bm_synth_tick(&synth);
            double m = fabs(y);
            if (m > peak) peak = m;
            sumsq += y * y;
            n++;
        }
    }

    *out_peak = peak;
    *out_rms = (n > 0) ? sqrt(sumsq / (double)n) : 0.0;
}

static double to_db(double x, double ref)
{
    if (x <= 1e-9 || ref <= 1e-9) return -99.0;
    return 20.0 * log10(x / ref);
}

int main(void)
{
    bm_voice voice;
    double   ref_peak, ref_rms;
    int      i;

    bm_voice_default(&voice);

    /* /a/ is the reference: the loudest vowel and a natural 0 dB anchor. */
    measure("AA1", &voice, &ref_peak, &ref_rms);

    printf("\nper-phoneme levels, relative to AA1\n");
    printf("  peak %.3f  rms %.3f absolute\n\n", ref_peak, ref_rms);
    printf("  %-5s %8s %8s   %8s %8s\n", "ph", "peak", "rms", "peak dB", "rms dB");

    for (i = 0; i < bm_phoneme_count(); i++) {
        const bm_phoneme *p = bm_phoneme_at(i);
        double peak, rms;
        char   token[8];

        if (p->cls == BM_CLS_SILENCE) continue;

        /* Stress digit so vowels get their primary-stress duration. */
        snprintf(token, sizeof token, "%s1", p->name);
        measure(token, &voice, &peak, &rms);

        printf("  %-5s %8.3f %8.4f   %+8.1f %+8.1f%s\n",
               p->name, peak, rms, to_db(peak, ref_peak), to_db(rms, ref_rms),
               peak > 1.0 ? "   << over full scale" : "");
    }
    printf("\n");
    return 0;
}
