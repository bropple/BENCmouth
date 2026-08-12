/*
 * BENCmouth - rendering a score to samples, all at once
 *
 * The engine is a pull interface: queue an utterance, read PCM until it runs
 * out. That is exactly right for speaking, and it is not a timeline - there is
 * no seeking in it, and nothing that can answer "give me the audio from bar
 * nine". A DAW asks that constantly, and so does anyone dragging a playhead.
 *
 * Rather than teaching the engine to seek, render the whole thing once and keep
 * it. The synthesizer runs at about ninety-seven times real time - Daisy Bell,
 * eleven point nine seconds of audio, renders in a tenth of a second including
 * process start and the WAV write - so a re-render when the score changes is
 * affordable, and everything downstream of it becomes a memcpy. Scrubbing,
 * looping and playing from the middle all stop being engine problems.
 *
 * This is the half that costs something and runs on whatever thread can afford
 * it. bm_player.h is the half that runs in the audio callback.
 */

#ifndef BM_RENDER_H
#define BM_RENDER_H

#include "bencmouth.h"

#include <stddef.h>

typedef struct bm_render {
    float   *pcm;      /* mono, owned */
    size_t   len;      /* samples in use */
    size_t   cap;      /* samples allocated */
    uint32_t rate;     /* what they were rendered at */
} bm_render;

/* Renders `phonemes` - the same string bm_speak_phonemes() takes, markup and
 * all - into `r`, growing its buffer if it has to and reusing it if it does
 * not. `config` carries the voice, the effects and the sample rate.
 *
 * Returns 0, or -1 with a message in `err`. On failure `r->len` is zero rather
 * than half a score: a partial render would be a song that stops in the middle
 * for no reason a listener could account for.
 *
 * IMPORTANT: this reallocates. Nothing may be reading r->pcm while it runs -
 * see the note in bm_player.h about how to hand a new render to an audio
 * thread that is playing the old one.
 */
int  bm_render_score(bm_render *r, const char *phonemes,
                     const bm_config *config, char *err, size_t err_cap);

void bm_render_free(bm_render *r);

/* Milliseconds of audio held. */
double bm_render_ms(const bm_render *r);

#endif /* BM_RENDER_H */
