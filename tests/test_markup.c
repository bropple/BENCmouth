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

    char on[1024];

    printf("markup off\n");

    check(to_phonemes("hello [pitch 70] world", 0u, out, sizeof out) == BM_OK,
          "text with brackets still converts");
    check(to_phonemes("hello [pitch 70] world", BM_TEXT_MARKUP, on, sizeof on) == BM_OK,
          "and so does the same text with markup on");

    /* The property is that the bracketed text becomes speech rather than
     * vanishing - so compare the two conversions rather than asserting literal
     * phonemes. An earlier version of this test pinned "P IH T CH" and broke
     * the moment the dictionary was compiled in, because cmudict spells that
     * word P IH1 CH. Asserting the behaviour instead of one build's output is
     * the difference between a test and a snapshot. */
    check(strstr(out, "[") == 0, "no bracket survives into the phoneme stream");
    check(strlen(out) > strlen(on) + 10,
          "the command is spoken as words, so the output is longer");
    printf("    off: %s\n", out);
    printf("    on : %s\n", on);
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

static void test_singing(void)
{
    bm_result rc;
    float     f0 = 0.0f, f0b = 0.0f;
    int       plain, held;

    printf("singing\n");

    /* A note is an absolute pitch, not a transposition of the speech contour.
     * When [note] shared [pitch]'s behaviour, the prosody planner's accent on a
     * stressed syllable multiplied on top and A4 came out at 525 Hz. */
    (void)measure("[note A4] AA1", &f0, &rc);
    printf("    [note A4] final F0 %.1f Hz (A4 = 440.0)\n", (double)f0);
    check(rc == BM_OK && fabs((double)f0 - 440.0) < 12.0,
          "[note A4] is 440 Hz, not 440 plus an accent");

    (void)measure("[note C3] AA1", &f0b, &rc);
    check(fabs((double)f0b - 130.81) < 5.0, "[note C3] is middle-C-minus-an-octave");

    (void)measure("[note Bb3] AA1", &f0b, &rc);
    check(rc == BM_OK && fabs((double)f0b - 233.08) < 8.0, "flats parse");
    (void)measure("[note A#3] AA1", &f0b, &rc);
    check(rc == BM_OK && fabs((double)f0b - 233.08) < 8.0, "sharps parse, and agree");

    (void)measure("[note H4] AA1", 0, &rc);
    check(rc == BM_ERR_ARG, "a letter that is not a note is rejected");

    /* [hold] lengthens vowels only. Stretching the consonants with them would
     * turn a sung word into a groan. */
    plain = measure("D EY1 Z IY0", 0, &rc);
    held  = measure("[hold 600] D EY1 Z IY0", 0, &rc);
    printf("    \"daisy\" %d frames, held %d\n", plain, held);
    check(held > plain + 60, "[hold] lengthens the note");

    {
        int cons = measure("[hold 600] S S S", 0, &rc);
        int base = measure("S S S", 0, &rc);
        check(cons == base, "[hold] leaves consonants alone");
    }
}

/* Pitch at a given frame, which is what a glide has to be measured in: the
 * question is not where it ends up but how it got there. */
static float f0_at(const char *phonemes, int frame, float prosody)
{
    bm_frame_gen g;
    bm_voice     v;
    bm_frame     f;
    int          i = 0;

    bm_voice_default(&v);
    v.prosody = prosody;
    v.vibrato = 0.0f;
    bm_frame_gen_init(&g, 100.0f, &v);
    if (bm_frame_gen_set_phonemes(&g, phonemes, 0) != BM_OK) return -1.0f;

    while (bm_frame_gen_next(&g, &f)) {
        if (i++ == frame) return f.f0;
    }
    return -1.0f;
}

/* [glide] is the pitch half of legato. The tone half needs no command - two
 * notes on one vowel do not re-articulate - but a note was always reached in a
 * single frame, so a slur stepped. */
static void test_glide(void)
{
    /* C4 for 400 ms and then G4: 262 Hz to 392 Hz, the change at frame 40. */
    const char *step  = "[dur 400][note C4] AA1 [dur 400][note G4] AA1";
    const char *slur  = "[dur 400][note C4] AA1 [glide 60][dur 400][note G4] AA1";
    bm_result rc;

    printf("legato\n");

    check(f0_at(step, 40, 0.0f) > 380.0f,
          "without it a note is reached in one frame");
    check(f0_at(slur, 40, 0.0f) < 300.0f && f0_at(slur, 40, 0.0f) > 262.0f,
          "with it the pitch has only set off");
    check(f0_at(slur, 46, 0.0f) > 385.0f, "and has arrived six frames later");
    printf("    stepped %.0f Hz at the boundary, glided %.0f, then %.0f\n",
           (double)f0_at(step, 40, 0.0f), (double)f0_at(slur, 40, 0.0f),
           (double)f0_at(slur, 46, 0.0f));

    /* Geometric, not linear in hertz: the halfway point of a fifth from 262 is
     * 321 and not 327, which is where the ear puts it. The glide sets off on
     * the frame the note changes, so frame 42 is the third of its six. */
    {
        float mid = f0_at(slur, 42, 0.0f);
        printf("    halfway through: %.0f Hz (geometric 321, linear 327)\n",
               (double)mid);
        check(mid > 310.0f && mid < 325.0f, "and it is geometric on the way");
    }

    /* A longer glide is longer, and is still going where the short one has
     * already arrived. */
    {
        const char *slow = "[dur 400][note C4] AA1 [glide 200][dur 400][note G4] AA1";
        check(f0_at(slow, 46, 0.0f) < f0_at(slur, 46, 0.0f),
              "[glide 200] is still on its way when [glide 60] has landed");
    }

    /* Nothing to glide from: a phrase that scooped up to its own first note
     * out of silence would be an effect nobody asked for. */
    check(f0_at("[glide 200][dur 800][note G4] AA1", 2, 0.0f) > 385.0f,
          "the first note of a phrase is not glided into");

    /* It governs an absolute note whether or not the prosody planner is
     * running, since it is a property of the note and not of the voice. */
    check(f0_at(slur, 40, 0.85f) < 300.0f, "and it works with prosody on too");

    /* Off by default, and off again when asked. */
    check(f0_at("[dur 400][note C4] AA1 [glide 60][dur 400][note G4] AA1 "
                "[glide 0][dur 400][note C4] AA1", 80, 0.0f) < 270.0f,
          "[glide 0] steps again");

    (void)measure("AA1 [glide 3000] AA1", 0, &rc);
    check(rc == BM_ERR_ARG, "a glide longer than any slur is rejected");
    (void)measure("AA1 [glide] AA1", 0, &rc);
    check(rc == BM_ERR_ARG, "and one with no argument at all");
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
    test_singing();
    test_glide();
    test_errors();
    test_survives_the_engine();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
