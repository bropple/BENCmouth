/*
 * BENCmouth - phoneme inventory and frame generator tests
 */

#include "bm_frames.h"
#include "bm_phonemes.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void test_lookup(void)
{
    const bm_phoneme *p;

    printf("lookup\n");

    p = bm_phoneme_lookup("AA", 0);
    check(p != 0 && strcmp(p->name, "AA") == 0, "plain name resolves");

    /* CMUdict writes stress as a trailing digit; it is not part of identity. */
    check(bm_phoneme_lookup("AA1", 0) == bm_phoneme_lookup("AA", 0),
          "stress digit is ignored");
    check(bm_phoneme_lookup("ah0", 0) == bm_phoneme_lookup("AH", 0),
          "lookup is case-insensitive");

    /* A prefix must not match a longer entry, or "N" would silently resolve
     * to "NG" and every /n/ in the language would come out nasalized wrong. */
    check(bm_phoneme_lookup("N", 0) != bm_phoneme_lookup("NG", 0),
          "N and NG are distinct");
    check(bm_phoneme_lookup("S", 0) != bm_phoneme_lookup("SH", 0),
          "S and SH are distinct");

    check(bm_phoneme_lookup("QQ", 0) == 0, "unknown phoneme returns NULL");
    check(bm_phoneme_lookup("", 0) == 0, "empty name returns NULL");
    check(bm_phoneme_silence() != 0, "silence always exists");
}

static void test_inventory(void)
{
    int i, n = bm_phoneme_count();
    int bad_dur = 0, bad_freq = 0, bad_amp = 0, unreachable = 0;

    printf("inventory (%d entries)\n", n);

    for (i = 0; i < n; i++) {
        const bm_phoneme *p = bm_phoneme_at(i);
        int j;

        if (p->steady_ms == 0u || p->transition_ms == 0u) bad_dur++;

        for (j = 0; j < BM_PH_NTARGETS; j++) {
            if (p->freq[j] < 100.0f || p->freq[j] > 4000.0f) bad_freq++;
            if (p->bw[j] < 20.0f || p->bw[j] > 500.0f) bad_freq++;
            if (p->freq_end[j] < 100.0f || p->freq_end[j] > 4000.0f) bad_freq++;
        }

        if (p->av > 60.0f || p->ah > 60.0f || p->af > 60.0f) bad_amp++;

        /* Every non-silent phoneme must actually make a sound through some
         * branch, or it is a hole in the inventory that will only show up as a
         * word with a missing consonant. */
        if (p->cls != BM_CLS_SILENCE) {
            int audible = (p->av > 0.0f) || (p->ah > 0.0f) || (p->af > 0.0f);
            for (j = 0; j < BM_NFORMANTS; j++) {
                if (p->burst_amp[j] > 0.0f) audible = 1;
            }
            if (!audible) { unreachable++; printf("      silent: %s\n", p->name); }
        }

        /* Formant ordering: F1 < F2 < F3 is not a stylistic choice, it is what
         * makes a vocal tract a vocal tract. */
        if (p->freq[0] >= p->freq[1] || p->freq[1] >= p->freq[2]) {
            printf("      formants out of order: %s\n", p->name);
            bad_freq++;
        }
    }

    check(bad_dur == 0, "every phoneme has nonzero durations");
    check(bad_freq == 0, "formants in range and correctly ordered");
    check(bad_amp == 0, "no amplitude exceeds full scale");
    check(unreachable == 0, "every non-silent phoneme is audible");
}

static void test_frame_gen(void)
{
    bm_frame_gen g;
    bm_voice     v;
    bm_frame     f;
    int          count = 0, bad = 0;

    printf("frame generator\n");

    bm_voice_default(&v);
    bm_frame_gen_init(&g, 100.0f, &v);

    check(bm_frame_gen_set_phonemes(&g, "HH AH0 L OW1", 0) == BM_OK,
          "parses a valid phoneme string");
    check(bm_frame_gen_set_phonemes(&g, "HH XX L OW1", 0) == BM_ERR_UNSUPPORTED,
          "rejects an unknown phoneme rather than skipping it");

    (void)bm_frame_gen_set_phonemes(&g, "HH AH0 L OW1", 0);
    while (bm_frame_gen_next(&g, &f)) {
        int i;
        count++;
        if (!(f.f0 == f.f0) || f.f0 < 20.0f || f.f0 > 500.0f) bad++;
        for (i = 0; i < BM_NFORMANTS; i++) {
            if (!(f.freq[i] == f.freq[i]) || f.freq[i] <= 0.0f) bad++;
            if (!(f.bw[i] == f.bw[i]) || f.bw[i] <= 0.0f) bad++;
        }
        if (count > 100000) break;
    }

    printf("      emitted %d frames, predicted %d\n", count, bm_frame_gen_length(&g));
    check(count == bm_frame_gen_length(&g), "emitted frame count matches prediction");
    check(bad == 0, "no NaN or out-of-range values in any frame");
    check(count > 30 && count < 300, "a four-phoneme word is a plausible length");
}

static void test_voice_affects_output(void)
{
    bm_frame_gen g;
    bm_voice     v;
    bm_frame     f;
    int          slow_frames, fast_frames;

    printf("voice parameters\n");

    bm_voice_default(&v);
    v.speed = 1.0f;
    bm_frame_gen_init(&g, 100.0f, &v);
    (void)bm_frame_gen_set_phonemes(&g, "HH AH0 L OW1", 0);
    slow_frames = bm_frame_gen_length(&g);

    v.speed = 2.0f;
    bm_frame_gen_init(&g, 100.0f, &v);
    (void)bm_frame_gen_set_phonemes(&g, "HH AH0 L OW1", 0);
    fast_frames = bm_frame_gen_length(&g);

    printf("      speed 1.0: %d frames, speed 2.0: %d frames\n",
           slow_frames, fast_frames);
    check(fast_frames < slow_frames, "higher speed produces fewer frames");

    /* throat is the vocal-tract-length knob for F1, so it must move F1.
     * Sample the last frame rather than the first: the first frame of any
     * utterance is the start of a transition and therefore still sitting at the
     * rest position, which says nothing about the phoneme's target. */
    v.speed = 1.0f;
    v.throat = 1.0f;
    v.mouth = 1.0f;
    bm_frame_gen_init(&g, 100.0f, &v);
    (void)bm_frame_gen_set_phonemes(&g, "AA1", 0);
    while (bm_frame_gen_next(&g, &f)) { /* run to the end */ }
    {
        float f1_normal = f.freq[0];
        float f3_normal = f.freq[2];

        v.throat = 1.2f;
        bm_frame_gen_init(&g, 100.0f, &v);
        (void)bm_frame_gen_set_phonemes(&g, "AA1", 0);
        while (bm_frame_gen_next(&g, &f)) { }
        printf("      /a/ throat 1.0 -> 1.2: F1 %.0f -> %.0f Hz, F3 %.0f -> %.0f Hz\n",
               (double)f1_normal, (double)f.freq[0],
               (double)f3_normal, (double)f.freq[2]);
        check(f.freq[0] > f1_normal * 1.15f, "throat shifts F1 up");
        /* The two axes must stay separable, or they are one knob wearing two
         * names and we have lost the range the split was for. */
        check(f.freq[2] < f3_normal * 1.05f, "throat barely touches F3");
    }
}

int main(void)
{
    printf("\nBENCmouth phoneme tests\n\n");
    test_lookup();
    test_inventory();
    test_frame_gen();
    test_voice_affects_output();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
