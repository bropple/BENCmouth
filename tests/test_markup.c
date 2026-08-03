/*
 * BENCmouth - inline markup tests
 *
 * The property that matters most here is the one about being off: with markup
 * disabled, brackets are ordinary characters and "[pitch 70]" is spoken as the
 * words "pitch seventy". A caller who never asked for markup must never have
 * text silently swallowed, so that case is tested first and hardest.
 */

#include "bm_frames.h"
#include "bm_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static bm_result to_phonemes(const char *text, unsigned flags, char *out, size_t cap)
{
    size_t n = 0;
    return bm_text_to_phonemes_ex(text, 0, out, cap, &n, flags);
}

/* Frames the sequence produces, and the F0 of the last frame. */
static int measure(const char *phonemes, float *last_f0, bm_result *rc)
{
    bm_frame_gen g;
    bm_voice     v;
    bm_frame     f;
    int          count = 0;

    bm_voice_default(&v);
    bm_frame_gen_init(&g, 100.0f, &v);

    *rc = bm_frame_gen_set_phonemes(&g, phonemes, 0);
    if (*rc != BM_OK) return 0;

    while (bm_frame_gen_next(&g, &f)) { count++; if (last_f0) *last_f0 = f.f0; }
    return count;
}

/* ------------------------------------------------------------------ */

static void test_off_by_default(void)
{
    char out[1024];

    printf("markup off\n");

    check(to_phonemes("hello [pitch 70] world", 0u, out, sizeof out) == BM_OK,
          "text with brackets still converts");

    /* "pitch" and "seventy" must appear as speech, not vanish. Matched on a
     * fragment rather than the whole word: the rules render "seventy" as
     * S IY V EH N T IH, which is not quite right, and pinning the exact string
     * would turn a future dictionary fix into a test failure. */
    check(strstr(out, "P IH T CH") != 0, "the word \"pitch\" is spoken");
    check(strstr(out, "V EH N T") != 0, "the number is spoken");
    check(strstr(out, "[") == 0, "no bracket survives into the phoneme stream");
    printf("    %s\n", out);
}

static void test_on_passes_through(void)
{
    char out[1024];

    printf("markup on\n");

    check(to_phonemes("hello [pitch 70] world", BM_TEXT_MARKUP, out, sizeof out) == BM_OK,
          "text with markup converts");
    check(strstr(out, "[pitch 70]") != 0, "the command survives verbatim");
    check(strstr(out, "P IH T CH") == 0, "and is not also spoken as words");
    printf("    %s\n", out);
}

static void test_effects(void)
{
    bm_result rc;
    float     f0_plain = 0.0f, f0_high = 0.0f;
    int       plain, paused, fast, slow;

    printf("effects on the frame stream\n");

    plain  = measure("W AH1 N T UW1 TH R IY1", 0, &rc);
    paused = measure("W AH1 N [pause 500] T UW1 TH R IY1", 0, &rc);
    check(paused > plain + 40, "[pause 500] adds about 50 frames");
    printf("    plain %d frames, with a 500 ms pause %d\n", plain, paused);

    fast = measure("[speed 2.0] W AH1 N T UW1 TH R IY1", 0, &rc);
    slow = measure("[speed 0.5] W AH1 N T UW1 TH R IY1", 0, &rc);
    check(fast < plain && slow > plain, "[speed] scales duration both ways");
    printf("    speed 2.0 %d frames, 1.0 %d, 0.5 %d\n", fast, plain, slow);

    (void)measure("W AH1 N T UW1 TH R IY1", &f0_plain, &rc);
    (void)measure("W AH1 N [pitch 200] T UW1 TH R IY1", &f0_high, &rc);
    check(f0_high > f0_plain * 1.4f, "[pitch] raises F0 for later phonemes");
    printf("    final F0 %.0f Hz plain, %.0f Hz after [pitch 200]\n",
           (double)f0_plain, (double)f0_high);

    /* [reset] has to actually undo, or the command is decorative. */
    {
        float f0_reset = 0.0f;
        (void)measure("W AH1 N [pitch 200] T UW1 [reset] TH R IY1", &f0_reset, &rc);
        check(fabs((double)(f0_reset - f0_plain)) < 1.0,
              "[reset] restores the voice's own pitch");
    }
}

static void test_errors(void)
{
    bm_result rc;

    printf("errors are reported, not guessed at\n");

    (void)measure("W AH1 N [pitch] T UW1", 0, &rc);
    check(rc == BM_ERR_ARG, "missing argument is rejected");

    (void)measure("W AH1 N [wobble 3] T UW1", 0, &rc);
    check(rc == BM_ERR_UNSUPPORTED, "unknown command is rejected");

    (void)measure("W AH1 N [pitch 90", 0, &rc);
    check(rc == BM_ERR_ARG, "unterminated bracket is rejected");

    (void)measure("W AH1 N [pitch 9000] T UW1", 0, &rc);
    check(rc == BM_ERR_ARG, "out-of-range value is rejected");

    (void)measure("W AH1 N [pitch abc] T UW1", 0, &rc);
    check(rc == BM_ERR_ARG, "non-numeric argument is rejected");

    /* A command with no phonemes after it is legal but produces nothing to
     * say; it must not be mistaken for success with content. */
    (void)measure("[pitch 120]", 0, &rc);
    check(rc == BM_ERR_ARG, "a sequence of only commands is empty, not valid");
}

static void test_survives_the_engine(void)
{
    bm_engine_storage storage;
    bm_engine        *e = 0;
    bm_config         cfg;
    static float      buf[200000];
    size_t            plain = 0, marked = 0, n;

    printf("through the public API\n");

    bm_config_default(&cfg);
    cfg.markup = 1;
    check(bm_engine_init(&storage, &cfg, &e) == BM_OK, "engine accepts markup config");

    check(bm_speak_text(e, "one two three", 0) == BM_OK, "plain text speaks");
    while ((n = bm_read(e, buf, 4096)) > 0) plain += n;

    bm_engine_reset(e);
    check(bm_speak_text(e, "one [pause 500] two three", 0) == BM_OK,
          "marked-up text speaks");
    while ((n = bm_read(e, buf, 4096)) > 0) marked += n;

    printf("    %lu samples plain, %lu with a pause\n",
           (unsigned long)plain, (unsigned long)marked);
    check(marked > plain + 10000, "the pause reaches the audio");

    /* And with markup off, the same string is longer still, because the
     * command is spoken aloud as words. */
    {
        size_t spoken = 0;
        bm_config off;
        bm_engine_storage s2;
        bm_engine *e2 = 0;

        bm_config_default(&off);
        off.markup = 0;
        bm_engine_init(&s2, &off, &e2);
        bm_speak_text(e2, "one [pause 500] two three", 0);
        while ((n = bm_read(e2, buf, 4096)) > 0) spoken += n;
        printf("    %lu samples with markup off (command spoken as words)\n",
               (unsigned long)spoken);
        check(spoken > plain, "with markup off the command becomes speech");
    }
}

int main(void)
{
    printf("\nBENCmouth inline markup tests\n\n");
    test_off_by_default();
    test_on_passes_through();
    test_effects();
    test_errors();
    test_survives_the_engine();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
