/*
 * BENCmouth - RIFF/WAVE writer
 * See bm_wav.h for the contract.
 */

#include "bm_wav.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Below this the signal passes through untouched; above it the limiter takes
 * over. Chosen so ordinary speech never engages it and only genuine overshoot
 * does. */
#define BM_WAV_LIMIT_THRESHOLD 0.85f

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)( v        & 0xFFu);
    p[1] = (unsigned char)((v >>  8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)( v       & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

/* Smooth knee above the threshold, asymptotically approaching 1.0. tanh gives
 * continuous gain and continuous first derivative at the knee, so the onset of
 * limiting is not itself audible as a click. */
static float soft_limit(float x, int *engaged)
{
    const float t = BM_WAV_LIMIT_THRESHOLD;
    float mag = fabsf(x);
    float over;

    if (mag <= t) return x;

    if (engaged != 0) *engaged = 1;
    over = (mag - t) / (1.0f - t);
    mag  = t + (1.0f - t) * tanhf(over);
    return (x < 0.0f) ? -mag : mag;
}

int16_t bm_pcm_sample(float x, int *limited)
{
    long v;

    x = soft_limit(x, limited);
    v = lrintf(x * 32767.0f);
    if (v >  32767) v =  32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

int bm_wav_write(const char *path, const float *samples, size_t count,
                 uint32_t sample_rate, bm_wav_report *report)
{
    unsigned char header[44];
    FILE    *f;
    size_t   i;
    uint32_t data_bytes;
    float    peak = 0.0f;
    int      engaged = 0;
    int      to_stdout;

    if (samples == 0 || count == 0 || sample_rate == 0) return -1;

    data_bytes = (uint32_t)(count * 2u);

    memcpy(header + 0,  "RIFF", 4);
    put_u32(header + 4,  36u + data_bytes);
    memcpy(header + 8,  "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    put_u32(header + 16, 16u);              /* PCM fmt chunk size */
    put_u16(header + 20, 1u);               /* format: PCM */
    put_u16(header + 22, 1u);               /* channels: mono */
    put_u32(header + 24, sample_rate);
    put_u32(header + 28, sample_rate * 2u); /* byte rate */
    put_u16(header + 32, 2u);               /* block align */
    put_u16(header + 34, 16u);              /* bits per sample */
    memcpy(header + 36, "data", 4);
    put_u32(header + 40, data_bytes);

    to_stdout = (path != 0 && path[0] == '-' && path[1] == '\0');
    f = to_stdout ? stdout : fopen(path, "wb");
    if (f == 0) return -1;

    if (fwrite(header, 1, sizeof header, f) != sizeof header) {
        if (!to_stdout) fclose(f);
        return -1;
    }

    for (i = 0; i < count; i++) {
        float mag = fabsf(samples[i]);
        unsigned char pcm[2];

        if (mag > peak) peak = mag;

        put_u16(pcm, (uint16_t)bm_pcm_sample(samples[i], &engaged));
        if (fwrite(pcm, 1, 2, f) != 2) {
            if (!to_stdout) fclose(f);
            return -1;
        }
    }

    if (to_stdout) {
        fflush(f);
    } else if (fclose(f) != 0) {
        return -1;
    }

    if (report != 0) {
        report->peak = peak;
        report->limited = engaged;
    }
    return 0;
}
