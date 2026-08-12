/*
 * BENCmouth - playing a rendered score
 * See bm_player.h. There is nothing in here but indices.
 */

#include "bm_player.h"
#include "bm_fence.h"

#include <string.h>

void bm_player_init(bm_player *p)
{
    if (p == 0) return;
    memset(p, 0, sizeof *p);
    p->rate = 22050u;
}

void bm_player_set_source(bm_player *p, const float *pcm, size_t len,
                          uint32_t rate)
{
    if (p == 0) return;

    /* Stopped, emptied, moved, and only then given its new length, with a
     * fence between each step that matters. A callback arriving partway
     * through sees either no audio or the right audio, and never a new pointer
     * measured by an old length - which on a shorter render is a read off the
     * end of the buffer.
     *
     * The fences are the point. `volatile` keeps the compiler from reordering
     * these four lines and does nothing at all about the processor, which on
     * arm64 may make them visible in any order it likes. See bm_fence.h. */
    p->playing = 0;
    p->len = 0;
    BM_FENCE();
    p->pcm = pcm;
    BM_FENCE();
    p->len = len;
    if (rate > 0u) p->rate = rate;
    if (p->pos > len) p->pos = len;
    if (p->loop_from > len) p->loop_from = 0;
    if (p->loop_to > len) p->loop_to = 0;
}

void bm_player_play(bm_player *p)
{
    if (p == 0 || p->pcm == 0 || p->len == 0) return;
    /* Playing from the end plays nothing at all, which looks like a broken
     * button. Somebody pressing play with the head at the end means the top. */
    if (p->pos >= p->len) p->pos = 0;
    p->playing = 1;
}

void bm_player_stop(bm_player *p)
{
    if (p == 0) return;
    p->playing = 0;
}

void bm_player_locate(bm_player *p, size_t sample)
{
    if (p == 0) return;
    p->pos = (sample > p->len) ? p->len : sample;
}

void bm_player_seek_ms(bm_player *p, double ms)
{
    double s;

    if (p == 0 || p->rate == 0u) return;
    if (ms < 0.0) ms = 0.0;

    s = ms * (double)p->rate / 1000.0;
    bm_player_locate(p, (size_t)(s + 0.5));
}

double bm_player_pos_ms(const bm_player *p)
{
    if (p == 0 || p->rate == 0u) return 0.0;
    return 1000.0 * (double)p->pos / (double)p->rate;
}

size_t bm_player_read(bm_player *p, float *out, size_t frames)
{
    const float *pcm;
    size_t done = 0, real = 0;
    size_t len, end;

    if (out == 0 || frames == 0) return 0;
    if (p == 0 || !p->playing) {
        if (out != 0) memset(out, 0, frames * sizeof *out);
        return 0;
    }

    /* Read once, into locals. The score is written by another thread, and a
     * loop that re-read it could take its start from one moment and its end
     * from the next - which is how a block ends up copying from outside the
     * buffer. Everything below works from this snapshot. */
    pcm = (const float *)p->pcm;
    BM_FENCE();
    len = p->len;
    if (pcm == 0 || len == 0) {
        memset(out, 0, frames * sizeof *out);
        return 0;
    }

    end = (p->loop && p->loop_to > p->loop_from && p->loop_to <= len)
              ? p->loop_to : len;

    while (done < frames) {
        size_t pos = p->pos;
        size_t run;

        if (pos >= end) {
            if (!p->loop) {
                /* Out of score. The rest of the block is silence and the
                 * transport stops itself, which is what the standalone needs;
                 * a host-driven one never gets here, because the host locates
                 * it somewhere else before the next block. */
                p->playing = 0;
                p->pos = end;
                break;
            }
            pos = (p->loop_from < end) ? p->loop_from : 0;
            p->pos = pos;
        }

        run = end - pos;
        if (run > frames - done) run = frames - done;

        memcpy(out + done, pcm + pos, run * sizeof *out);
        p->pos = pos + run;
        done += run;
        real += run;
    }

    if (done < frames) memset(out + done, 0, (frames - done) * sizeof *out);
    return real;
}
