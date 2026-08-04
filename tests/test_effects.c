/*
 * BENCmouth - effects tests
 *
 * The load-bearing one is test_bypass_is_exact. Everything else here checks
 * that an effect does what it claims; that one checks that the stage's presence
 * costs nothing when it is off, which is what lets BENCmouth Retro's golden
 * reference survive a whole new stage in the signal path.
 */

#include "bm_effects.h"
#include "bm_frames.h"
#include "bm_synth.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FS       22050.0f
#define FRAME_HZ 100.0f

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ */

static size_t render(const bm_voice *v, const bm_effects *fx,
                     const char *phonemes, float *out, size_t cap)
{
    bm_frame_gen gen;
    bm_synth     synth;
    bm_frame     f;
    size_t       spf, pos = 0;

    bm_frame_gen_init(&gen, FRAME_HZ, v);
    if (bm_frame_gen_set_phonemes(&gen, phonemes, 0) != BM_OK) return 0;

    bm_synth_init(&synth, FS);
    bm_synth_set_flutter(&synth, v->f0_flutter);
    bm_synth_set_vibrato(&synth, v->vibrato, v->vibrato_rate);
    bm_synth_set_gain(&synth, v->gain);
    if (fx != 0) bm_synth_set_effects(&synth, fx);

    spf = (size_t)(FS / FRAME_HZ);
    while (bm_frame_gen_next(&gen, &f) && pos + spf <= cap) {
        size_t i;
        bm_synth_set_frame(&synth, &f);
        for (i = 0; i < spf; i++) out[pos++] = bm_synth_tick(&synth);
    }
    return pos;
}

/* Goertzel energy at one frequency. */
static double bin(const float *x, size_t n, double hz)
{
    double w = 2.0 * 3.14159265358979 * hz / (double)FS;
    double coeff = 2.0 * cos(w), s1 = 0.0, s2 = 0.0;
    size_t i;

    for (i = 0; i < n; i++) {
        double s0 = (double)x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

static double rms_of(const float *x, size_t n)
{
    double s = 0.0;
    size_t i;
    for (i = 0; i < n; i++) s += (double)x[i] * (double)x[i];
    return (n > 0) ? sqrt(s / (double)n) : 0.0;
}

/* ------------------------------------------------------------------ */

static float a[200000], b[200000];

/* The contract that lets the stage exist at all. */
static void test_bypass_is_exact(void)
{
    bm_voice   v;
    bm_effects off;
    size_t     na, nb, i;
    int        differing = 0;

    printf("bypass\n");

    bm_voice_default(&v);
    bm_effects_default(&off);

    /* No effects object at all, versus an all-zero one. */
    na = render(&v, 0,    "HH AH0 L OW1 SIL W ER1 L D", a, sizeof a / sizeof a[0]);
    nb = render(&v, &off, "HH AH0 L OW1 SIL W ER1 L D", b, sizeof b / sizeof b[0]);

    check(na == nb && na > 0, "the same number of samples");
    if (na != nb) return;

    for (i = 0; i < na; i++) {
        if (a[i] != b[i]) differing++;
    }
    printf("    %d of %lu samples differ\n", differing, (unsigned long)na);

    /* Not "close" - identical. An effects stage that perturbs the output when
     * switched off would drift the pinned voice by a little every release. */
    check(differing == 0, "a zeroed chain is bit-for-bit the unprocessed signal");

    check(bm_effects_preset("none")->ring == 0.0f &&
          bm_effects_preset("none")->drive == 0.0f,
          "the None preset really is nothing");
}

/* Ring modulation replaces each component with a pair of sidebands. Testing for
 * the sidebands rather than for "it sounds different" is the difference between
 * checking the mechanism and checking that something happened. */
static void test_ring_makes_sidebands(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     n;
    double     f0, carrier = 70.0;
    double     dry_lower, dry_f0, wet_lower, wet_f0;

    printf("ring modulation\n");

    bm_voice_default(&v);
    v.f0_flutter = 0.0f;
    v.prosody = 0.0f;
    v.f0_base = 110.0f;
    /* The pre-prosody contour declines across the utterance, so the
     * fundamental in the middle third is not f0_base. Measure where it
     * actually is rather than where it started. */
    f0 = (double)v.f0_base * 0.90;

    bm_effects_default(&fx);
    n = render(&v, &fx, "[hold 500] AA1 AA1 AA1", a, sizeof a / sizeof a[0]);
    if (n < 20000) { check(0, "renders"); return; }

    dry_f0    = bin(a + n / 3, 8192, f0);
    dry_lower = bin(a + n / 3, 8192, f0 - carrier);

    fx.ring = 1.0f;
    fx.ring_hz = (float)carrier;
    n = render(&v, &fx, "[hold 500] AA1 AA1 AA1", b, sizeof b / sizeof b[0]);

    wet_f0    = bin(b + n / 3, 8192, f0);
    wet_lower = bin(b + n / 3, 8192, f0 - carrier);

    printf("    at f0 (110 Hz):        dry %.3e  wet %.3e\n", dry_f0, wet_f0);
    printf("    at f0-carrier (40 Hz): dry %.3e  wet %.3e\n", dry_lower, wet_lower);

    /* The carrier is what the fundamental is traded for: it should collapse
     * where it was and appear where it was not. */
    check(wet_f0 < dry_f0 * 0.25, "the original fundamental is suppressed");
    check(wet_lower > dry_lower * 4.0, "a sideband appears below it");
}

/* Drive is supposed to change the timbre and not the level. Both halves are
 * asserted, because getting the first without the second is easy and useless -
 * a distortion that is merely louder is not distortion. */
static void test_drive_adds_harmonics_not_level(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     na, nb;
    double     clean_rms, driven_rms, clean_hi, driven_hi, ratio;

    printf("drive\n");

    bm_voice_default(&v);
    bm_effects_default(&fx);
    na = render(&v, &fx, "HH AH0 L OW1 SIL W ER1 L D", a, sizeof a / sizeof a[0]);

    fx.drive = 1.0f;
    nb = render(&v, &fx, "HH AH0 L OW1 SIL W ER1 L D", b, sizeof b / sizeof b[0]);

    check(na == nb && na > 0, "drive does not change timing");
    if (na != nb) return;

    clean_rms  = rms_of(a, na);
    driven_rms = rms_of(b, nb);
    ratio      = driven_rms / clean_rms;
    printf("    RMS clean %.4f, driven %.4f  (%.2fx, %+.1f dB)\n",
           clean_rms, driven_rms, ratio, 20.0 * log10(ratio));

    /* The compensation curve was fitted to hold this flat. A wide bound: the
     * point is that the knob is not a volume control, not that the fit is
     * exact to a fraction of a dB. */
    check(ratio > 0.7 && ratio < 1.4, "full drive leaves the level alone");

    /* Energy well above where a formant synthesizer puts any: entirely
     * generated by the shaper. */
    clean_hi  = bin(a + na / 2, 8192, 3300.0) + bin(a + na / 2, 8192, 4100.0);
    driven_hi = bin(b + nb / 2, 8192, 3300.0) + bin(b + nb / 2, 8192, 4100.0);
    printf("    energy above 3 kHz: clean %.3e, driven %.3e\n",
           clean_hi, driven_hi);
    check(driven_hi > clean_hi * 3.0, "and generates harmonics that were not there");
}

/* Sample-rate reduction holds each value for a whole step. That is directly
 * observable, so observe it rather than inferring it from a spectrum. */
static void test_crush_holds_samples(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     n, i;
    int        longest = 1, run_len = 1;

    printf("crush\n");

    bm_voice_default(&v);
    bm_effects_default(&fx);
    fx.crush = 1.0f;                       /* the deepest hold available */

    n = render(&v, &fx, "[hold 500] AA1 AA1 AA1", a, sizeof a / sizeof a[0]);
    if (n < 1000) { check(0, "renders"); return; }

    /* Nonzero only. A held zero is indistinguishable from silence, and the
     * leading and trailing silence of any utterance is thousands of samples of
     * exact zero - which the first version of this measured, and reported a
     * hold of 228. */
    for (i = 1; i < n; i++) {
        if (a[i] != 0.0f && a[i] == a[i - 1]) {
            run_len++;
            if (run_len > longest) longest = run_len;
        } else {
            run_len = 1;
        }
    }
    printf("    longest run of identical samples: %d\n", longest);

    /* Exactly the documented maximum hold. Longer would mean the counter is
     * wrong; shorter would mean the stage is not doing anything. */
    check(longest == 12, "the deepest setting holds each sample 12 times");
}

/* A feedback comb resonates at multiples of its spacing and notches between
 * them. Driven with the noisiest thing available, so the result is the filter
 * and not the source. */
static void test_comb_resonates(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     n;
    double     peak_bin, notch_bin;
    const double spacing = 400.0;

    printf("comb\n");

    bm_voice_default(&v);
    v.whisper = 1.0f;                      /* noise excitation: a flat-ish source */

    bm_effects_default(&fx);
    fx.comb = 1.0f;
    fx.comb_hz = (float)spacing;

    n = render(&v, &fx, "[hold 500] AA1 AA1 AA1", a, sizeof a / sizeof a[0]);
    if (n < 20000) { check(0, "renders"); return; }

    /* A peak sits on a multiple of the spacing, a notch halfway between. */
    peak_bin  = bin(a + n / 3, 8192, spacing * 4.0);
    notch_bin = bin(a + n / 3, 8192, spacing * 4.5);
    printf("    1600 Hz (on a tooth) %.3e   1800 Hz (between) %.3e\n",
           peak_bin, notch_bin);

    check(peak_bin > notch_bin * 3.0, "energy piles up on the comb's teeth");
}

static void test_presets_and_params(void)
{
    static const char *KEYS[] = {
        "ring", "ring_hz", "comb", "comb_hz", "drive", "crush", "level"
    };
    const int nkeys = (int)(sizeof KEYS / sizeof KEYS[0]);
    int p, k, bad = 0;

    printf("presets and parameters\n");

    printf("    %d presets:", bm_effects_preset_count());
    for (p = 0; p < bm_effects_preset_count(); p++) {
        printf(" \"%s\"", bm_effects_preset_at(p)->name);
    }
    printf("\n");

    check(bm_effects_preset("enforcer") != 0, "a name resolves");
    check(bm_effects_preset("EN-FORCER") == bm_effects_preset("enforcer"),
          "case and punctuation ignored");
    check(bm_effects_preset("nonexistent") == 0, "an unknown name returns NULL");
    check(bm_effects_preset_at(-1) == 0 &&
          bm_effects_preset_at(bm_effects_preset_count()) == 0,
          "the index is bounds-checked");

    {
        bm_effects e;
        bm_effects_default(&e);
        check(bm_effects_set_param(&e, "wibble", 0, 1.0f) == BM_ERR_ARG,
              "an unknown key is rejected");
        check(bm_effects_set_param(&e, "DRIVE", 0, 0.5f) == BM_OK &&
              e.drive == 0.5f, "keys are case-insensitive");
    }

    /* The same span check the voice test uses: if a field is added without a
     * key, a saved chain silently drops it. */
    {
        size_t span = offsetof(bm_effects, level) + sizeof(float)
                    - offsetof(bm_effects, ring);
        printf("    %d keys for %lu fields\n", nkeys,
               (unsigned long)(span / sizeof(float)));
        check((size_t)nkeys == span / sizeof(float),
              "every field has a key");
    }

    for (p = 0; p < bm_effects_preset_count(); p++) {
        const bm_effects *src = bm_effects_preset_at(p);
        const float *sf = (const float *)(const void *)&src->ring;
        bm_effects dst;
        const float *df;

        bm_effects_default(&dst);
        for (k = 0; k < nkeys; k++) {
            if (bm_effects_set_param(&dst, KEYS[k], 0, sf[k]) != BM_OK) bad++;
        }
        df = (const float *)(const void *)&dst.ring;
        for (k = 0; k < nkeys; k++) {
            if (df[k] != sf[k]) {
                printf("      %s: %s %.6g != %.6g\n", src->name, KEYS[k],
                       (double)df[k], (double)sf[k]);
                bad++;
            }
        }
    }
    check(bad == 0, "every preset survives a set_param round-trip");
}

/* No preset may push the output into the host limiter, and none may be so far
 * below the others that switching to it sounds like a mistake. */
static void test_presets_are_level_matched(void)
{
    bm_voice v;
    size_t   n;
    double   dry;
    int      p, bad = 0;

    printf("preset levels\n");

    bm_voice_default(&v);
    n = render(&v, 0, "HH AH0 L OW1 SIL W ER1 L D", a, sizeof a / sizeof a[0]);
    dry = rms_of(a, n);

    for (p = 0; p < bm_effects_preset_count(); p++) {
        const bm_effects *fx = bm_effects_preset_at(p);
        double wet, peak = 0.0, ratio;
        size_t i;

        n = render(&v, fx, "HH AH0 L OW1 SIL W ER1 L D", b, sizeof b / sizeof b[0]);
        wet = rms_of(b, n);
        for (i = 0; i < n; i++) {
            double m = fabs((double)b[i]);
            if (m > peak) peak = m;
        }
        ratio = wet / dry;

        printf("    %-11s peak %.3f  rms %.4f  (%+.1f dB vs dry)\n",
               fx->name, peak, wet, 20.0 * log10(ratio));

        if (peak > 0.85) { printf("      ^ would hit the limiter\n"); bad++; }
        if (ratio < 0.5 || ratio > 2.0) { printf("      ^ off level\n"); bad++; }
    }
    check(bad == 0, "every preset is level-matched and clear of the limiter");
}

int main(void)
{
    printf("\nBENCmouth effects tests\n\n");
#if !BM_WITH_EFFECTS
    printf("  built with BM_WITH_EFFECTS=0; the stage is compiled out\n");
    test_bypass_is_exact();
    test_presets_and_params();
#else
    test_bypass_is_exact();
    test_ring_makes_sidebands();
    test_drive_adds_harmonics_not_level();
    test_crush_holds_samples();
    test_comb_resonates();
    test_presets_and_params();
    test_presets_are_level_matched();
#endif
    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
