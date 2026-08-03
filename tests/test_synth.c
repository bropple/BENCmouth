/*
 * BENCmouth - synthesizer tests
 *
 * The question these answer is the one the vowel demo cannot: does the output
 * spectrum actually have peaks where we asked for formants? Listening is the
 * final judge but it is not a regression test, and "sounds a bit off" is a
 * miserable bug report to act on.
 *
 * The trick throughout is to excite with aspiration rather than voicing. A
 * voiced spectrum is a harmonic comb spaced at F0, so its peaks are harmonics
 * and the formants only show up in the envelope. Noise excitation gives a
 * continuous spectrum whose peaks are the formants directly, which makes the
 * measurement straightforward and the failure messages readable.
 */

#include "bm_synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI       3.14159265358979323846
#define FS       22050.0
#define NSAMPLES 44100u          /* two seconds; averages down noise variance */

/* Integer so NBINS is a constant expression usable as an array bound. */
#define BIN_LO_HZ   120
#define BIN_HI_HZ   4000
#define BIN_STEP_HZ 10
#define NBINS       ((BIN_HI_HZ - BIN_LO_HZ) / BIN_STEP_HZ + 1)

#define BIN_FREQ(i) ((double)BIN_LO_HZ + (double)BIN_STEP_HZ * (double)(i))

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ */

static void vowel_frame(bm_frame *f, float f1, float f2, float f3,
                        float av, float ah)
{
    static const float bw[BM_NFORMANTS] = { 60.0f, 90.0f, 150.0f, 200.0f, 250.0f };
    int i;

    memset(f, 0, sizeof *f);
    f->f0 = 120.0f;
    f->av = av;
    f->ah = ah;
    f->af = 0.0f;
    f->open_quotient = 0.5f;
    f->tilt = 0.0f;

    f->freq[0] = f1; f->freq[1] = f2; f->freq[2] = f3;
    f->freq[3] = 3500.0f; f->freq[4] = 4500.0f;
    for (i = 0; i < BM_NFORMANTS; i++) f->bw[i] = bw[i];

    /* Coincident pole and zero cancel exactly, switching the nasal branch off. */
    f->nasal_pole_f = 270.0f; f->nasal_pole_bw = 100.0f;
    f->nasal_zero_f = 270.0f; f->nasal_zero_bw = 100.0f;
}

static void render(const bm_frame *f, float *out, size_t n)
{
    bm_synth s;
    size_t   i;

    bm_synth_init(&s, (float)FS);
    bm_synth_set_frame(&s, f);
    /* Discard the filter transient before measuring. */
    for (i = 0; i < 4000; i++) (void)bm_synth_tick(&s);
    for (i = 0; i < n; i++) out[i] = bm_synth_tick(&s);
}

/* Goertzel magnitude at one frequency. */
static double mag_at(const float *x, size_t n, double freq)
{
    double w = 2.0 * cos(2.0 * PI * freq / FS);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    size_t i;

    for (i = 0; i < n; i++) {
        s0 = (double)x[i] + w * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - w * s1 * s2) / (double)n;
}

/* Magnitude spectrum, lightly smoothed so noise ripple does not register as
 * spurious formants. */
static void spectrum(const float *x, size_t n, double *mag)
{
    static double raw[NBINS];
    int i, k;

    for (i = 0; i < NBINS; i++) {
        raw[i] = mag_at(x, n, BIN_FREQ(i));
    }
    for (i = 0; i < NBINS; i++) {
        double sum = 0.0;
        int    cnt = 0;
        for (k = i - 3; k <= i + 3; k++) {
            if (k >= 0 && k < NBINS) { sum += raw[k]; cnt++; }
        }
        mag[i] = sum / (double)cnt;
    }
}

/* Frequency of the strongest local maximum within `tol` Hz of `target`,
 * or -1 if there is no local maximum in that window. */
static double peak_near(const double *mag, double target, double tol)
{
    double best_mag = -1.0, best_freq = -1.0;
    int    i;

    for (i = 1; i < NBINS - 1; i++) {
        double f = BIN_FREQ(i);
        if (fabs(f - target) > tol) continue;
        if (mag[i] >= mag[i - 1] && mag[i] >= mag[i + 1] && mag[i] > best_mag) {
            best_mag = mag[i];
            best_freq = f;
        }
    }
    return best_freq;
}

/* ------------------------------------------------------------------ */

static void test_formant_peaks(void)
{
    static const struct { const char *name; float f1, f2, f3; } V[] = {
        { "/i/", 270.0f, 2290.0f, 3010.0f },
        { "/e/", 530.0f, 1840.0f, 2480.0f },
        { "/a/", 730.0f, 1090.0f, 2440.0f },
        { "/o/", 570.0f,  840.0f, 2410.0f },
        { "/u/", 300.0f,  870.0f, 2240.0f }
    };
    static double mag[NBINS];
    float   *buf = (float *)malloc(NSAMPLES * sizeof *buf);
    size_t   v;

    printf("formant peaks (noise-excited, so peaks are formants not harmonics)\n");

    for (v = 0; v < sizeof V / sizeof V[0]; v++) {
        bm_frame f;
        double   targets[3];
        int      j, ok = 1;

        /* Aspiration only - no voicing - so the spectrum is continuous. */
        vowel_frame(&f, V[v].f1, V[v].f2, V[v].f3, 0.0f, 60.0f);
        render(&f, buf, NSAMPLES);
        spectrum(buf, NSAMPLES, mag);

        targets[0] = (double)V[v].f1;
        targets[1] = (double)V[v].f2;
        targets[2] = (double)V[v].f3;

        printf("    %s wanted %4.0f %4.0f %4.0f   got ",
               V[v].name, targets[0], targets[1], targets[2]);

        for (j = 0; j < 3; j++) {
            /* Generous window: formant peaks shift slightly when neighbours are
             * close, which is real behaviour, not a defect. */
            double tol = 0.12 * targets[j] + 40.0;
            double got = peak_near(mag, targets[j], tol);
            if (got < 0.0) { printf("  --- "); ok = 0; }
            else           { printf(" %4.0f ", got); }
        }
        printf("\n");
        check(ok, "all three formants produced a spectral peak");
    }

    free(buf);
}

static void test_voiced_periodicity(void)
{
    const size_t n = 22050u;
    float  *buf = (float *)malloc(n * sizeof *buf);
    bm_frame f;
    double  best = -1e30;
    int     best_lag = 0, lag;
    int     expected;

    printf("voiced source\n");

    vowel_frame(&f, 730.0f, 1090.0f, 2440.0f, 60.0f, 0.0f);
    f.f0 = 120.0f;
    render(&f, buf, n);

    /* Autocorrelation over a plausible pitch range: the strongest lag should be
     * the glottal period. */
    for (lag = 60; lag < 400; lag++) {
        double acc = 0.0;
        size_t i;
        for (i = 0; i + (size_t)lag < n; i++) acc += (double)buf[i] * (double)buf[i + (size_t)lag];
        if (acc > best) { best = acc; best_lag = lag; }
    }

    expected = (int)(FS / 120.0 + 0.5);
    printf("    period %d samples, expected %d (%.1f Hz vs 120 Hz)\n",
           best_lag, expected, FS / best_lag);
    check(abs(best_lag - expected) <= 3, "autocorrelation period matches F0");

    free(buf);
}

static void test_nasal_cancellation(void)
{
    const size_t n = 8192u;
    float  *a = (float *)malloc(n * sizeof *a);
    float  *b = (float *)malloc(n * sizeof *b);
    bm_frame fa, fb;
    double   diff = 0.0;
    size_t   i;

    printf("nasal branch\n");

    /* Coincident pole/zero should be exactly transparent, since one is the
     * algebraic inverse of the other and both are DC-normalized. Moving the
     * pair together to a different frequency must therefore change nothing. */
    vowel_frame(&fa, 730.0f, 1090.0f, 2440.0f, 0.0f, 60.0f);
    fb = fa;
    fb.nasal_pole_f = 900.0f; fb.nasal_zero_f = 900.0f;
    fb.nasal_pole_bw = 250.0f; fb.nasal_zero_bw = 250.0f;

    render(&fa, a, n);
    render(&fb, b, n);

    for (i = 0; i < n; i++) diff += fabs((double)a[i] - (double)b[i]);
    diff /= (double)n;

    printf("    mean absolute difference %.3g\n", diff);
    check(diff < 1e-4, "coincident nasal pole/zero is transparent");

    free(a);
    free(b);
}

static void test_silence_and_sanity(void)
{
    const size_t n = 8192u;
    float  *buf = (float *)malloc(n * sizeof *buf);
    bm_frame f;
    size_t   i;
    int      nonzero = 0, bad = 0;
    double   peak = 0.0;

    printf("sanity\n");

    vowel_frame(&f, 730.0f, 1090.0f, 2440.0f, 0.0f, 0.0f);
    render(&f, buf, n);
    for (i = 0; i < n; i++) if (buf[i] != 0.0f) nonzero++;
    check(nonzero == 0, "all amplitudes at 0 dB produces exact silence");

    /* Every branch active at once, which is not a real speech sound but is the
     * worst case for overflow. */
    vowel_frame(&f, 730.0f, 1090.0f, 2440.0f, 60.0f, 60.0f);
    f.af = 60.0f;
    f.par_bypass = 60.0f;
    for (i = 0; i < BM_NFORMANTS; i++) f.par_amp[i] = 60.0f;
    render(&f, buf, n);
    for (i = 0; i < n; i++) {
        double m = fabs((double)buf[i]);
        if (!(buf[i] == buf[i]) || m > 1e4) bad++;
        if (m > peak) peak = m;
    }
    printf("    all branches at full: peak %.2f\n", peak);
    check(bad == 0, "no NaN or runaway with every branch at full scale");

    free(buf);
}

int main(void)
{
    printf("\nBENCmouth synthesizer tests\n\n");
    test_formant_peaks();
    test_voiced_periodicity();
    test_nasal_cancellation();
    test_silence_and_sanity();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
