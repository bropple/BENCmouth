/*
 * BENCmouth - playing a rendered score
 *
 * The other half of bm_render.h. That one turns a score into samples and costs
 * something; this one hands those samples to an audio callback and costs
 * nothing - no allocation, no locks, no engine. Everything a timeline wants
 * that the engine cannot do becomes arithmetic here: playing from the middle is
 * an index, scrubbing is a different index, looping is a subtraction.
 *
 * It is deliberately not driven by its own clock. bm_player_read advances the
 * position because a standalone player has nothing else to advance it, but a
 * host with a transport sets the position every block instead - see
 * bm_player_locate - and then the player is only a window onto a buffer. That
 * is the shape a plugin needs, and building it this way round means the
 * standalone and the plugin play through the same code.
 *
 * Threads. `pcm`, `len` and the flags are written by whoever owns the score and
 * read by the audio thread; `pos` is the other way about. All of them are
 * single words, so a reader sees an old value or a new one and never half of
 * each, which for a playhead is the difference between a frame of staleness and
 * a fault. The one thing that is *not* safe is freeing or reallocating the
 * buffer under a running callback: stop the player, or publish the new buffer
 * and retire the old one a render later. bm_render_score reallocates.
 */

#ifndef BM_PLAYER_H
#define BM_PLAYER_H

#include "bencmouth.h"

#include <stddef.h>

typedef struct bm_player {
    /* Not owned. Whoever rendered it keeps it alive.
     *
     * Volatile because these two have to be written in an order the compiler
     * may not rearrange: the length is dropped to zero before the pointer moves
     * and raised again after it. Otherwise a callback could pick up the new
     * buffer against the old length and read off the end of a shorter render -
     * a rare crash that would only ever happen to somebody editing while
     * something was playing, which is the whole point of the tab. */
    const float * volatile pcm;
    volatile size_t        len;
    uint32_t               rate;

    volatile size_t pos;        /* samples from the start */
    volatile int    playing;
    volatile int    loop;

    /* The loop, in samples. `loop_to` of 0 means the end of what there is, so
     * a player that has never been told about a loop still loops the whole
     * thing when asked to. */
    size_t loop_from;
    size_t loop_to;
} bm_player;

void bm_player_init(bm_player *p);

/* Points the player at a rendered score. Stops it: the position it held was an
 * index into a buffer that no longer exists. */
void bm_player_set_source(bm_player *p, const float *pcm, size_t len,
                          uint32_t rate);

void bm_player_play(bm_player *p);
void bm_player_stop(bm_player *p);

/* Moves the playhead. Past the end clamps to it rather than wrapping, which is
 * what dragging a playhead past the last note should do. */
void bm_player_seek_ms(bm_player *p, double ms);

/* The same in samples, for a host that thinks in them. Named for what a plugin
 * host calls it. */
void bm_player_locate(bm_player *p, size_t sample);

double bm_player_pos_ms(const bm_player *p);

/* Fills `frames` samples and returns how many of them came from the score.
 *
 * The rest are zeros: a block is always filled, because an audio callback that
 * returns short leaves whatever was in the buffer before. The return value is
 * for meters, which should not average in the silence after the end.
 *
 * Safe to call from an audio callback: no allocation, no locks, no syscalls.
 */
size_t bm_player_read(bm_player *p, float *out, size_t frames);

#endif /* BM_PLAYER_H */
