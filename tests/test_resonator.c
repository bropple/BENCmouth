/*
 * BENCmouth - resonator and math tests
 *
 * These check the properties the synthesizer actually depends on: unity DC
 * gain (so the cascade needs no makeup gain), a resonant peak at the requested
 * frequency, a notch where the antiresonator was asked for one, and stability
 * under abusive parameters. A formant synth that fails any of these will
 * produce sound - just the wrong sound - so catching it here is much cheaper
 * than catching it by ear later.
 */

#include "bm_math.h"
#include "bm_resonator.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define PI 3.14159265358979323846

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void check_close(double got, double want, double tol, const char *what)
{
    int ok = fabs(got - want) <= tol;
    printf("  %-58s %s  (got %.6g, want %.6g)\n",
           what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
}

/* Steady-state amplitude of the resonator's response to a unit sine at `freq`,
 * measured by quadrature correlation after the transient has settled. */
static double response_at(const bm_resonator *proto, float freq, float fs)
{
    bm_resonator r = *proto;
    const int settle = 8000, window = 16000;
    double re = 0.0, im = 0.0;
    int i;

    bm_resonator_reset(&r);
    for (i = 0; i < settle + window; i++) {
        double t = 2.0 * PI * (double)freq * (double)i / (double)fs;
        float  y = bm_resonator_tick(&r, (float)sin(t));
        if (i >= settle) {
            re += (double)y * cos(t);
            im += (double)y * sin(t);
        }
    }
    return 2.0 * sqrt(re * re + im * im) / (double)window;
}

static double antiresponse_at(const bm_resonator *proto, float freq, float fs)
{
    bm_resonator r = *proto;
    const int settle = 8000, window = 16000;
    double re = 0.0, im = 0.0;
    int i;

    bm_resonator_reset(&r);
    for (i = 0; i < settle + window; i++) {
        double t = 2.0 * PI * (double)freq * (double)i / (double)fs;
        float  y = bm_antiresonator_tick(&r, (float)sin(t));
        if (i >= settle) {
            re += (double)y * cos(t);
            im += (double)y * sin(t);
        }
    }
    return 2.0 * sqrt(re * re + im * im) / (double)window;
}

/* ------------------------------------------------------------------ */

static void test_math(void)
{
    double worst_exp = 0.0, worst_cos = 0.0;
    int i;

    printf("math\n");

    for (i = -600; i <= 600; i++) {
        double x    = i * 0.05;
        double got  = (double)bm_exp2f((float)x);
        double want = pow(2.0, x);
        double rel  = fabs(got - want) / want;
        if (rel > worst_exp) worst_exp = rel;
    }
    check(worst_exp < 2e-5, "bm_exp2f relative error < 2e-5 over [-30, 30]");
    printf("      worst relative error: %.3g\n", worst_exp);

    for (i = -2000; i <= 2000; i++) {
        double x    = i * 0.01;
        double got  = (double)bm_cosf((float)x);
        double want = cos(x);
        double err  = fabs(got - want);
        if (err > worst_cos) worst_cos = err;
    }
    check(worst_cos < 1e-5, "bm_cosf absolute error < 1e-5 over [-20, 20]");
    printf("      worst absolute error: %.3g\n", worst_cos);

    check_close((double)bm_db_to_linear(0.0f),   1.0,   1e-5, "0 dB is unity gain");
    /* Half amplitude is -6.0206 dB, not -6. */
    check_close((double)bm_db_to_linear(-6.0206f), 0.5,  1e-4, "-6.0206 dB is half amplitude");
    check_close((double)bm_db_to_linear(-6.0f),  pow(10.0, -6.0 / 20.0),
                1e-5, "-6 dB matches 10^(db/20)");
    check_close((double)bm_db_to_linear(20.0f),  10.0,  1e-2, "+20 dB is ten times");
    check_close((double)bm_sinf((float)(PI / 6.0)), 0.5, 1e-5, "sin(pi/6) is 0.5");
}

static void test_resonator(void)
{
    const float fs = 22050.0f;
    bm_resonator r;
    double dc, at_peak, below, above, peak_freq, best;
    float  f;
    int    i;

    printf("resonator\n");

    /* Unity DC gain: this is the property that lets five resonators chain
     * without a makeup gain stage. */
    bm_resonator_set(&r, 700.0f, 90.0f, fs);
    bm_resonator_reset(&r);
    for (i = 0; i < 20000; i++) (void)bm_resonator_tick(&r, 1.0f);
    dc = (double)bm_resonator_tick(&r, 1.0f);
    check_close(dc, 1.0, 1e-4, "DC gain is unity (F=700, BW=90)");

    /* The peak should sit where we asked for it. */
    best = 0.0; peak_freq = 0.0;
    for (f = 600.0f; f <= 800.0f; f += 2.0f) {
        double m = response_at(&r, f, fs);
        if (m > best) { best = m; peak_freq = (double)f; }
    }
    check(fabs(peak_freq - 700.0) <= 6.0, "peak within 6 Hz of requested 700 Hz");
    printf("      measured peak at %.0f Hz, gain %.2f\n", peak_freq, best);

    /* And it should actually be a peak - well above the skirts. */
    at_peak = response_at(&r, 700.0f, fs);
    below   = response_at(&r, 300.0f, fs);
    above   = response_at(&r, 1500.0f, fs);
    check(at_peak > below * 4.0 && at_peak > above * 4.0,
          "peak is >12 dB above the skirts at 300 and 1500 Hz");

    /* Narrower bandwidth means a taller, sharper peak. */
    {
        bm_resonator narrow, wide;
        bm_resonator_set(&narrow, 700.0f,  40.0f, fs);
        bm_resonator_set(&wide,   700.0f, 200.0f, fs);
        check(response_at(&narrow, 700.0f, fs) > response_at(&wide, 700.0f, fs) * 2.0,
              "narrower bandwidth gives a taller peak");
    }
}

static void test_antiresonator(void)
{
    const float fs = 22050.0f;
    bm_resonator z;
    double at_notch, off_notch;

    printf("antiresonator\n");

    bm_antiresonator_set(&z, 1200.0f, 100.0f, fs);

    at_notch  = antiresponse_at(&z, 1200.0f, fs);
    off_notch = antiresponse_at(&z, 300.0f,  fs);

    check(at_notch < off_notch * 0.2,
          "notch attenuates >14 dB relative to 300 Hz");
    printf("      notch %.4f vs passband %.4f\n", at_notch, off_notch);

    /* Unity DC gain here too. */
    {
        int i;
        double dc;
        bm_resonator_reset(&z);
        for (i = 0; i < 20000; i++) (void)bm_antiresonator_tick(&z, 1.0f);
        dc = (double)bm_antiresonator_tick(&z, 1.0f);
        check_close(dc, 1.0, 1e-4, "antiresonator DC gain is unity");
    }
}

static void test_stability(void)
{
    const float fs = 22050.0f;
    /* Parameters that interpolation overshoot could plausibly produce, plus a
     * few that are simply nonsense. None may produce NaN or blow up. */
    static const float freqs[] = { -500.0f, 0.0f, 1.0f, 11024.0f, 30000.0f, 1e9f };
    static const float bws[]   = { -10.0f, 0.0f, 0.5f, 1.0f, 5000.0f, 1e9f };
    size_t fi, bi;
    int    i, bad = 0;
    double worst = 0.0;

    printf("stability\n");

    for (fi = 0; fi < sizeof freqs / sizeof freqs[0]; fi++) {
        for (bi = 0; bi < sizeof bws / sizeof bws[0]; bi++) {
            bm_resonator r;
            uint32_t     seed;

            /* Full-scale white-ish drive, the worst case for a resonator.
             * Unsigned LCG - signed overflow here would be UB, and at -O2 that
             * is not a theoretical concern. */
            bm_resonator_set(&r, freqs[fi], bws[bi], fs);
            bm_resonator_reset(&r);
            seed = 1u;
            for (i = 0; i < 40000; i++) {
                float x, y;
                seed = seed * 1103515245u + 12345u;
                x = (float)((seed >> 16) & 0xFFFFu) / 32768.0f - 1.0f;
                y = bm_resonator_tick(&r, x);
                if (!(y == y) || fabs((double)y) > 1e6) { bad++; break; }
                if (fabs((double)y) > worst) worst = fabs((double)y);
            }

            bm_antiresonator_set(&r, freqs[fi], bws[bi], fs);
            bm_resonator_reset(&r);
            seed = 1u;
            for (i = 0; i < 40000; i++) {
                float x, y;
                seed = seed * 1103515245u + 12345u;
                x = (float)((seed >> 16) & 0xFFFFu) / 32768.0f - 1.0f;
                y = bm_antiresonator_tick(&r, x);
                if (!(y == y) || fabs((double)y) > 1e6) { bad++; break; }
                if (fabs((double)y) > worst) worst = fabs((double)y);
            }
        }
    }
    check(bad == 0, "no NaN or blowup across 36 abusive parameter pairs");
    printf("      worst peak excursion: %.4g\n", worst);
}

int main(void)
{
    printf("\nBENCmouth resonator tests\n\n");
    test_math();
    test_resonator();
    test_antiresonator();
    test_stability();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
