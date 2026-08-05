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

/* A chorus has to *detune*, not merely delay. The comb already delays, and it
 * sounds like a tube for exactly that reason - so the thing to measure is that
 * the fundamental has been smeared across a band rather than copied.
 *
 * Measured as the energy at the fundamental relative to a pair of bins a few
 * cents either side of it. A dry steady tone is concentrated: the neighbours
 * are far below the peak. Three copies at slowly-moving delays are three
 * slightly different pitches, so the peak drops and the neighbours rise. */
static void test_chorus_detunes(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     n;
    double     dry_c, dry_s, wet_c, wet_s, dry_ratio, wet_ratio;
    const double f0 = 110.0;
    /* About 40 cents either side - wider than the sweep, so a pure delay would
     * leave these empty. */
    const double LO = f0 * 0.977, HI = f0 * 1.023;

    printf("chorus\n");

    bm_voice_default(&v);
    v.f0_flutter = 0.0f;
    v.prosody = 0.0f;

    /* [note A2] is exactly 110 Hz and carries no declination, so the bin below
     * is on the fundamental rather than near it. Measuring at f0_base instead
     * put the centre bin off-pitch, and the "sidebands" then held more energy
     * than the "centre" before any chorus was applied at all. */
    bm_effects_default(&fx);
    n = render(&v, &fx, "[note A2][hold 600] AA1 AA1 AA1", a, sizeof a / sizeof a[0]);
    if (n < 20000) { check(0, "renders"); return; }
    dry_c = bin(a + n / 3, 8192, f0);
    dry_s = bin(a + n / 3, 8192, LO) + bin(a + n / 3, 8192, HI);

    fx.chorus = 1.0f;
    fx.chorus_hz = 0.5f;
    n = render(&v, &fx, "[note A2][hold 600] AA1 AA1 AA1", b, sizeof b / sizeof b[0]);
    wet_c = bin(b + n / 3, 8192, f0);
    wet_s = bin(b + n / 3, 8192, LO) + bin(b + n / 3, 8192, HI);

    dry_ratio = dry_s / (dry_c > 0.0 ? dry_c : 1.0);
    wet_ratio = wet_s / (wet_c > 0.0 ? wet_c : 1.0);
    printf("    sideband/centre energy: dry %.4f, chorused %.4f\n",
           dry_ratio, wet_ratio);

    check(wet_ratio > dry_ratio * 3.0,
          "the fundamental is spread across a band, not copied");

    /* And the taps must be interpolated. Without it the delay steps by whole
     * samples as it sweeps, which is a train of small discontinuities - it
     * shows up as broadband junk far from the fundamental. */
    {
        double junk_dry = bin(a + n / 3, 8192, 5000.0);
        double junk_wet = bin(b + n / 3, 8192, 5000.0);
        printf("    energy at 5 kHz: dry %.3e, chorused %.3e\n",
               junk_dry, junk_wet);
        check(junk_wet < junk_dry * 50.0,
              "and the sweep does not introduce broadband noise");
    }
}

/* Carrier frequency over a window, by counting rising zero crossings.
 *
 * Adequate because the thing being measured is a sine and nothing else: the
 * ring stage fed a constant hands back its own carrier, scaled. Anything
 * cleverer would be measuring the measurement. */
static double carrier_hz(const float *x, size_t n)
{
    size_t i;
    long   crossings = 0;

    for (i = 1; i < n; i++) {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) crossings++;
    }
    return (double)crossings * (double)FS / (double)n;
}

/* The drift moves the carrier, and moving it is the whole point.
 *
 * A ring modulator fed a constant outputs its carrier, so this drives the
 * stage with 1.0 and reads the frequency straight off. Off, it must be the
 * requested frequency for as long as anyone listens; on, it must be somewhere
 * else two seconds later. */
static void test_ring_drift(void)
{
    /* Nine seconds, as an integer count because a static array needs a
     * constant expression and FS is a float. Long enough to hold three
     * samples of an eight-second drift cycle. */
#define DRIFT_SECONDS 9
#define DRIFT_N       (22050 * DRIFT_SECONDS)
    static float buf[DRIFT_N];
    bm_effects_state st;
    bm_effects       fx;
    size_t i, n = sizeof buf / sizeof buf[0];
    size_t win = (size_t)FS;              /* one second */
    double a, b, c;

    printf("ring drift\n");

    bm_effects_default(&fx);
    fx.ring    = 1.0f;
    fx.ring_hz = 62.0f;

    /* ---- off ---- */
    bm_effects_state_init(&st, FS);
    bm_effects_state_set(&st, &fx);
    for (i = 0; i < n; i++) buf[i] = bm_effects_tick(&st, 1.0f);

    a = carrier_hz(buf, win);
    b = carrier_hz(buf + n - win, win);
    printf("    drift 0:  first second %.2f Hz, last %.2f Hz\n", a, b);
    check(fabs(a - 62.0) < 1.0 && fabs(b - 62.0) < 1.0,
          "with drift off the carrier is where it was asked to be");
    check(fabs(a - b) < 0.5, "and it is still there eight seconds later");

    /* ---- on ---- */
    fx.ring_drift = 1.0f;
    bm_effects_state_init(&st, FS);
    bm_effects_state_set(&st, &fx);
    for (i = 0; i < n; i++) buf[i] = bm_effects_tick(&st, 1.0f);

    a = carrier_hz(buf, win);
    b = carrier_hz(buf + (size_t)(FS * 3.0f), win);
    c = carrier_hz(buf + (size_t)(FS * 6.0f), win);
    printf("    drift 1:  %.2f Hz at 0 s, %.2f at 3 s, %.2f at 6 s\n", a, b, c);

    /* Three seconds apart, on a cycle of about eight, so no two of these can
     * be the same point on it. Five hertz is well outside what the crossing
     * count could produce by rounding. */
    check(fabs(a - b) > 5.0 && fabs(b - c) > 5.0 && fabs(a - c) > 5.0,
          "with drift on the carrier is somewhere new at each check");

    /* It wanders rather than running away: whatever the LFO is doing, the
     * carrier stays inside the band the depth allows. */
    check(a > 62.0 * 0.6 && a < 62.0 * 1.4 &&
          b > 62.0 * 0.6 && b < 62.0 * 1.4 &&
          c > 62.0 * 0.6 && c < 62.0 * 1.4,
          "and stays within the depth it was given");

    /* And it is still a bypass when the whole struct is zero, drift included -
     * the one property the entire stage is built around. */
    bm_effects_default(&fx);
    bm_effects_state_init(&st, FS);
    bm_effects_state_set(&st, &fx);
    {
        int exact = 1;
        for (i = 0; i < 2048; i++) {
            float x = (float)sin((double)i * 0.017) * 0.5f;
            if (bm_effects_tick(&st, x) != x) exact = 0;
        }
        check(exact, "a zeroed bm_effects is still an exact bypass");
    }
}

/* Echo repeats the signal at the delay it was given.
 *
 * Driven with a single impulse into silence, so the repeats are countable
 * rather than inferred: the echo of a click is a click, and it should arrive
 * where the delay says and again at twice that. */
static void test_echo_repeats(void)
{
    bm_effects_state st;
    bm_effects       fx;
    size_t i, n = (size_t)(FS * 1.5f);
    int    d, first = -1, second = -1;

    printf("echo\n");

    bm_effects_default(&fx);
    fx.echo    = 0.8f;
    fx.echo_ms = 120.0f;
    d = (int)(0.120f * FS);

    bm_effects_state_init(&st, FS);
    bm_effects_state_set(&st, &fx);

    for (i = 0; i < n; i++) {
        float out = bm_effects_tick(&st, i == 0 ? 1.0f : 0.0f);
        a[i] = out;
        /* Anything well above the noise floor of an all-zero signal, which is
         * exactly zero - so this is only picking out the repeats. */
        if (i > 0 && out > 0.01f) {
            if (first < 0)       first = (int)i;
            else if (second < 0 && (int)i > first + d / 2) second = (int)i;
        }
    }

    printf("    delay asked for %d samples; repeats at %d and %d\n",
           d, first, second);

    /* Within a hundred samples of the requested delay, which is 4 ms - the
     * tolerance is for the integer rounding of the delay, not for the
     * mechanism. */
    check(first > d - 100 && first < d + 100, "the first repeat lands at the delay");
    check(second > 2 * d - 200 && second < 2 * d + 200,
          "and the second at twice it");

    /* Each repeat quieter than the last, or it is an oscillator. */
    check(a[first] > a[second] && a[second] > 0.0f, "and each is quieter than the last");
}

/* Reverb turns one impulse into a tail: many arrivals rather than a few, and
 * still going long after an echo of the same length would have finished.
 *
 * Counted rather than described. The difference between a reverb and a delay is
 * the density of the response, so density is what to measure. */
static void test_reverb_is_dense(void)
{
    bm_effects_state st;
    bm_effects       fx;
    size_t i, n = (size_t)(FS * 1.5f);
    int    crossings = 0;
    double early = 0.0, late = 0.0;

    printf("reverb\n");

    bm_effects_default(&fx);
    fx.reverb = 1.0f;
    fx.reverb_size = 0.7f;

    bm_effects_state_init(&st, FS);
    bm_effects_state_set(&st, &fx);

    for (i = 0; i < n; i++) {
        a[i] = bm_effects_tick(&st, i == 0 ? 1.0f : 0.0f);
        if (i > 0 && ((a[i - 1] <= 0.0f) != (a[i] <= 0.0f))) crossings++;
    }

    /* Energy in the first 100 ms against the 300-400 ms window. A tail that
     * decays is the claim; a tail that has stopped by 300 ms is a slap. */
    for (i = 0; i < (size_t)(FS * 0.1f); i++) early += (double)a[i] * a[i];
    for (i = (size_t)(FS * 0.3f); i < (size_t)(FS * 0.4f); i++)
        late += (double)a[i] * a[i];

    printf("    %d sign changes; energy 0-100 ms %.3e, 300-400 ms %.3e\n",
           crossings, early, late);

    /* Against an echo of comparable length, on the same impulse. An absolute
     * threshold here would be a guess about the damping - a tail that has lost
     * its treble legitimately crosses zero less often, which is what a warm
     * room is - so the claim is made relative to the thing a reverb has to be
     * distinguishable from. An echo is a handful of separated clicks; this
     * should be orders of magnitude busier. */
    {
        bm_effects_state e2;
        bm_effects       ef;
        int ecross = 0;

        bm_effects_default(&ef);
        ef.echo = 0.8f;
        ef.echo_ms = 120.0f;
        bm_effects_state_init(&e2, FS);
        bm_effects_state_set(&e2, &ef);
        for (i = 0; i < n; i++) {
            b[i] = bm_effects_tick(&e2, i == 0 ? 1.0f : 0.0f);
            if (i > 0 && ((b[i - 1] <= 0.0f) != (b[i] <= 0.0f))) ecross++;
        }
        printf("    an echo of the same impulse crosses zero %d times\n", ecross);
        check(crossings > ecross * 20,
              "the response is dense where an echo is a few discrete repeats");
    }
    check(late > 0.0 && late < early, "and it decays without stopping dead");

    /* Bigger room, longer tail. The size control has to do the thing its name
     * says rather than merely change something. */
    {
        double late_small = 0.0;
        fx.reverb_size = 0.0f;
        bm_effects_state_init(&st, FS);
        bm_effects_state_set(&st, &fx);
        for (i = 0; i < n; i++) b[i] = bm_effects_tick(&st, i == 0 ? 1.0f : 0.0f);
        for (i = (size_t)(FS * 0.3f); i < (size_t)(FS * 0.4f); i++)
            late_small += (double)b[i] * b[i];
        printf("    at size 0 the same window holds %.3e\n", late_small);
        check(late_small < late * 0.5, "a smaller room decays faster");
    }
}

/* Neither may be a volume control. Both add to the dry signal rather than
 * mixing against it, which is correct for what they are and is exactly the
 * arrangement that runs hot if nothing compensates. */
static void test_ambience_keeps_level(void)
{
    bm_voice   v;
    bm_effects fx;
    size_t     na, nb;
    double     dry, wet, ratio;
    int        k;
    static const char *WHAT[] = { "echo", "reverb" };

    printf("echo and reverb levels\n");

    bm_voice_default(&v);
    bm_effects_default(&fx);
    na = render(&v, &fx, "HH AH0 L OW1 SIL W ER1 L D", a, sizeof a / sizeof a[0]);
    dry = rms_of(a, na);

    for (k = 0; k < 2; k++) {
        char msg[96];
        bm_effects_default(&fx);
        if (k == 0) { fx.echo = 1.0f; fx.echo_ms = 150.0f; }
        else        { fx.reverb = 1.0f; fx.reverb_size = 0.6f; }

        nb = render(&v, &fx, "HH AH0 L OW1 SIL W ER1 L D", b, sizeof b / sizeof b[0]);
        wet = rms_of(b, nb);
        ratio = wet / dry;
        printf("    %-6s dry %.4f, wet %.4f  (%.2fx, %+.1f dB)\n",
               WHAT[k], dry, wet, ratio, 20.0 * log10(ratio));

        /* Wide, and deliberately so on the high side: both of these genuinely
         * add energy that was not there - that is what an ambience is - and the
         * requirement is that they not run away, not that they be silent. */
        snprintf(msg, sizeof msg, "%s at full stays within 4 dB of dry", WHAT[k]);
        check(ratio > 0.63 && ratio < 1.6, msg);
    }
}

static void test_presets_and_params(void)
{
    static const char *KEYS[] = {
        "ring", "ring_hz", "ring_drift", "comb", "comb_hz", "chorus", "chorus_hz",
        "drive", "crush", "echo", "echo_ms", "reverb", "reverb_size", "level"
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
    test_ring_drift();
    test_drive_adds_harmonics_not_level();
    test_crush_holds_samples();
    test_comb_resonates();
    test_chorus_detunes();
    test_echo_repeats();
    test_reverb_is_dense();
    test_ambience_keeps_level();
    test_presets_and_params();
    test_presets_are_level_matched();
#endif
    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
