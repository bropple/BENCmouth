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

#include "bm_fixed.h"
#include "bm_math.h"
#include "bm_resonator.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define PI 3.14159265358979323846

/* Fixed-point stores coefficients as Q16, so the unity-DC-gain property that
 * lets the cascade chain without a makeup stage holds to about 1e-3 rather than
 * exactly. Stated here rather than loosened everywhere: the float path really
 * is accurate to 1e-4, and hiding that behind one tolerance would lose the
 * distinction. */
#if BM_FIXED_POINT
#define DC_GAIN_TOL 2e-3
#else
#define DC_GAIN_TOL 1e-4
#endif

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

    {
        double worst_log = 0.0;
        for (i = -600; i <= 600; i++) {
            double x = pow(10.0, i * 0.01);          /* 1e-6 .. 1e6 */
            double err = fabs((double)bm_log2f((float)x) - log2(x));
            if (err > worst_log) worst_log = err;
        }
        check(worst_log < 2e-6, "bm_log2f absolute error < 2e-6 over [1e-6, 1e6]");
        printf("      worst absolute error: %.3g\n", worst_log);

        check_close((double)bm_log2f(1.0f), 0.0, 1e-6, "log2(1) is 0");
        check_close((double)bm_log2f(1024.0f), 10.0, 1e-5, "log2(1024) is 10");
        /* Round trip against the exp2 it will be paired with. */
        check_close((double)bm_exp2f(bm_log2f(440.0f)), 440.0, 0.02,
                    "exp2(log2(440)) round-trips");
    }
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
    check_close(dc, 1.0, DC_GAIN_TOL, "DC gain is unity (F=700, BW=90)");

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
        check_close(dc, 1.0, DC_GAIN_TOL, "antiresonator DC gain is unity");
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

/* A resonance above the representable band is bypassed, not moved.
 *
 * These sections are normalised to unity gain at DC, and that is only harmless
 * while the poles stay away from Nyquist: as a pole approaches it, the gain at
 * the resonance grows relative to DC without bound. Five of those in a cascade
 * multiply, and the engine got quietly, enormously louder as the sample rate
 * fell - peak 0.6 at 22050, 22 at 8000, 1968 at 6000, all of it finite and all
 * of it wrong.
 *
 * Measured here at the level it happens: one section's peak gain, swept. The
 * cascade is what the listener hears, but the section is where the fault is. */
static void test_band_limit(void)
{
    const float fs = 8000.0f;
    bm_resonator r;
    float peak, x;
    int i;

    printf("band limit\n");

    /* Inside the band: an ordinary resonator, gain of a few at its peak. */
    bm_resonator_set(&r, 1000.0f, 100.0f, fs);
    bm_resonator_reset(&r);
    peak = 0.0f;
    for (i = 0; i < 4000; i++) {
        x = (i == 0) ? 1.0f : 0.0f;
        {
            float y = bm_resonator_tick(&r, x);
            if (y > peak) peak = y;
            if (-y > peak) peak = -y;
        }
    }
    printf("    1000 Hz at 8 kHz: impulse peak %.3f\n", (double)peak);
    check(peak > 0.0f && peak < 2.0f, "a resonance inside the band behaves");

    /* Above 0.40 of the rate: bypassed, so the impulse comes straight back and
     * nothing follows it. Asserted as identity rather than as "small", because
     * bypass is the actual claim. */
    bm_resonator_set(&r, 3750.0f, 100.0f, fs);
    bm_resonator_reset(&r);
    {
        float first = bm_resonator_tick(&r, 1.0f);
        float rest = 0.0f;
        for (i = 0; i < 500; i++) {
            float y = bm_resonator_tick(&r, 0.0f);
            if (y > rest) rest = y;
            if (-y > rest) rest = -y;
        }
        printf("    3750 Hz at 8 kHz: first %.3f, tail %.3e\n",
               (double)first, (double)rest);
        check(first == 1.0f && rest == 0.0f,
              "a resonance above the band passes the signal through untouched");
    }

    /* And it is not doing that at any rate anyone actually renders at: the
     * highest formant in the table is about 3020 Hz. */
    bm_resonator_set(&r, 3020.0f, 100.0f, 22050.0f);
    bm_resonator_reset(&r);
    {
        float first = bm_resonator_tick(&r, 1.0f);
        check(first != 1.0f, "and the highest real formant is untouched at 22 kHz");
    }
}

int main(void)
{
    printf("\nBENCmouth resonator tests\n\n");
    test_math();
    test_resonator();
    test_antiresonator();
    test_stability();
    test_band_limit();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
