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

#endif /* BM_WAV_H */
