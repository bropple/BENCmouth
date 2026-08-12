/*
 * BENCmouth - offline render and transport tests
 *
 * These two are what a timeline is made of, and they are the pieces a plugin
 * will be built on - so they are tested here, in a program with no audio device
 * and no window, rather than by listening to the result later.
 *
 * The case that matters most is a block that spans the loop point. It is the
 * one an audio callback hits a few times a second and the one that is silently
 * wrong if the arithmetic is off: a player that zero-fills the tail of that
 * block instead of wrapping produces a click every time round, which sounds
 * like a bad loop point rather than like a bug.
 */

#include "../src/host/bm_player.c"
#include "../src/host/bm_render.c"

#include <stdio.h>
#include <math.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* A ramp, so that every sample says where it came from. */
static float source[1000];

static void fill_source(void)
{
    int i;
    for (i = 0; i < 1000; i++) source[i] = (float)i;
}

/* ------------------------------------------------------------------ */

static void test_plays_from_the_beginning(void)
{
    bm_player p;
    float     out[16];
    size_t    real;

    printf("playing\n");

    bm_player_init(&p);
    bm_player_set_source(&p, source, 1000, 1000u);

    real = bm_player_read(&p, out, 16);
    check(real == 0, "a player that has not been started is silent");
    check(out[0] == 0.0f && out[15] == 0.0f, "and fills the block with zeros");

    bm_player_play(&p);
    real = bm_player_read(&p, out, 16);
    check(real == 16, "started, it plays");
    check(out[0] == 0.0f && out[1] == 1.0f && out[15] == 15.0f,
          "from the beginning, in order");

    real = bm_player_read(&p, out, 16);
    check(out[0] == 16.0f, "and carries on where it left off");
    check(real == 16, "with a full block");
}

static void test_seeking(void)
{
    bm_player p;
    float     out[8];

    printf("seeking\n");

    bm_player_init(&p);
    bm_player_set_source(&p, source, 1000, 1000u);

    /* 1000 samples at 1000 Hz is one second, so a millisecond is a sample. */
    bm_player_seek_ms(&p, 500.0);
    check(p.pos == 500, "half a second in is sample 500");
    check(fabs(bm_player_pos_ms(&p) - 500.0) < 0.001,
          "and it says so when asked");

    bm_player_play(&p);
    (void)bm_player_read(&p, out, 8);
    check(out[0] == 500.0f, "playing starts from where it was left");

    bm_player_seek_ms(&p, 99999.0);
    check(p.pos == 1000, "seeking past the end stops at the end");
    bm_player_seek_ms(&p, -50.0);
    check(p.pos == 0, "and before the beginning at the beginning");

    /* A host locating the transport is the same operation by another name. */
    bm_player_locate(&p, 250);
    check(p.pos == 250, "a host can put it anywhere too");
}

static void test_the_end(void)
{
    bm_player p;
    float     out[16];
    size_t    real;

    printf("the end of the score\n");

    bm_player_init(&p);
    bm_player_set_source(&p, source, 1000, 1000u);
    bm_player_seek_ms(&p, 990.0);        /* ten samples left */
    bm_player_play(&p);

    real = bm_player_read(&p, out, 16);
    check(real == 10, "the last block is short of real samples");
    check(out[9] == 999.0f, "which are the ones that were there");
    check(out[10] == 0.0f && out[15] == 0.0f,
          "and the rest of it is silence, not whatever was in the buffer");
    check(p.playing == 0, "the transport stops itself");

    /* Pressing play again from the end starts over rather than doing nothing,
     * which is what a button that appears to be broken feels like. */
    bm_player_play(&p);
    check(p.playing == 1 && p.pos == 0, "play at the end starts from the top");
}

/* The one that matters. */
static void test_a_block_across_the_loop_point(void)
{
    bm_player p;
    float     out[32];
    size_t    real, i;
    int       wrapped_at = -1;

    printf("looping\n");

    bm_player_init(&p);
    bm_player_set_source(&p, source, 1000, 1000u);
    p.loop = 1;
    p.loop_from = 100;
    p.loop_to   = 120;                   /* a twenty-sample loop */
    bm_player_locate(&p, 110);
    bm_player_play(&p);

    /* Thirty-two samples out of a twenty-sample loop: the block has to wrap
     * inside itself, more than once. */
    real = bm_player_read(&p, out, 32);
    check(real == 32, "every sample of the block is real audio");

    check(out[0] == 110.0f && out[9] == 119.0f, "it plays to the loop end");
    check(out[10] == 100.0f, "and comes straight back to the loop start");
    check(out[29] == 119.0f && out[30] == 100.0f, "twice over in one block");

    for (i = 1; i < 32; i++) {
        if (out[i] < out[i - 1]) { wrapped_at = (int)i; break; }
    }
    check(wrapped_at == 10, "the wrap is where the loop says and not elsewhere");

    /* No zeros anywhere: a block that zero-filled its tail instead of wrapping
     * would click once per pass. */
    for (i = 0; i < 32; i++) if (out[i] == 0.0f) break;
    check(i == 32, "and nothing is padded with silence");

    check(p.playing == 1, "a looping transport does not stop at the end");
}

static void test_a_loop_that_was_never_set(void)
{
    bm_player p;
    float     out[8];

    printf("a loop with no points\n");

    bm_player_init(&p);
    bm_player_set_source(&p, source, 1000, 1000u);
    p.loop = 1;                          /* but no from/to */
    bm_player_locate(&p, 996);
    bm_player_play(&p);

    (void)bm_player_read(&p, out, 8);
    check(out[3] == 999.0f && out[4] == 0.0f,
          "it loops the whole score, back to the very start");
    check(p.playing == 1, "and keeps going");
}

static void test_a_source_that_is_not_there(void)
{
    bm_player p;
    float     out[8];

    printf("nothing to play\n");

    bm_player_init(&p);
    memset(out, 7, sizeof out);
    check(bm_player_read(&p, out, 8) == 0, "no source is not a crash");
    check(out[0] == 0.0f, "and the block is cleared rather than left alone");

    bm_player_play(&p);
    check(p.playing == 0, "play with nothing loaded does nothing");

    bm_player_set_source(&p, source, 0, 1000u);
    bm_player_play(&p);
    check(p.playing == 0, "and neither does an empty score");
}

/* ------------------------------------------------------------------ */

static void test_render(void)
{
    bm_render r;
    bm_config cfg;
    char      err[192];

    printf("rendering a score\n");

    memset(&r, 0, sizeof r);
    bm_config_default(&cfg);
    cfg.sample_rate = 22050u;

    check(bm_render_score(&r, "[dur 500][note C4] M IY1", &cfg,
                          err, sizeof err) == 0, "a score renders");
    printf("    %lu samples, %.0f ms\n", (unsigned long)r.len, bm_render_ms(&r));
    /* 500 ms of note plus the engine's ring-out. */
    check(bm_render_ms(&r) > 550.0 && bm_render_ms(&r) < 650.0,
          "to about the length it says it is");

    {
        size_t was = r.len;
        check(bm_render_score(&r, "[dur 500][note C4] M IY1 "
                                  "[dur 500][note E4] M IY1", &cfg,
                              err, sizeof err) == 0, "and again, longer");
        check(r.len > was, "into the same buffer, grown");
    }

    /* A score with a bad phoneme in it must not leave half a song behind. */
    check(bm_render_score(&r, "[dur 500][note C4] WOBBLE", &cfg,
                          err, sizeof err) == -1, "a bad score is refused");
    check(r.len == 0, "and leaves nothing half-rendered");
    printf("    refused with: %s\n", err);

    check(bm_render_score(&r, "", &cfg, err, sizeof err) == 0 && r.len == 0,
          "an empty score is empty, not an error");

    bm_render_free(&r);
    check(r.pcm == 0, "freeing clears the handle");
}

/* Render, then play: the two halves against each other. */
static void test_render_then_play(void)
{
    bm_render r;
    bm_player p;
    bm_config cfg;
    char      err[192];
    float     out[512];
    size_t    real, total = 0;

    printf("rendered, then played\n");

    memset(&r, 0, sizeof r);
    bm_config_default(&cfg);
    bm_player_init(&p);

    check(bm_render_score(&r, "[dur 500][note C4] M IY1 [dur 500][note E4] M IY1",
                          &cfg, err, sizeof err) == 0, "renders");

    bm_player_set_source(&p, r.pcm, r.len, r.rate);
    bm_player_seek_ms(&p, 500.0);
    bm_player_play(&p);

    while (p.playing) {
        real = bm_player_read(&p, out, 512);
        total += real;
    }
    printf("    %.0f ms rendered, %.0f ms played from half way\n",
           bm_render_ms(&r), 1000.0 * (double)total / (double)r.rate);
    check(total > 0 && total < r.len, "playing from the middle plays the rest");
    check(total >= r.len - (size_t)(0.5 * r.rate) - 8 &&
          total <= r.len - (size_t)(0.5 * r.rate) + 8,
          "which is exactly what was left after the seek");

    bm_render_free(&r);
}

int main(void)
{
    printf("\nBENCmouth render and transport tests\n\n");
    fill_source();
    test_plays_from_the_beginning();
    test_seeking();
    test_the_end();
    test_a_block_across_the_loop_point();
    test_a_loop_that_was_never_set();
    test_a_source_that_is_not_there();
    test_render();
    test_render_then_play();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
