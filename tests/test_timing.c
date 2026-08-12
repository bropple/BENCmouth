/*
 * BENCmouth - [dur] and bm_measure tests
 *
 * Two properties are worth more than the rest of this file put together.
 *
 * The first is that [dur N] lasts N milliseconds whatever phonemes are in it.
 * That is the whole point of it: [hold] sets the vowel and lets the consonants
 * add their own time on top, which is right for a written score and wrong for
 * anything drawn on a grid, because three notes of the same written length come
 * out at three different real lengths and the error accumulates.
 *
 * The second is that bm_measure() agrees with the audio. A timeline that is
 * approximately right is worse than none: it looks authoritative and drifts.
 * So the last test renders through the public API and compares sample counts
 * against what was predicted before a sample existed.
 */

#include "bencmouth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* One engine's worth of scratch, reused by every measurement here. Static
 * rather than automatic because it is the best part of a hundred kilobytes and
 * this is exactly the reason bm_measure takes it as a parameter. */
static bm_engine_storage scratch;

static uint32_t measure(const char *phonemes, bm_result *rc_out)
{
    uint32_t  total = 0;
    bm_result rc = bm_measure(&scratch, 0, phonemes, 0, 0, 0, 0, &total);

    if (rc_out != 0) *rc_out = rc;
    return (rc == BM_OK) ? total : 0u;
}

/* Sums the spans belonging to one [dur] group, which is what an editor drawing
 * a note has to do. */
static uint32_t group_ms(const bm_span *s, size_t n, uint16_t group)
{
    uint32_t ms = 0;
    size_t   i;

    for (i = 0; i < n; i++) {
        if (s[i].group == group) ms += s[i].length_ms;
    }
    return ms;
}

/* ------------------------------------------------------------------ */

/* The measurement that started this: same written length, three real ones. */
static void test_hold_does_not_fit_a_grid(void)
{
    uint32_t bare, one, cluster;

    printf("[hold] is vowel-only, and so cannot be a grid\n");

    bare    = measure("[hold 400][note C4] IY1", 0);
    one     = measure("[hold 400][note C4] M IY1", 0);
    cluster = measure("[hold 400][note C4] S T R EY1 T", 0);

    printf("    IY1 %u ms, M IY1 %u ms, S T R EY1 T %u ms - all written 400\n",
           bare, one, cluster);
    check(bare > 0u && one > bare && cluster > one,
          "three notes written 400 come out at three lengths");
    check(cluster > bare + 300u, "and the spread is most of a note");
}

static void test_dur_is_the_length_it_says(void)
{
    uint32_t bare, one, cluster, longer;
    bm_result rc;

    printf("[dur] is the length it says\n");

    bare    = measure("[dur 400][note C4] IY1", &rc);
    one     = measure("[dur 400][note C4] M IY1", &rc);
    cluster = measure("[dur 400][note C4] S T R EY1 T", &rc);

    printf("    IY1 %u ms, M IY1 %u ms, S T R EY1 T %u ms - all written 400\n",
           bare, one, cluster);

    /* Within a frame: the frame rate is 100 Hz by default, so 10 ms is the
     * finest anything here can be, and that is reported rather than hidden. */
    check(bare >= 390u && bare <= 410u, "a bare vowel lands on 400");
    check(one >= 390u && one <= 410u, "one consonant in front, still 400");
    check(cluster >= 390u && cluster <= 410u, "a four-consonant note, still 400");

    longer = measure("[dur 800][note C4] S T R EY1 T", &rc);
    check(longer >= 790u && longer <= 810u, "and 800 when 800 is asked for");
}

/* A consonant cluster has a floor: the cues to which stop you heard live in
 * the transitions and cannot be scaled away. Asking for less than that is a
 * request that cannot be met, and the answer is to say so. */
static void test_impossible_lengths_are_reported_not_hidden(void)
{
    uint32_t got;
    bm_result rc;

    printf("a note too short for its consonants overruns, and says so\n");

    got = measure("[dur 20][note C4] S T R EY1 T", &rc);
    printf("    asked 20 ms, sounds for %u ms\n", got);
    check(rc == BM_OK, "it still sings");
    check(got > 20u, "and the measurement admits the overrun");

    /* The clamp is not a wall: a longer request still gets longer. */
    check(measure("[dur 600][note C4] S T R EY1 T", &rc) >
          measure("[dur 500][note C4] S T R EY1 T", &rc),
          "beyond the floor, more time asked for is more time given");
}

/* A note can hold more than one vowel - a lyric of two syllables sung on one
 * pitch - and the budget is then shared between them in proportion to what they
 * would naturally have been. The case worth testing is a shared note that is
 * also too short for its consonants, because the sharing and the compression
 * are measured from the same numbers only if both are taken after the squeeze.
 */
static void test_a_note_with_two_vowels(void)
{
    bm_span  spans[64];
    size_t   n = 0, i;
    uint32_t total = 0;
    uint32_t first = 0, second = 0;
    int      seen = 0;
    bm_result rc;

    printf("a note with two vowels in it\n");

    rc = bm_measure(&scratch, 0, "[dur 700][note C4] M IY1 Y UW1", 0,
                    spans, sizeof spans / sizeof spans[0], &n, &total);
    check(rc == BM_OK, "it measures");
    check(total >= 690u && total <= 710u, "and lasts the 700 ms it asked for");

    for (i = 0; i < n; i++) {
        if (!spans[i].vowel) continue;
        if (seen++ == 0) first = spans[i].length_ms;
        else             second = spans[i].length_ms;
    }
    printf("    IY1 %u ms, UW1 %u ms\n", first, second);
    check(seen == 2, "both vowels are there");
    check(first > 0u && second > 0u, "and both got a share of the note");

    /* Squeezed as well as shared. */
    rc = bm_measure(&scratch, 0, "[dur 260][note C4] S T R IY1 M Y UW1", 0,
                    spans, sizeof spans / sizeof spans[0], &n, &total);
    check(rc == BM_OK, "a two-vowel note too short for its consonants measures");
    seen = 0;
    for (i = 0; i < n; i++) {
        if (spans[i].vowel && spans[i].length_ms > 0u) seen++;
    }
    check(seen == 2, "and neither vowel is squeezed out of existence");
    printf("    asked 260 ms, sounds for %u ms\n", total);
}

static void test_groups(void)
{
    bm_span  spans[64];
    size_t   n = 0;
    uint32_t total = 0;
    bm_result rc;

    printf("groups\n");

    /* Two notes of the same length in a row. If groups were found by looking
     * for equal [dur] values rather than carried as ids, these two would merge
     * into one 400 ms note and every melody would be half its length. */
    rc = bm_measure(&scratch, 0,
                    "[dur 400][note C4] M IY1 [dur 400][note D4] M IY1", 0,
                    spans, sizeof spans / sizeof spans[0], &n, &total);
    check(rc == BM_OK, "two notes of equal length measure");
    check(group_ms(spans, n, 1u) >= 390u && group_ms(spans, n, 1u) <= 410u,
          "the first note is 400 ms");
    check(group_ms(spans, n, 2u) >= 390u && group_ms(spans, n, 2u) <= 410u,
          "the second is its own note, also 400 ms");
    check(total >= 790u && total <= 810u, "and the pair is 800, not 400");

    /* A rest ends the note before it rather than being folded into it. */
    rc = bm_measure(&scratch, 0, "[dur 400] M IY1 [pause 200] M IY1", 0,
                    spans, sizeof spans / sizeof spans[0], &n, &total);
    check(rc == BM_OK, "a rest inside a score measures");
    {
        size_t i;
        int    after = 0;
        for (i = 0; i < n; i++) if (spans[i].group == 0u) after++;
        check(after > 0, "[pause] and what follows it are outside the group");
    }
}

static void test_spans_describe_the_utterance(void)
{
    bm_span  spans[64];
    size_t   n = 0, i;
    uint32_t total = 0, sum = 0;
    const char *score = "[dur 400][note C4] M IY1";
    bm_result rc;

    printf("spans\n");

    rc = bm_measure(&scratch, 0, score, 0, spans,
                    sizeof spans / sizeof spans[0], &n, &total);
    check(rc == BM_OK && n == 2, "two phonemes measured");

    for (i = 0; i < n; i++) {
        printf("    %2u..%2u ms  src %2u  group %u  %s\n",
               spans[i].start_ms, spans[i].start_ms + spans[i].length_ms,
               spans[i].source, spans[i].group,
               spans[i].vowel ? "vowel" : "consonant");
    }

    check(spans[0].start_ms == 0u, "the first phoneme starts at zero");
    for (i = 1; i < n; i++) {
        if (spans[i].start_ms != spans[i - 1].start_ms + spans[i - 1].length_ms) {
            break;
        }
    }
    check(i == n, "spans abut exactly - no gaps, no overlaps");

    for (i = 0; i < n; i++) sum += spans[i].length_ms;
    check(sum == total, "and they add up to the total");

    check(spans[0].vowel == 0u && spans[1].vowel == 1u,
          "the vowel is marked, and it is the one carrying the length");
    check(memcmp(score + spans[0].source, "M", 1) == 0 &&
          memcmp(score + spans[1].source, "IY1", 3) == 0,
          "source offsets point at the tokens that produced them");
}

static void test_api_edges(void)
{
    bm_span  spans[2];
    size_t   n = 0;
    uint32_t total = 0;
    bm_result rc;

    printf("the API's edges\n");

    check(bm_measure(0, 0, "M IY1", 0, 0, 0, 0, &total) == BM_ERR_ARG,
          "no scratch is an error, not a crash");
    check(bm_measure(&scratch, 0, 0, 0, 0, 0, 0, &total) == BM_ERR_ARG,
          "no input is an error");

    (void)measure("M IY1 [wobble 3]", &rc);
    check(rc == BM_ERR_UNSUPPORTED, "a bad command reports what it would when sung");
    (void)measure("[dur 0] M IY1", &rc);
    check(rc == BM_ERR_ARG, "a zero-length note is rejected");
    (void)measure("[dur 99999] M IY1", &rc);
    check(rc == BM_ERR_ARG, "and one longer than the format allows");

    /* Too small a buffer must not look like a short utterance. */
    rc = bm_measure(&scratch, 0, "M IY1 M IY1 M IY1", 0, spans, 2, &n, &total);
    check(rc == BM_ERR_OVERFLOW, "a buffer that is too small says so");
    check(n == 6, "and reports the size it needed");
    check(total > 0u, "the total is still answered");
}

/* The one that matters: does the prediction match the sound? */
static void test_measurement_matches_the_audio(void)
{
    static float      buf[400000];
    bm_engine_storage storage;
    bm_engine        *e = 0;
    bm_config         cfg;
    const char       *scores[] = {
        "[dur 400][note C4] M IY1",
        "[dur 400][note C4] S T R EY1 T",
        "[dur 300][note C4] D EY1 [dur 300][note B3] Z IY0 [pause 200]"
        "[dur 600][note G3] M IY1"
    };
    size_t s;

    printf("measurement against the audio it predicts\n");

    bm_config_default(&cfg);
    cfg.markup = 1;

    for (s = 0; s < sizeof scores / sizeof scores[0]; s++) {
        uint32_t predicted = 0;
        size_t   got = 0, n;
        double   rendered_ms, tail_ms;

        check(bm_measure(&scratch, &cfg, scores[s], 0, 0, 0, 0, &predicted)
              == BM_OK, "the score measures");

        if (bm_engine_init(&storage, &cfg, &e) != BM_OK) {
            check(0, "engine init");
            return;
        }
        if (bm_speak_phonemes(e, scores[s], 0) != BM_OK) {
            check(0, "the same score sings");
            return;
        }
        while ((n = bm_read(e, buf + got,
                            (sizeof buf / sizeof buf[0]) - got)) > 0) {
            got += n;
        }

        rendered_ms = 1000.0 * (double)got / (double)cfg.sample_rate;
        /* The engine rings the filters out after the last frame - a real part
         * of the sound, and deliberately not part of the score's timeline. It
         * is the whole of the difference, which is what this asserts. */
        tail_ms = rendered_ms - (double)predicted;

        printf("    predicted %4u ms, rendered %7.1f ms, difference %5.1f ms\n",
               predicted, rendered_ms, tail_ms);
        check(tail_ms > 80.0 && tail_ms < 120.0,
              "the audio is the predicted length plus the ring-out, nothing else");
    }
}

int main(void)
{
    printf("\nBENCmouth timing tests\n\n");
    test_hold_does_not_fit_a_grid();
    test_dur_is_the_length_it_says();
    test_impossible_lengths_are_reported_not_hidden();
    test_a_note_with_two_vowels();
    test_groups();
    test_spans_describe_the_utterance();
    test_api_edges();
    test_measurement_matches_the_audio();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
