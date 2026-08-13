/*
 * BENCmouth - measuring an utterance without rendering it
 *
 * See the Measuring section of bencmouth.h for what this is for. The short
 * version: the length of a sung note is not the number written beside it, and
 * anything drawing a timeline has to be told the truth by the code that makes
 * the sound rather than working it out again beside it.
 *
 * There is deliberately no arithmetic here. Every duration comes from
 * bm_frame_gen_phoneme_frames(), which is the renderer's own segment_frames()
 * summed up - so a span cannot drift from the audio, because there is only one
 * place that decides how long anything is. The whole of this file is walking a
 * sequence and converting frames to milliseconds.
 */

#include "bencmouth.h"
#include "bm_frames.h"

#include <stddef.h>

/* The frame generator is the large half of an engine, so it fits in the storage
 * an engine would have taken. Asserted rather than assumed: a caller who hands
 * over a bm_engine_storage is entitled to be sure it is big enough, and finding
 * out otherwise at runtime would be a stack overrun on the target with the
 * least memory. */
typedef char bm_gen_fits_in_storage[
    (sizeof(bm_frame_gen) <= BM_ENGINE_RESERVED) ? 1 : -1];

/* Frames to milliseconds, rounded to nearest.
 *
 * Applied to accumulated frame counts rather than to each phoneme in turn, so
 * that rounding cannot accumulate: the start of the hundredth phoneme is
 * converted from the hundred frame counts before it, not from ninety-nine
 * roundings added together. */
static uint32_t frames_to_ms(int frames, uint32_t frame_rate)
{
    uint64_t f;

    if (frames <= 0 || frame_rate == 0u) return 0u;
    f = (uint64_t)frames * 1000u + frame_rate / 2u;
    return (uint32_t)(f / frame_rate);
}

bm_result bm_measure(bm_engine_storage *scratch, const bm_config *config,
                     const char *phonemes, size_t len,
                     bm_span *out, size_t out_cap, size_t *out_count,
                     uint32_t *total_ms)
{
    bm_frame_gen *g;
    bm_config     cfg;
    bm_result     rc;
    int           i, at = 0;

    if (scratch == 0 || phonemes == 0) return BM_ERR_ARG;
    if (out == 0 && out_cap != 0) return BM_ERR_ARG;

    if (config != 0) cfg = *config;
    else             bm_config_default(&cfg);
    if (cfg.frame_rate == 0u) return BM_ERR_ARG;

    g = (bm_frame_gen *)(void *)scratch;
    bm_frame_gen_init(g, (float)cfg.frame_rate, &cfg.voice);

    rc = bm_frame_gen_set_phonemes(g, phonemes, len);
    if (rc != BM_OK) return rc;

    for (i = 0; i < g->count; i++) {
        int n = bm_frame_gen_phoneme_frames(g, i);

        if (out != 0 && (size_t)i < out_cap) {
            const bm_phoneme *p = g->seq[i];

            out[i].start_ms  = frames_to_ms(at, cfg.frame_rate);
            /* The difference of two converted totals, not the conversion of the
             * difference: spans then abut exactly, and a caller adding them up
             * gets the total back. */
            out[i].length_ms = frames_to_ms(at + n, cfg.frame_rate) -
                               frames_to_ms(at, cfg.frame_rate);
            out[i].source    = g->src[i];
            out[i].group     = g->mod[i].dur_group;
            out[i].vowel     = (unsigned char)
                               ((p != 0 && (p->cls == BM_CLS_VOWEL ||
                                            p->cls == BM_CLS_DIPHTHONG)) ? 1 : 0);
            out[i].reserved  = 0u;
        }
        at += n;
    }

    /* The total comes from the generator rather than from `at`, so that what is
     * reported here and what bm_read() will produce come from one number. They
     * agree today; taking the generator's own is what keeps them agreeing. */
    if (total_ms != 0) {
        *total_ms = frames_to_ms(bm_frame_gen_length(g), cfg.frame_rate);
    }
    if (out_count != 0) *out_count = (size_t)g->count;

    if (out != 0 && (size_t)g->count > out_cap) return BM_ERR_OVERFLOW;
    return BM_OK;
}
