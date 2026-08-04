/*
 * BENCmouth - RIFF/WAVE writer
 *
 * Host layer. Uses stdio and libm freely; the core does not.
 */

#ifndef BM_WAV_H
#define BM_WAV_H

#include <stddef.h>
#include <stdint.h>

typedef struct bm_wav_report {
    float peak;        /* largest |sample| seen before limiting */
    /* Root mean square over the same samples, which is a different question
     * and often a differently-answered one. Drive and crush raise loudness
     * while collapsing the crest factor, so the loudest voices in this
     * synthesizer are routinely the ones with the lowest peaks - Aggressor
     * peaks at a quarter of Gravel and is a decibel quieter, not four times
     * quieter. Peak says how close the limiter is; this says how loud it is. */
    float rms;
    int   limited;     /* nonzero if the soft limiter engaged */
} bm_wav_report;

/* Writes mono 16-bit PCM. Samples are expected in roughly [-1, 1]; anything
 * beyond BM_WAV_LIMIT_THRESHOLD is soft-limited rather than hard-clipped,
 * because a formant synth summing cascade and parallel branches does overshoot
 * on plosives and silent clipping is miserable to diagnose by ear.
 *
 * `report` may be NULL. Pass "-" as path to write to stdout.
 * Returns 0 on success, nonzero on I/O failure. */
int bm_wav_write(const char *path, const float *samples, size_t count,
                 uint32_t sample_rate, bm_wav_report *report);

/* One float sample to 16-bit PCM, soft-limited.
 *
 * Shared with the live audio output rather than duplicated there: if the file
 * and the speaker applied different limiting, a rendered WAV would stop being
 * a faithful record of what you just heard, which is the one thing it is for.
 * `limited` is set nonzero if the limiter engaged; it may be NULL. */
int16_t bm_pcm_sample(float x, int *limited);

#endif /* BM_WAV_H */
