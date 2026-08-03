/*
 * BENCmouth - voice preset tests, and the BENCmouth Retro golden reference
 *
 * The contract this file enforces: BENCmouth Retro does not drift. Naturalness
 * features arrive as parameters whose off setting reproduces the older
 * behaviour, and Retro leaves them off, so its output should be stable across
 * every future change to the synthesizer.
 *
 * The fingerprint below is deliberately not a checksum. A checksum tells you
 * something changed and nothing else; these measurements tell you *what*
 * changed - overall level, timing, or brightness - which is the difference
 * between a five-minute fix and an afternoon.
 *
 * If a legitimate change to the DSP moves these numbers, that is a decision to
 * make consciously: re-measure, paste the new values, and say so in the commit.
 * Updating them silently is how a pinned voice stops being pinned.
 */

#include "bm_frames.h"
#include "bm_synth.h"

#include <math.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#define FS       22050.0f
#define FRAME_HZ 100.0f
#define NSLICES  8

/* The reference utterance. Long enough to exercise vowels, a diphthong,
 * liquids, a nasal-free stop, and a silence. */
#define REFERENCE_UTTERANCE "HH AH0 L OW1 SIL W ER1 L D"

/* Measured 2026-08-03 from BENCmouth Retro, at the commit where the two-axis
 * throat/mouth voice model landed. That build was verified to render
 * byte-identical WAVs to the ones that established the voice, so these numbers
 * describe the original sound and not merely the current one. */
#define REF_SAMPLES 29040
#define REF_PEAK    0.565104
#define REF_RMS     0.113757
#define REF_ZCR     0.039222
static const double REF_SLICE_RMS[NSLICES] = {
    0.083969, 0.135790, 0.156878, 0.136671,
    0.049084, 0.123383, 0.118283, 0.055871
};

/* Tolerances. Tight enough that a real change trips them, loose enough to
 * survive a different compiler's floating-point scheduling. */
#define TOL_REL   0.02
#define TOL_ABS   0.002

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void check_close(double got, double want, const char *what)
{
    double tol = TOL_ABS + fabs(want) * TOL_REL;
    int    ok = fabs(got - want) <= tol;
    printf("  %-40s %s  got %.6f want %.6f\n",
           what, ok ? "ok  " : "FAIL", got, want);
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ */

static size_t render(const bm_voice *v, const char *phonemes,
                     float *out, size_t cap)
{
    bm_frame_gen gen;
    bm_synth     synth;
    bm_frame     f;
    size_t       spf, pos = 0;

    bm_frame_gen_init(&gen, FRAME_HZ, v);
    if (bm_frame_gen_set_phonemes(&gen, phonemes, 0) != BM_OK) return 0;

    bm_synth_init(&synth, FS);
    bm_synth_set_flutter(&synth, v->f0_flutter);
    bm_synth_set_gain(&synth, v->gain);

    spf = (size_t)(FS / FRAME_HZ);
    while (bm_frame_gen_next(&gen, &f) && pos + spf <= cap) {
        size_t i;
        bm_synth_set_frame(&synth, &f);
        for (i = 0; i < spf; i++) out[pos++] = bm_synth_tick(&synth);
    }
    return pos;
}

static void test_retro_golden(void)
{
    static float buf[200000];
    const bm_voice *retro;
    size_t n, i;
    double peak = 0.0, sumsq = 0.0, zc = 0.0;
    double slice[NSLICES];
    int    s;

    printf("BENCmouth Retro golden reference\n");

    retro = bm_voice_preset("retro");
    if (retro == 0) { check(0, "retro preset exists"); return; }

    n = render(retro, REFERENCE_UTTERANCE, buf, sizeof buf / sizeof buf[0]);
    check(n > 0, "reference utterance renders");
    if (n == 0) return;

    for (i = 0; i < n; i++) {
        double y = (double)buf[i];
        double m = fabs(y);
        if (m > peak) peak = m;
        sumsq += y * y;
        if (i > 0 && ((buf[i] >= 0.0f) != (buf[i - 1] >= 0.0f))) zc += 1.0;
    }

    for (s = 0; s < NSLICES; s++) {
        size_t lo = n * (size_t)s / NSLICES;
        size_t hi = n * (size_t)(s + 1) / NSLICES;
        double acc = 0.0;
        for (i = lo; i < hi; i++) acc += (double)buf[i] * (double)buf[i];
        slice[s] = (hi > lo) ? sqrt(acc / (double)(hi - lo)) : 0.0;
    }

    printf("    measured: samples %lu  peak %.6f  rms %.6f  zcr %.6f\n",
           (unsigned long)n, peak, sqrt(sumsq / (double)n), zc / (double)n);
    printf("    slices:");
    for (s = 0; s < NSLICES; s++) printf(" %.6f", slice[s]);
    printf("\n");

    check((size_t)REF_SAMPLES == n, "sample count unchanged");
    check_close(peak, REF_PEAK, "peak amplitude");
    check_close(sqrt(sumsq / (double)n), REF_RMS, "overall RMS");
    check_close(zc / (double)n, REF_ZCR, "zero-crossing rate (brightness)");

    for (s = 0; s < NSLICES; s++) {
        char label[48];
        sprintf(label, "envelope slice %d/%d", s + 1, NSLICES);
        check_close(slice[s], REF_SLICE_RMS[s], label);
    }
}

static void test_presets(void)
{
    const bm_voice *retro, *v;
    int i, n;

    printf("presets\n");

    n = bm_voice_preset_count();
    printf("    %d presets compiled in:", n);
    for (i = 0; i < n; i++) printf(" \"%s\"", bm_voice_preset_at(i)->name);
    printf("\n");

    retro = bm_voice_preset("BENCmouth Retro");
    check(retro != 0, "full name resolves");
    check(bm_voice_preset("retro") == retro, "bare suffix resolves");
    check(bm_voice_preset("BENCMOUTH-RETRO") == retro, "case and punctuation ignored");
    check(bm_voice_preset("nonexistent") == 0, "unknown preset returns NULL");

    check(retro != 0 && retro->coarticulation == 0.0f,
          "Retro has coarticulation off");
    check(retro != 0 && retro->throat == 1.0f && retro->mouth == 1.0f,
          "Retro has a neutral vocal tract");

    /* The default voice may move as naturalness features land. Retro may not.
     * Asserting they have diverged is the point: it proves a feature was added
     * by creating a preset rather than by editing the pinned one. */
    {
        bm_voice d;
        bm_voice_default(&d);
        check(retro != 0 && d.coarticulation > 0.0f &&
              retro->coarticulation == 0.0f,
              "default has naturalness on, Retro does not");
        check(retro != 0 && d.f0_base == retro->f0_base &&
              d.throat == retro->throat && d.mouth == retro->mouth,
              "default is Retro plus naturalness, not a different speaker");
    }

    /* Presets must actually differ from Retro somewhere, or they are
     * decoration. Compared over the whole parameter block, not a hand-picked
     * subset: the default voice shares Retro's tract and pitch exactly and
     * differs only in a naturalness control, which is the intended shape and
     * must not read as a duplicate. Identity is by pointer so this survives
     * presets being reordered. */
    for (i = 0; i < n; i++) {
        v = bm_voice_preset_at(i);
        if (v == retro) continue;
        if (memcmp(&v->f0_base, &retro->f0_base,
                   sizeof(bm_voice) - sizeof(const char *)) == 0) {
            printf("      preset \"%s\" is identical to Retro\n", v->name);
            failures++;
        }
    }
    check(1, "every preset differs from Retro somewhere");
}

static void test_params(void)
{
    bm_voice v;

    printf("voice parameters\n");

    bm_voice_default(&v);

    check(bm_voice_set_param(&v, "throat", 0, 0.8f) == BM_OK, "sets a known key");
    check(v.throat == 0.8f, "value is applied");
    /* A typo in a hand-edited voice file must be reported, not ignored - a
     * silently dropped setting is a voice that mysteriously sounds wrong. */
    check(bm_voice_set_param(&v, "thrat", 0, 0.8f) == BM_ERR_ARG,
          "unknown key is rejected");
    check(bm_voice_set_param(&v, "COARTICULATION", 0, 0.5f) == BM_OK,
          "keys are case-insensitive");

    /* throat governs F1, mouth governs the top, F2 answers to both. */
    bm_voice_default(&v);
    v.throat = 0.8f;
    check(bm_voice_formant_scale(&v, 0) < 0.85f, "throat moves F1");
    check(bm_voice_formant_scale(&v, 4) > 0.99f, "throat leaves F5 alone");

    bm_voice_default(&v);
    v.mouth = 1.2f;
    check(bm_voice_formant_scale(&v, 0) < 1.01f, "mouth leaves F1 alone");
    check(bm_voice_formant_scale(&v, 4) > 1.15f, "mouth moves F5");
    {
        float f2 = bm_voice_formant_scale(&v, 1);
        check(f2 > 1.05f && f2 < 1.2f, "F2 answers to both axes");
    }
}

/* Every tunable field must be reachable by name, or a voice file cannot express
 * it - and a saved voice would silently drop it on the way back in. This is the
 * test that catches "someone added a field and forgot bm_voice_set_param",
 * which would otherwise show up as a preset that quietly stops surviving a
 * save/load cycle. */
static void test_every_field_has_a_key(void)
{
    static const char *KEYS[] = {
        "f0_base", "f0_range", "f0_flutter", "speed",
        "throat", "mouth",
        "breathiness", "tilt", "open_quotient", "gain",
        "coarticulation", "prosody", "formant_glide", "bandwidth_track"
    };
    const int nkeys = (int)(sizeof KEYS / sizeof KEYS[0]);
    int p, k, mismatched = 0;

    printf("voice serialization\n");

    /* Count the tunable fields by measuring from the first to the last, rather
     * than from sizeof(bm_voice). sizeof includes trailing padding: with ten
     * floats after an eight-byte pointer the struct happened to need none, so
     * the naive calculation was right by luck, and adding an eleventh float
     * introduced four bytes of pad and made it report a field that does not
     * exist. Spanning named members is padding-proof. */
    {
        size_t span = offsetof(bm_voice, bandwidth_track) + sizeof(float)
                    - offsetof(bm_voice, f0_base);
        size_t float_fields = span / sizeof(float);
        printf("    %d keys for %lu tunable fields\n", nkeys, (unsigned long)float_fields);
        check((size_t)nkeys == float_fields,
              "key count matches the number of tunable fields");
    }

    /* Round-trip each preset through set_param and compare field by field. */
    for (p = 0; p < bm_voice_preset_count(); p++) {
        const bm_voice *src = bm_voice_preset_at(p);
        const float *sf = (const float *)(const void *)&src->f0_base;
        bm_voice dst;
        const float *df;

        bm_voice_default(&dst);
        for (k = 0; k < nkeys; k++) {
            if (bm_voice_set_param(&dst, KEYS[k], 0, sf[k]) != BM_OK) {
                printf("      key rejected: %s\n", KEYS[k]);
                mismatched++;
            }
        }

        df = (const float *)(const void *)&dst.f0_base;
        for (k = 0; k < nkeys; k++) {
            if (df[k] != sf[k]) {
                printf("      %s: field %d (%s) %.6g != %.6g\n",
                       src->name, k, KEYS[k], (double)df[k], (double)sf[k]);
                mismatched++;
            }
        }
    }
    check(mismatched == 0, "every preset survives a set_param round-trip");
}

static void test_coarticulation_is_optional(void)
{
    static float a[200000], b[200000];
    bm_voice va, vb;
    size_t   na, nb, i;
    double   diff = 0.0;

    printf("coarticulation\n");

    bm_voice_default(&va);
    va.coarticulation = 0.0f;
    vb = va;
    vb.coarticulation = 0.8f;

    na = render(&va, REFERENCE_UTTERANCE, a, sizeof a / sizeof a[0]);
    nb = render(&vb, REFERENCE_UTTERANCE, b, sizeof b / sizeof b[0]);

    check(na == nb, "coarticulation does not change timing");
    if (na != nb) return;

    for (i = 0; i < na; i++) diff += fabs((double)a[i] - (double)b[i]);
    diff /= (double)na;

    printf("    mean absolute difference at 0.0 vs 0.8: %.5f\n", diff);
    check(diff > 1e-4, "coarticulation audibly changes the output when on");
}

static void test_formant_glide(void)
{
    static float off[200000], on[200000];
    bm_voice a, b;
    size_t na, nb, i;
    double diff = 0.0;

    printf("formant glide\n");

    bm_voice_default(&a);
    a.formant_glide = 0.0f;
    b = a;
    b.formant_glide = 1.0f;

    /* A diphthong is the longest formant movement in the language, so it is
     * where linear-in-hertz and geometric spacing differ most. */
    na = render(&a, "AY1 AW1 OY1", off, sizeof off / sizeof off[0]);
    nb = render(&b, "AY1 AW1 OY1", on,  sizeof on  / sizeof on[0]);

    check(na == nb && na > 0, "glide does not change timing");
    if (na != nb || na == 0) return;

    for (i = 0; i < na; i++) diff += fabs((double)(off[i] - on[i]));
    diff /= (double)na;

    printf("    mean absolute difference, glide 0 vs 1: %.5f\n", diff);
    check(diff > 1e-4, "geometric spacing audibly changes diphthongs");

    /* And the off setting has to be exactly the old behaviour, not merely
     * close - that is what keeps Retro pinned. */
    check(bm_voice_preset("retro")->formant_glide == 0.0f,
          "Retro has formant_glide off");
}

/* Last-frame bandwidth for a phoneme, at a given tracking strength. */
static void bandwidths_for(const char *ph, float track, float *f1, float *b1)
{
    bm_frame_gen g;
    bm_voice     v;
    bm_frame     f;
    int          any = 0;

    bm_voice_default(&v);
    v.bandwidth_track = track;
    v.coarticulation = 0.0f;    /* keep the formant on its table value */
    bm_frame_gen_init(&g, FRAME_HZ, &v);
    if (bm_frame_gen_set_phonemes(&g, ph, 0) != BM_OK) { *f1 = *b1 = 0.0f; return; }
    while (bm_frame_gen_next(&g, &f)) any = 1;
    *f1 = any ? f.freq[0] : 0.0f;
    *b1 = any ? f.bw[0] : 0.0f;
}

static void test_bandwidth_tracking(void)
{
    float fi, bi, fa, ba, b_off;

    printf("bandwidth tracking\n");

    bandwidths_for("IY1", 0.0f, &fi, &b_off);
    check(b_off == 60.0f, "off, every vowel gets the table's 60 Hz first formant");

    bandwidths_for("IY1", 1.0f, &fi, &bi);
    bandwidths_for("AA1", 1.0f, &fa, &ba);

    printf("    /i/ F1 %.0f -> B1 %.1f Hz   (measured speech: ~45)\n",
           (double)fi, (double)bi);
    printf("    /a/ F1 %.0f -> B1 %.1f Hz   (measured speech: ~90)\n",
           (double)fa, (double)ba);

    check(bi < b_off && ba > b_off,
          "a low first formant narrows, a high one widens");
    check(bi > 30.0f && bi < 55.0f, "/i/ B1 is near the measured value");
    check(ba > 65.0f && ba < 100.0f, "/a/ B1 is near the measured value");
    check(bm_voice_preset("retro")->bandwidth_track == 0.0f,
          "Retro has bandwidth tracking off");
}

static void test_random_voices(void)
{
    bm_voice v, w;
    int i, hot = 0;

    printf("random voices\n");

    bm_voice_random(&v, 12345u);
    bm_voice_random(&w, 12345u);
    check(memcmp(&v.f0_base, &w.f0_base,
                 sizeof(bm_voice) - sizeof(const char *)) == 0,
          "the same seed gives the same voice");

    bm_voice_random(&w, 12346u);
    check(memcmp(&v.f0_base, &w.f0_base,
                 sizeof(bm_voice) - sizeof(const char *)) != 0,
          "an adjacent seed gives a different one");

    /* Every draw has to be usable. A generator that produces a voice which
     * clips, or one with a nonsensical vocal tract, is not an exploration tool
     * - it is a source of false negatives while hunting for good presets. */
    for (i = 0; i < 400; i++) {
        bm_voice_random(&v, (uint32_t)(i * 7919 + 1));
        if (v.f0_base < 60.0f || v.f0_base > 260.0f) { check(0, "f0 in range"); return; }
        if (v.throat < 0.5f || v.throat > 1.5f)      { check(0, "throat in range"); return; }
        if (v.mouth  < 0.5f || v.mouth  > 1.5f)      { check(0, "mouth in range"); return; }
        if (v.speed  < 0.5f || v.speed  > 2.0f)      { check(0, "speed in range"); return; }
        if (v.gain   <= 0.0f || v.gain  > 1.0f)      { check(0, "gain in range"); return; }
        /* Low flutter means peaks stack up, so the trim must follow it down. */
        if (v.f0_flutter < 0.2f && v.gain > 0.9f) hot++;
    }
    check(1, "400 seeds all produce plausible voices");
    check(hot == 0, "low-flutter voices get their gain trimmed");
}

int main(void)
{
    printf("\nBENCmouth voice tests\n\n");
    test_formant_glide();
    test_bandwidth_tracking();
    test_random_voices();
    test_presets();
    test_params();
    test_every_field_has_a_key();
    test_coarticulation_is_optional();
    test_retro_golden();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
