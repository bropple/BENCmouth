/*
 * BENCmouth - prosody tests
 *
 * Prosody is the one part of this synthesizer where "sounds right" is the real
 * acceptance criterion, and that makes it the part most in need of assertions
 * that do not depend on anyone's ear. Everything here checks a numeric property
 * of the planned contour: which direction it moves, where the peaks land,
 * whether phrases are independent of each other.
 */

#include "bm_frames.h"
#include "bm_prosody.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* Plans a contour for a phoneme string and reports it. */
typedef struct {
    bm_frame_gen g;
    float f0[BM_MAX_PHONEMES];
    float dur[BM_MAX_PHONEMES];
    int   count;
} plan;

static int make_plan(plan *p, const char *phonemes, const bm_voice *v)
{
    bm_frame_gen_init(&p->g, 100.0f, v);
    if (bm_frame_gen_set_phonemes(&p->g, phonemes, 0) != BM_OK) return 0;
    bm_prosody_plan(p->g.seq, p->g.stress, p->g.count, v, p->f0, p->dur);
    p->count = p->g.count;
    return 1;
}

static float last_voiced_f0(const plan *p)
{
    int i;
    for (i = p->count - 1; i >= 0; i--) {
        if (p->g.seq[i]->cls != BM_CLS_SILENCE) return p->f0[i];
    }
    return 0.0f;
}

/* ------------------------------------------------------------------ */

static void test_boundary_tones(void)
{
    bm_voice v;
    plan st, qu, cm;
    float base;

    printf("boundary tones\n");

    bm_voice_default(&v);
    base = v.f0_base;

    check(make_plan(&st, "HH AH0 L OW1 D EH1 R .", &v) &&
          make_plan(&qu, "HH AH0 L OW1 D EH1 R ?", &v) &&
          make_plan(&cm, "HH AH0 L OW1 D EH1 R ,", &v), "all three plans build");

    printf("    statement ends at %.0f Hz, question at %.0f, comma at %.0f (base %.0f)\n",
           (double)last_voiced_f0(&st), (double)last_voiced_f0(&qu),
           (double)last_voiced_f0(&cm), (double)base);

    check(last_voiced_f0(&st) < base * 0.85f,
          "a full stop falls well below base pitch");
    check(last_voiced_f0(&qu) > base * 1.15f,
          "a question rises well above it");
    check(last_voiced_f0(&cm) > last_voiced_f0(&st) &&
          last_voiced_f0(&cm) < last_voiced_f0(&qu),
          "a comma sits between the two: unfinished, but not a question");
}

static void test_phrases_are_independent(void)
{
    bm_voice v;
    plan p;
    int i, second_start = -1;
    float first_peak = 0.0f, second_peak = 0.0f;

    printf("phrase segmentation\n");

    bm_voice_default(&v);
    check(make_plan(&p, "W AH1 N T UW1 . TH R IY1 F AO1 R .", &v), "two phrases plan");

    for (i = 0; i < p.count; i++) {
        if (p.g.seq[i]->cls == BM_CLS_SILENCE) { second_start = i + 1; break; }
    }
    check(second_start > 0, "the boundary is found");

    for (i = 0; i < second_start - 1; i++)
        if (p.f0[i] > first_peak) first_peak = p.f0[i];
    for (i = second_start; i < p.count; i++)
        if (p.f0[i] > second_peak) second_peak = p.f0[i];

    printf("    first phrase peaks at %.0f Hz, second at %.0f\n",
           (double)first_peak, (double)second_peak);

    /* The whole point of segmenting: the second phrase resets to its own
     * starting pitch instead of continuing the first one downhill. Before this,
     * a paragraph declined steadily into the floor. */
    check(second_peak > first_peak * 0.9f,
          "the second phrase starts fresh rather than continuing the decline");
}

static void test_accents(void)
{
    bm_voice v;
    plan p;
    int i;
    float stressed = 0.0f, unstressed = 0.0f;

    printf("pitch accents\n");

    bm_voice_default(&v);
    check(make_plan(&p, "AH0 B AH0 V AH1 B AH0 V AH0 .", &v), "plan builds");

    for (i = 0; i < p.count; i++) {
        if (p.g.seq[i]->cls != BM_CLS_VOWEL) continue;
        if (p.g.stress[i] == 1u) stressed = p.f0[i];
        else if (unstressed == 0.0f) unstressed = p.f0[i];
    }
    printf("    stressed vowel %.0f Hz, unstressed %.0f\n",
           (double)stressed, (double)unstressed);
    check(stressed > unstressed * 1.05f, "a stressed vowel is pitched above an unstressed one");
}

static void test_final_lengthening(void)
{
    bm_voice v;
    plan p;
    int i, last_vowel = -1;

    printf("phrase-final lengthening\n");

    bm_voice_default(&v);
    check(make_plan(&p, "W AH1 N T UW1 TH R IY1 .", &v), "plan builds");

    for (i = 0; i < p.count; i++) {
        if (p.g.seq[i]->cls == BM_CLS_VOWEL || p.g.seq[i]->cls == BM_CLS_DIPHTHONG)
            last_vowel = i;
    }
    check(last_vowel >= 0 && p.dur[last_vowel] > 1.1f,
          "the last vowel before a boundary is lengthened");
    printf("    last vowel duration multiplier %.2f\n", (double)p.dur[last_vowel]);
}

static void test_off_is_off(void)
{
    bm_voice v;
    plan p;
    int i, flat = 1;

    printf("prosody off\n");

    v = *bm_voice_preset("retro");
    check(v.prosody == 0.0f, "Retro has prosody off");

    check(make_plan(&p, "HH AH0 L OW1 D EH1 R ?", &v), "plan builds anyway");
    for (i = 0; i < p.count; i++) {
        if (fabs((double)(p.f0[i] - v.f0_base)) > 0.01) flat = 0;
        if (p.dur[i] != 1.0f) flat = 0;
    }
    check(flat, "the plan is flat, so the older contour governs unchanged");
}

static void test_range_scales_it(void)
{
    bm_voice narrow, wide;
    plan pn, pw;
    float base;

    printf("f0_range\n");

    bm_voice_default(&narrow);
    base = narrow.f0_base;
    wide = narrow;
    narrow.f0_range = 1.0f;
    wide.f0_range = 8.0f;

    check(make_plan(&pn, "HH AH0 L OW1 D EH1 R ?", &narrow) &&
          make_plan(&pw, "HH AH0 L OW1 D EH1 R ?", &wide), "both plan");

    printf("    range 1 st ends at %.0f Hz, range 8 st at %.0f (base %.0f)\n",
           (double)last_voiced_f0(&pn), (double)last_voiced_f0(&pw), (double)base);

    /* f0_range was a documented voice parameter that nothing read until
     * prosody existed. This is the assertion that it is wired up. */
    check(last_voiced_f0(&pw) > last_voiced_f0(&pn) * 1.3f,
          "a wider f0_range produces a bigger excursion");
}

int main(void)
{
    printf("\nBENCmouth prosody tests\n\n");
    test_boundary_tones();
    test_phrases_are_independent();
    test_accents();
    test_final_lengthening();
    test_off_is_off();
    test_range_scales_it();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
