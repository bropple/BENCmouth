/*
 * BENCmouth - rendering a score to samples, all at once
 * See bm_render.h for why this exists rather than seeking in the engine.
 */

#include "bm_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Samples pulled per call. Any size works; this is one engine block's worth of
 * a few hundred milliseconds, which keeps the realloc count down without
 * asking for a large scratch. */
#define CHUNK 4096

void bm_render_free(bm_render *r)
{
    if (r == 0) return;
    free(r->pcm);
    r->pcm = 0;
    r->len = 0;
    r->cap = 0;
}

double bm_render_ms(const bm_render *r)
{
    if (r == 0 || r->rate == 0u) return 0.0;
    return 1000.0 * (double)r->len / (double)r->rate;
}

int bm_render_score(bm_render *r, const char *phonemes,
                    const bm_config *config, char *err, size_t err_cap)
{
    bm_engine_storage *storage;
    bm_engine         *e = 0;
    bm_config          cfg;
    bm_result          rc;

    if (r == 0 || phonemes == 0) return -1;

    r->len = 0;

    if (config != 0) cfg = *config;
    else             bm_config_default(&cfg);
    /* A score is phonemes, and bm_speak_phonemes honours markup whatever this
     * says - but the flag travels with the config and something downstream may
     * read it, so it says what is true. */
    cfg.markup = 1;
    r->rate = cfg.sample_rate;

    if (phonemes[0] == '\0') return 0;      /* nothing to sing is not an error */

    /* On the heap rather than the stack: an engine is 128 KB reserved, and this
     * may be called from a worker thread whose stack is not a desktop's. */
    storage = (bm_engine_storage *)malloc(sizeof *storage);
    if (storage == 0) {
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "out of memory");
        return -1;
    }

    if (bm_engine_init(storage, &cfg, &e) != BM_OK) {
        free(storage);
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "engine init failed");
        return -1;
    }

    rc = bm_speak_phonemes(e, phonemes, 0);
    if (rc != BM_OK) {
        free(storage);
        if (err != 0 && err_cap > 0) {
            snprintf(err, err_cap, "%s", bm_strerror(rc));
        }
        return -1;
    }

    while (bm_is_speaking(e)) {
        size_t got;

        if (r->len + CHUNK > r->cap) {
            size_t want = (r->cap != 0) ? r->cap * 2 : (size_t)CHUNK * 16;
            float *grown;

            while (want < r->len + CHUNK) want *= 2;
            grown = (float *)realloc(r->pcm, want * sizeof *grown);
            if (grown == 0) {
                free(storage);
                r->len = 0;
                if (err != 0 && err_cap > 0) snprintf(err, err_cap, "out of memory");
                return -1;
            }
            r->pcm = grown;
            r->cap = want;
        }

        got = bm_read(e, r->pcm + r->len, CHUNK);
        if (got == 0) break;
        r->len += got;
    }

    free(storage);
    return 0;
}
