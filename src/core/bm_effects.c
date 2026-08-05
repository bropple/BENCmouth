/*
 * BENCmouth - post-synthesis effects
 * See bm_effects.h and the bm_effects comment in bencmouth.h.
 */

#include "bm_effects.h"
#include "bm_math.h"

#include <stddef.h>

/* ------------------------------------------------------------------ *
 * Presets
 *
 * Named for what they do rather than for what they are imitating. Each one is
 * either a single effect at a useful setting - so the knob can be understood by
 * hearing it alone - or a combination that needed all of its parts.
 * ------------------------------------------------------------------ */

static const bm_effects BM_EFFECT_PRESETS[] = {
    /* The bypass. Present as a named entry so a dropdown has something to
     * return to, and so "no effects" is a choice rather than the absence of
     * one. */
    { "None",      0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  0.0f },

    /* Ring modulation alone, at a carrier low enough that the sidebands stay
     * inside the formants rather than scattering above them. This is the one
     * to listen to first: it is the effect that most obviously is not a
     * person. */
    { "Metal",     0.85f, 62.0f, 0.0f,  0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  0.0f },

    /* Drive alone, so the waveshaper can be heard without anything else
     * happening. Loud in character and not in level - the trim compensates. */
    { "Overdrive", 0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,   0.72f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  0.0f },

    /* Sample-rate reduction alone. The aliasing is the sound. */
    { "Crushed",   0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.62f, 0.0f, 0.0f, 0.0f, 0.0f,  0.0f },

    /* The metallic sentry. Ring modulation for the inharmonic edge, a comb
     * tuned low for the sense of a voice coming out of a chest cavity, and
     * just enough drive to harden the consonants. Deliberately less driven
     * than Enforcer: this one is meant to sound inhuman, not angry.
     *
     * The only preset carrying an output level. Ring at 0.62 sits in the dip
     * where the dry and wet paths partly cancel, and stacking a comb on top of
     * it put the whole thing 3.7 dB down - far enough that selecting it in a
     * dropdown read as a fault. 1.5 brings it back to within 0.2 dB. */
    { "Sentinel",  0.62f, 108.0f, 0.0f, 0.55f, 150.0f, 0.0f, 0.0f,   0.30f, 0.22f, 0.0f, 0.0f, 0.0f, 0.0f,  1.5f },

    /* The aggressive one. Drive carries it - harmonics that were not in the
     * voice are what the ear reads as force - with a tight comb for the metal
     * and only a trace of ring, because too much of it turns menace into
     * novelty. Crush left off: it makes a thing sound old, and this is not
     * supposed to sound old. */
    { "Enforcer",  0.22f, 47.0f, 0.0f,  0.42f, 240.0f, 0.0f, 0.0f,   0.88f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  0.0f },

    /* Three of it. A modulated delay is a pitch shift, so three taps swept a
     * third of a cycle apart really are three detuned copies - which a fixed
     * comb is not, and is why the comb sounds like a tube instead. Slow rate:
     * above about 1 Hz a chorus stops sounding like several voices and starts
     * sounding like one voice being wobbled, which is a different effect and
     * already available as vibrato. */
    { "Trinode",   0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.85f, 0.42f, 0.10f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  1.4f },

    /* ---- chains that arrived with a voice -------------------------------
     *
     * These six were written as part of a voice rather than on their own - see
     * bm_voice.c, where each is paired with the preset it belongs to. They are
     * listed here as well because a chain is not specific to the voice it was
     * built for: the comb that gives Carillon its case rings any voice put
     * through it, and being able to try one on somebody else is most of what
     * an effects menu is for.
     *
     * Named for the chain, not for the voice, for the same reason. */

    /* A small resonant body. One comb, spaced wide enough that its teeth land
     * between the formants rather than on them, so it colours without
     * swallowing the vowels. */
    { "Chamber",   0.0f, 0.0f, 0.0f,   0.30f, 190.0f, 0.0f, 0.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  1.0f },

    /* A voice arriving over a bad link. The crush is the sample rate of the
     * channel, the high comb is the pipe it came down, and the ring at 33 Hz is
     * below pitch - too low to be heard as a tone, so it reads as the thing
     * being modulated rather than as a modulator. */
    { "Downlink",  0.18f, 33.0f, 0.0f, 0.60f, 520.0f, 0.0f, 0.0f,  0.20f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f,  1.2f },

    /* Ring modulation almost to the exclusion of the dry signal, at a carrier
     * just under a typical fundamental. Harder than Metal and deliberately
     * plainer - no comb, so nothing gives it a body to be inside. */
    { "Alloy",     0.90f, 74.0f, 0.0f, 0.0f, 0.0f,    0.0f, 0.0f,  0.15f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  1.3f },

    /* Amplified and slightly broken. The comb is tuned high, where a small horn
     * resonates, and the crush and drive between them do what a cheap speaker
     * driven too hard does to a voice. */
    { "Bullhorn",  0.0f, 0.0f, 0.0f,   0.50f, 700.0f, 0.0f, 0.0f,  0.60f, 0.38f, 0.0f, 0.0f, 0.0f, 0.0f,  1.05f },

    /* A low comb and nothing else: the wooden case an instrument sits in, which
     * is a resonance and not a distortion. The gentlest entry in this table. */
    { "Cabinet",   0.0f, 0.0f, 0.0f,   0.35f, 105.0f, 0.0f, 0.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.0f, 0.0f,  1.0f },

    /* The harsh one. Ring at 92 Hz against a fundamental near 104 puts the
     * sidebands close enough to the pitch to beat against it rather than
     * scatter - a buzz rather than a shimmer - and the comb and drive stacked on
     * top are what make it sound loud before the level control touches it. */
    { "Klaxon",    0.95f, 92.0f, 0.0f, 0.45f, 168.0f, 0.0f, 0.0f,  0.35f, 0.20f, 0.0f, 0.0f, 0.0f, 0.0f,  1.15f },

    /* ---- the space around it ------------------------------------------ */

    /* A large room and nothing else. Deliberately the plainest entry in the
     * table: reverb is the one effect people already have an ear for, so it is
     * worth being able to hear it on its own without anything else explaining
     * it away. Size well up but short of the top, where the tail starts
     * outlasting the sentence. */
    { "Hall",      0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,  0.0f,  0.0f,
                   0.0f, 0.0f,   0.55f, 0.85f,  1.0f },

    /* Outdoors, and a long way from anything. A slow echo for the distance and
     * a large room underneath it for what the repeats are bouncing off - one
     * without the other is either a delay pedal or a cathedral, and the pair is
     * what reads as open country. The echo time is deliberately past the point
     * where repeats belong to the syllable that caused them. */
    { "Canyon",    0.0f, 0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,  0.0f,  0.0f,
                   0.62f, 330.0f, 0.30f, 0.75f,  1.1f }
};

#define BM_EFFECT_COUNT \
    ((int)(sizeof BM_EFFECT_PRESETS / sizeof BM_EFFECT_PRESETS[0]))

/* ------------------------------------------------------------------ */

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int key_equals(const char *a, const char *b, size_t blen)
{
    size_t i;

    for (i = 0; i < blen; i++) {
        if (a[i] == '\0') return 0;
        if (lower(a[i]) != lower(b[i])) return 0;
    }
    return a[i] == '\0';
}

/* Matched the same loose way voice names are: spaces, hyphens and underscores
 * ignored, so a name typed into a file does not fail over punctuation. */
static int name_matches(const char *full, const char *query)
{
    size_t fi = 0, qi = 0;

    for (;;) {
        while (full[fi] == ' ' || full[fi] == '-' || full[fi] == '_') fi++;
        while (query[qi] == ' ' || query[qi] == '-' || query[qi] == '_') qi++;

        if (query[qi] == '\0') return full[fi] == '\0';
        if (full[fi] == '\0') return 0;
        if (lower(full[fi]) != lower(query[qi])) return 0;
        fi++;
        qi++;
    }
}

void bm_effects_default(bm_effects *effects)
{
    if (effects == 0) return;
    *effects = BM_EFFECT_PRESETS[0];       /* "None" */
}

const bm_effects *bm_effects_preset(const char *name)
{
    int i;

    if (name == 0) return 0;
    for (i = 0; i < BM_EFFECT_COUNT; i++) {
        if (name_matches(BM_EFFECT_PRESETS[i].name, name)) {
            return &BM_EFFECT_PRESETS[i];
        }
    }
    return 0;
}

int bm_effects_preset_count(void)
{
    return BM_EFFECT_COUNT;
}

const bm_effects *bm_effects_preset_at(int index)
{
    if (index < 0 || index >= BM_EFFECT_COUNT) return 0;
    return &BM_EFFECT_PRESETS[index];
}

bm_result bm_effects_set_param(bm_effects *effects, const char *key,
                               size_t key_len, float value)
{
    if (effects == 0 || key == 0) return BM_ERR_ARG;

    if (key_len == 0) {
        while (key[key_len] != '\0') key_len++;
    }
    if (key_len == 0) return BM_ERR_ARG;

    if      (key_equals("ring",    key, key_len)) effects->ring = value;
    else if (key_equals("ring_hz", key, key_len)) effects->ring_hz = value;
    else if (key_equals("ring_drift", key, key_len)) effects->ring_drift = value;
    else if (key_equals("echo",    key, key_len)) effects->echo = value;
    else if (key_equals("echo_ms", key, key_len)) effects->echo_ms = value;
    else if (key_equals("reverb",  key, key_len)) effects->reverb = value;
    else if (key_equals("reverb_size", key, key_len)) effects->reverb_size = value;
    else if (key_equals("comb",    key, key_len)) effects->comb = value;
    else if (key_equals("comb_hz", key, key_len)) effects->comb_hz = value;
    else if (key_equals("chorus",  key, key_len)) effects->chorus = value;
    else if (key_equals("chorus_hz",key, key_len)) effects->chorus_hz = value;
    else if (key_equals("drive",   key, key_len)) effects->drive = value;
    else if (key_equals("crush",   key, key_len)) effects->crush = value;
    else if (key_equals("level",   key, key_len)) effects->level = value;
    else return BM_ERR_ARG;

    return BM_OK;
}

/* ------------------------------------------------------------------ *
 * The chain
 * ------------------------------------------------------------------ */

/* Highest pre-gain the waveshaper reaches, as a linear multiplier. 25 is about
 * 28 dB, which folds even quiet vowels hard - past this the difference stops
 * being audible because everything is already clipped flat. */
#define BM_DRIVE_MAX_GAIN 25.0f

/* Deepest decimation, as a hold length in samples. At 22050 Hz a hold of 12
 * puts the effective rate near 1.8 kHz, which is already below the second
 * formant of most vowels - further than this stops being an effect and starts
 * being unintelligible. */
#define BM_CRUSH_MAX_HOLD 12u

/* Chorus geometry, in milliseconds.
 *
 * The centre delay has to be long enough that the copies are heard as separate
 * voices rather than as comb filtering of one - below about 10 ms the ear
 * fuses them and what is left is a flanger. The sweep either side is what does
 * the detuning: a delay changing by D samples per sample shifts pitch by a
 * factor of (1 - D), so 3 ms of sweep at half a hertz is a few cents, which is
 * what a section of singers is out by. */
#define BM_CHORUS_BASE_MS  16.0f
#define BM_CHORUS_DEPTH_MS  3.2f
#define BM_CHORUS_DEFAULT_HZ 0.5f

/* Three, which is what makes this the detuned-chorus voice rather than a
 * generic wobble. Spread evenly around the LFO cycle so no two are ever at the
 * same delay. */
#define BM_CHORUS_TAPS 3

/* 1/sqrt(BM_CHORUS_TAPS) - see the comment where it is used. */
#define BM_CHORUS_NORM 0.5774f

/* Carrier drift.
 *
 * Rate first, because it is the part that matters and the part that is easy to
 * get wrong. Above about half a hertz a moving carrier stops being a drift and
 * becomes a wobble - a rhythm of its own, competing with the syllables - and
 * the ear locks onto that instead, which is the same failure in a different
 * costume. This is one cycle every eight seconds, slower than any sentence is
 * long, so within a single utterance the carrier does not repeat itself at all.
 *
 * The odd value rather than 0.125 is deliberate: at exactly an eighth of a
 * hertz the drift would come back into step with anything else periodic in the
 * signal, and the one thing this must never do is settle.
 *
 * Depth of 0.35 at full setting, so a 62 Hz carrier travels between 40 and 84.
 * That is wide enough to carry it through the simple ratios against a typical
 * fundamental - at 120 Hz, past both the half and the two-thirds - and each
 * crossing is a different sideband pattern rather than a louder one. */
#define BM_RING_DRIFT_HZ    0.119f
#define BM_RING_DRIFT_DEPTH 0.35f

/* Echo.
 *
 * The default spacing is 180 ms because that is roughly where a repeat stops
 * being heard as part of the word and starts being heard as a copy of it - far
 * enough to be a separate event, near enough to still belong to the syllable
 * that caused it.
 *
 * Feedback tops out at 0.72. Above about 0.8 the tail outlasts the sentence
 * that produced it and the next one arrives over the top of it; below 0.5 there
 * is effectively one repeat and the control stops meaning anything. */
#define BM_ECHO_DEFAULT_MS 180.0f
#define BM_ECHO_MIN_MS      20.0f
#define BM_ECHO_MAX_FB       0.72f

/* Reverb line lengths in samples at 22050 Hz, scaled with the rate and clamped
 * to what the block holds.
 *
 * Mutually prime, and that is the whole design. Four combs whose lengths shared
 * a factor would put their repeats on top of each other at the common multiple,
 * and a reverb whose echoes coincide has a pitch - the exact thing a tail must
 * not have. The allpasses are much shorter: their job is to disperse a single
 * impulse across a few milliseconds, not to add time.
 *
 * These sum to 3067, which is what BM_REVERB_LEN is 3072 for. */
#if BM_WITH_EFFECTS
static const unsigned BM_VERB_LEN[BM_REVERB_LINES] = {
    557, 617, 673, 743,     /* combs     - 25 to 34 ms */
    241, 89                 /* allpasses - 11 and 4 ms */
};
#endif

/* Feedback at reverb_size 0 and 1: a small tiled room against a hall. Not taken
 * to 1 - a comb at unity feedback never decays, and the difference between a
 * very long reverb and a broken one is that the broken one keeps going. */
#define BM_VERB_FB_MIN 0.70f
#define BM_VERB_FB_MAX 0.92f

/* How much of each comb's feedback is lowpassed on the way round. Real rooms
 * absorb treble faster than bass - it is why a hall sounds warm and a tiled
 * bathroom does not - and without this the tail keeps its brightness all the
 * way down and rings metallically. */
#define BM_VERB_DAMP 0.38f

/* The allpass coefficient. 0.5 is Schroeder's, and it is not a free parameter
 * in the way it looks: an allpass is only flat when the feed-forward and
 * feedback gains match, and any other value trades that flatness for nothing in
 * particular. */
#define BM_VERB_AP 0.5f

/* Four combs summed and then twice diffused arrive far louder than the input.
 * Measured against dry rather than derived: at reverb 1 and size 0.6 the wet
 * path came back 17 dB hot, and this is what brings it level. */
#define BM_VERB_GAIN 0.135f

void bm_effects_state_init(bm_effects_state *s, float sample_rate)
{
    if (s == 0) return;

    s->sample_rate = (sample_rate > 0.0f) ? sample_rate : 1.0f;
    bm_effects_default(&s->p);
    s->active = 0;
    s->drive_gain = 1.0f;
    s->drive_trim = 1.0f;
    s->crush_step = 1u;
    s->out_level = 1.0f;
    s->ring_drift = 0.0f;
    s->ring_lfo_inc = 0.0f;
#if BM_WITH_EFFECTS
    s->comb_delay = 1u;
    s->comb_fb = 0.0f;
    s->comb_wet = 0.0f;
    s->comb_norm = 1.0f;
    s->chorus_wet = 0.0f;
    s->chorus_base = 1.0f;
    s->chorus_depth = 0.0f;
    s->chorus_inc = 0.0f;
    s->echo_delay = 1u;
    s->echo_fb = 0.0f;
    s->echo_wet = 0.0f;
    s->echo_trim = 1.0f;
    s->verb_fb = 0.0f;
    s->verb_wet = 0.0f;
#endif
    bm_effects_state_reset(s);
}

void bm_effects_state_reset(bm_effects_state *s)
{
    if (s == 0) return;

    s->ring_phase = 0.0f;
    /* A quarter turn in, so an utterance starts with the carrier moving at its
     * fastest rather than sitting at a turning point. Starting at zero would
     * give every utterance the same near-stationary first second, which is the
     * part of it anyone judges. */
    s->ring_lfo = 0.25f;
    s->crush_count = 0u;
    s->crush_held = 0.0f;

#if BM_WITH_EFFECTS
    {
        int i;
        for (i = 0; i < BM_COMB_LEN; i++) s->comb_buf[i] = 0.0f;
        s->comb_at = 0u;
        for (i = 0; i < BM_CHORUS_LEN; i++) s->chorus_buf[i] = 0.0f;
        s->chorus_at = 0u;
        s->chorus_phase = 0.0f;
        for (i = 0; i < BM_ECHO_LEN; i++) s->echo_buf[i] = 0.0f;
        s->echo_at = 0u;
        for (i = 0; i < BM_REVERB_LEN; i++) s->verb_buf[i] = 0.0f;
        for (i = 0; i < BM_REVERB_LINES; i++) s->verb_at[i] = 0u;
        for (i = 0; i < BM_REVERB_COMBS; i++) s->verb_damp_z[i] = 0.0f;
    }
#endif
}

void bm_effects_state_set(bm_effects_state *s, const bm_effects *e)
{
    if (s == 0 || e == 0) return;

    s->p = *e;

    s->p.ring    = bm_clampf(s->p.ring, 0.0f, 1.0f);
    s->p.comb    = bm_clampf(s->p.comb, 0.0f, 1.0f);
    s->p.drive   = bm_clampf(s->p.drive, 0.0f, 1.0f);
    s->p.crush   = bm_clampf(s->p.crush, 0.0f, 1.0f);
    s->p.chorus  = bm_clampf(s->p.chorus, 0.0f, 1.0f);
    s->p.ring_hz = bm_clampf(s->p.ring_hz, 0.0f, s->sample_rate * 0.45f);
    s->p.ring_drift = bm_clampf(s->p.ring_drift, 0.0f, 1.0f);
    s->p.echo   = bm_clampf(s->p.echo, 0.0f, 1.0f);
    s->p.reverb = bm_clampf(s->p.reverb, 0.0f, 1.0f);
    s->p.reverb_size = bm_clampf(s->p.reverb_size, 0.0f, 1.0f);
    s->p.comb_hz = bm_clampf(s->p.comb_hz, 0.0f, s->sample_rate * 0.45f);
    s->p.chorus_hz = bm_clampf(s->p.chorus_hz, 0.0f, 12.0f);

#if BM_WITH_EFFECTS
    /* Delay in samples for the requested resonance spacing. Clamped to the
     * line: a comb below what the buffer can hold would otherwise wrap and
     * resonate at some unrelated frequency, which is worse than not
     * resonating. */
    if (s->p.comb_hz > 0.0f) {
        float d = s->sample_rate / s->p.comb_hz;
        if (d < 2.0f) d = 2.0f;
        if (d > (float)(BM_COMB_LEN - 1)) d = (float)(BM_COMB_LEN - 1);
        s->comb_delay = (unsigned)d;
    } else {
        s->comb_delay = 1u;
    }

    /* One knob sets both the mix and the feedback, because they are not
     * independently interesting: a comb with mix and no feedback is a flanger
     * notch, and with feedback and no mix is inaudible. What a person wants
     * from this control is "more metal".
     *
     * A feedback comb has a peak gain of 1/(1 - fb) at resonance, so the *wet
     * signal* is scaled by (1 - fb) to bring its teeth back to unity. That
     * scaling belongs on the wet path and not on the mix - folding it into the
     * mix instead was the first version, and at full setting it left the output
     * 78% dry, so the comb measured only 2 dB of tooth-to-notch depth where it
     * should have had 13. The feedback still reads the unnormalized value, or
     * the resonance would decay away. */
    s->comb_fb  = s->p.comb * 0.78f;
    s->comb_wet = s->p.comb;
    s->comb_norm = 1.0f - s->comb_fb;

    /* One knob again, for the same reason: depth without mix is inaudible and
     * mix without depth is three copies of the same delay, which is a comb.
     * The base delay is fixed - it decides whether the copies are heard as
     * separate voices, and that is not a taste setting. */
    {
        float base  = BM_CHORUS_BASE_MS  * 0.001f * s->sample_rate;
        float depth = BM_CHORUS_DEPTH_MS * 0.001f * s->sample_rate * s->p.chorus;

        /* Keep the deepest excursion inside the line, with a sample spare for
         * the interpolator to read behind. */
        if (base + depth > (float)(BM_CHORUS_LEN - 2)) {
            base = (float)(BM_CHORUS_LEN - 2) - depth;
        }
        if (base < 2.0f) base = 2.0f;
        if (depth > base - 2.0f) depth = base - 2.0f;

        s->chorus_base  = base;
        s->chorus_depth = depth;
        s->chorus_wet   = s->p.chorus;
        s->chorus_inc   = ((s->p.chorus_hz > 0.0f) ? s->p.chorus_hz
                                                   : BM_CHORUS_DEFAULT_HZ)
                        / s->sample_rate;
    }
#endif

    /* Ring modulation spreads a component into two sidebands, which costs
     * about 3 dB, and the loss is worst around half wet where the dry and wet
     * paths partly cancel - measured RMS ran 0.089 dry, 0.052 at 0.6 wet, 0.063
     * at full. A knob labelled with a timbre should not also be a volume
     * control, so this puts most of it back. It is a fit, not an identity: the
     * residual is about 2 dB at the dip. */
    s->ring_trim = 1.0f + 0.5f * s->p.ring;

    /* Pre-gain into the shaper, and a trim back out.
     *
     * The trim is emphatically not 1/gain. The shaper is linear for small
     * signals, so 1/gain would be right for the quiet parts and would bury the
     * loud ones - at full drive it renders the voice 13 dB below where it
     * started. Nor is a straight line right: measured RMS jumps 2.6x by a drive
     * of only 0.2 and then *falls* as compression takes over, so the correction
     * needs a sharp knee near zero and almost none after.
     *
     * Fitted against rendered RMS at six drive settings, holding the level flat
     * to within 3%. The fourth root is what gives the knee, and it is two
     * square roots rather than a pow() the core does not have. */
    s->drive_gain = 1.0f + s->p.drive * (BM_DRIVE_MAX_GAIN - 1.0f);
    s->drive_trim = 1.0f /
        (1.0f + 4.5f * bm_sqrtf(bm_sqrtf(s->p.drive)));

    s->crush_step = 1u + (unsigned)(s->p.crush * (float)(BM_CRUSH_MAX_HOLD - 1u)
                                    + 0.5f);

    /* Resolved here rather than per sample, and left at zero when the drift is
     * off so the carrier costs exactly what it always did - one add and one
     * compare - for everybody not using this. */
    s->ring_drift   = s->p.ring_drift * BM_RING_DRIFT_DEPTH;
    s->ring_lfo_inc = (s->p.ring_drift > 0.0f)
                    ? BM_RING_DRIFT_HZ / s->sample_rate : 0.0f;

#if BM_WITH_EFFECTS
    /* ---- echo ---- */
    {
        float ms = (s->p.echo_ms > 0.0f) ? s->p.echo_ms : BM_ECHO_DEFAULT_MS;
        float d;

        if (ms < BM_ECHO_MIN_MS) ms = BM_ECHO_MIN_MS;
        d = ms * 0.001f * s->sample_rate;
        if (d < 1.0f) d = 1.0f;
        if (d > (float)(BM_ECHO_LEN - 1)) d = (float)(BM_ECHO_LEN - 1);
        s->echo_delay = (unsigned)d;

        s->echo_fb  = s->p.echo * BM_ECHO_MAX_FB;
        s->echo_wet = s->p.echo;

        /* The dry path is untouched and the repeats are added on top, which is
         * what an echo is - so the sum runs hot in proportion to how much tail
         * there is. A geometric series of wet*fb^n sums to wet/(1-fb); this is
         * that, softened, because the repeats are spread across time rather
         * than arriving together and the peak never sees the whole series. */
        s->echo_trim = 1.0f / (1.0f + s->echo_wet * s->echo_fb * 1.6f);
    }

    /* ---- reverb ---- */
    {
        float scale = s->sample_rate / 22050.0f;
        unsigned off = 0;
        int k;

        for (k = 0; k < BM_REVERB_LINES; k++) {
            float want = (float)BM_VERB_LEN[k] * scale;
            unsigned len = (unsigned)(want < 1.0f ? 1.0f : want);

            /* Clamped to what is left, so a high sample rate gives a smaller
             * room rather than reading off the end of the block. */
            if (off + len > (unsigned)BM_REVERB_LEN) {
                len = (unsigned)BM_REVERB_LEN - off;
                if (len < 1u) len = 1u;
            }
            s->verb_off[k] = off;
            s->verb_len[k] = len;
            off += len;
            if (off > (unsigned)BM_REVERB_LEN) off = (unsigned)BM_REVERB_LEN;
        }

        s->verb_fb  = BM_VERB_FB_MIN +
                      s->p.reverb_size * (BM_VERB_FB_MAX - BM_VERB_FB_MIN);
        s->verb_wet = s->p.reverb;
    }
#endif

    /* Zero means unity, so that an all-zero bm_effects stays a bypass. The
     * ceiling stops a typo in a voice file from producing something that
     * deafens someone, the same way bm_synth_set_gain does. */
    s->out_level = (s->p.level > 0.0f) ? bm_clampf(s->p.level, 0.0f, 8.0f)
                                       : 1.0f;

    s->active = (s->p.ring > 0.0f && s->p.ring_hz > 0.0f) ||
                (s->p.comb > 0.0f && s->p.comb_hz > 0.0f) ||
                (s->p.chorus > 0.0f) ||
                (s->p.drive > 0.0f) ||
                (s->crush_step > 1u) ||
                (s->p.echo > 0.0f) ||
                (s->p.reverb > 0.0f) ||
                (s->out_level != 1.0f);
}

/* Time for a feedback loop of gain `fb`, going round every `per_ms`, to fall by
 * 40 dB. Not 60: the last twenty decibels of a tail are below anything audible
 * over the next sentence, and chasing them would double the length of every
 * rendered file for silence nobody hears. */
static float decay_ms(float per_ms, float fb)
{
    float db_per_loop;

    if (fb <= 0.0f) return 0.0f;
    if (fb >= 0.999f) return (float)BM_EFFECTS_TAIL_MAX_MS;

    /* log10 from the core's log2, which is the only logarithm it has. */
    db_per_loop = -20.0f * bm_log2f(fb) * 0.301030f;
    if (db_per_loop <= 0.01f) return (float)BM_EFFECTS_TAIL_MAX_MS;

    return per_ms * (40.0f / db_per_loop);
}

unsigned bm_effects_tail_ms(const bm_effects *e)
{
    float ms = 0.0f;

    if (e == 0) return 0u;

#if BM_WITH_EFFECTS
    if (e->echo > 0.0f) {
        float per = (e->echo_ms > 0.0f) ? e->echo_ms : BM_ECHO_DEFAULT_MS;
        float t;
        if (per < BM_ECHO_MIN_MS) per = BM_ECHO_MIN_MS;
        t = decay_ms(per, bm_clampf(e->echo, 0.0f, 1.0f) * BM_ECHO_MAX_FB);
        if (t > ms) ms = t;
    }

    if (e->reverb > 0.0f) {
        /* The longest comb sets the decay; the shorter ones are already quiet
         * by the time it is. Its loop time is the same in milliseconds at any
         * sample rate, because the line lengths scale with the rate. */
        float per = (float)BM_VERB_LEN[BM_REVERB_COMBS - 1] * 1000.0f / 22050.0f;
        float fb  = BM_VERB_FB_MIN + bm_clampf(e->reverb_size, 0.0f, 1.0f) *
                                     (BM_VERB_FB_MAX - BM_VERB_FB_MIN);
        /* Damping takes treble out of the loop every pass, so the real tail is
         * shorter than the feedback alone predicts. Over-estimating is the safe
         * direction - it renders silence, where under-estimating audibly
         * chops. */
        float t = decay_ms(per, fb);
        if (t > ms) ms = t;
    }

    /* The comb rings too, but its loop is a few milliseconds, so 40 dB of decay
     * fits inside the tail the engine already rendered. Left out rather than
     * added and clamped to nothing. */
#else
    (void)decay_ms;
#endif

    if (ms > (float)BM_EFFECTS_TAIL_MAX_MS) ms = (float)BM_EFFECTS_TAIL_MAX_MS;
    return (unsigned)ms;
}

#if BM_WITH_EFFECTS

/* Cubic soft clip. f(x) = x - x^3/3 up to |x| = 1, flat at +-2/3 beyond.
 *
 * Chosen over a hyperbolic tangent because the core has no libm and this is
 * two multiplies. It is also the more useful curve here: it stays linear for
 * small signals, so the quiet parts of a driven voice are still the quiet
 * parts, and folds sharply near the limit, which is where the harmonics that
 * make it sound aggressive come from. */
static float softclip(float x)
{
    if (x >=  1.0f) return  2.0f / 3.0f;
    if (x <= -1.0f) return -2.0f / 3.0f;
    return x - x * x * x * (1.0f / 3.0f);
}

float bm_effects_tick(bm_effects_state *s, float x)
{
    float y = x;

    if (s == 0 || !s->active) return x;

    /* ---- ring modulation ---- */
    if (s->p.ring > 0.0f && s->p.ring_hz > 0.0f) {
        float c  = bm_sinf(BM_TWO_PI * s->ring_phase);
        float hz = s->p.ring_hz;

        /* The carrier wanders, when asked to. Modulating the *frequency* and
         * accumulating phase from it, rather than modulating the phase
         * directly: phase modulation would move the carrier and then put it
         * back where it started, and a carrier that returns to its old value
         * every cycle is one the ear can still learn. */
        if (s->ring_lfo_inc > 0.0f) {
            hz += hz * s->ring_drift * bm_sinf(BM_TWO_PI * s->ring_lfo);
            s->ring_lfo += s->ring_lfo_inc;
            if (s->ring_lfo >= 1.0f) s->ring_lfo -= 1.0f;
        }

        s->ring_phase += hz / s->sample_rate;
        if (s->ring_phase >= 1.0f) s->ring_phase -= 1.0f;

        y = (y + (y * c - y) * s->p.ring) * s->ring_trim;
    }

    /* ---- resonant comb ---- */
    if (s->comb_wet > 0.0f) {
        unsigned r = (s->comb_at - s->comb_delay) & (unsigned)(BM_COMB_LEN - 1);
        float    v = y + s->comb_fb * s->comb_buf[r];

        s->comb_buf[s->comb_at] = v;
        s->comb_at = (s->comb_at + 1u) & (unsigned)(BM_COMB_LEN - 1);

        y = y + (v * s->comb_norm - y) * s->comb_wet;
    }

    /* ---- chorus ----
     *
     * Before the drive: modulation after distortion smears the harmonics the
     * distortion just made, which is mud. Every guitar rig puts modulation
     * ahead of the amp for the same reason. */
    if (s->chorus_wet > 0.0f) {
        float sum = 0.0f;
        int   t;

        s->chorus_buf[s->chorus_at] = y;

        for (t = 0; t < BM_CHORUS_TAPS; t++) {
            /* Evenly spread around the cycle, so no two taps ever sit at the
             * same delay and the copies stay distinct. */
            float ph = s->chorus_phase + (float)t / (float)BM_CHORUS_TAPS;
            float d, frac;
            unsigned i0, i1;
            int      w;

            while (ph >= 1.0f) ph -= 1.0f;
            d = s->chorus_base + s->chorus_depth * bm_sinf(BM_TWO_PI * ph);

            /* Interpolated, and this is not optional: a delay that jumps whole
             * samples as it sweeps produces a zipper of small discontinuities
             * where the smooth pitch shift is supposed to be. */
            w    = (int)d;
            frac = d - (float)w;
            i0 = (s->chorus_at - (unsigned)w)       & (unsigned)(BM_CHORUS_LEN - 1);
            i1 = (s->chorus_at - (unsigned)(w + 1)) & (unsigned)(BM_CHORUS_LEN - 1);

            sum += s->chorus_buf[i0] +
                   (s->chorus_buf[i1] - s->chorus_buf[i0]) * frac;
        }

        s->chorus_at = (s->chorus_at + 1u) & (unsigned)(BM_CHORUS_LEN - 1);
        s->chorus_phase += s->chorus_inc;
        if (s->chorus_phase >= 1.0f) s->chorus_phase -= 1.0f;

        /* Divided by the square root of the tap count, not by the tap count.
         * The copies are detuned, so they sum incoherently - three of them are
         * sqrt(3) louder than one, not 3 times - and dividing by 3 measured
         * 6.4 dB down against dry, which is a volume control wearing a timbre
         * knob's label. */
        sum *= BM_CHORUS_NORM;
        y = y + (sum - y) * s->chorus_wet;
    }

    /* ---- drive ---- */
    if (s->p.drive > 0.0f) {
        y = softclip(y * s->drive_gain) * s->drive_trim;
    }

    /* ---- sample-rate reduction ----
     *
     * The last thing done to the signal itself, because it is a property of the
     * converter rather than of the signal path: whatever the chain produced,
     * this is what a slower one would have captured of it. What follows is not
     * done to the voice but to the space around it. */
    if (s->crush_step > 1u) {
        if (s->crush_count == 0u) s->crush_held = y;
        s->crush_count++;
        if (s->crush_count >= s->crush_step) s->crush_count = 0u;
        y = s->crush_held;
    }

    /* ---- echo ----
     *
     * After the distortion, which is the order every rig is wired in and for a
     * reason that is audible: distorting an echo squashes the repeats up
     * against the dry signal until the tail is as loud as the voice, whereas
     * echoing a distorted signal repeats something already finished. The first
     * arrangement sounds like a fault.
     *
     * The delayed sample is *added* rather than mixed against the dry, because
     * an echo is not a wet/dry blend - the original arrives unaltered and the
     * copies come afterwards. That is also why this needs a trim and the comb
     * does not. */
    if (s->echo_wet > 0.0f) {
        unsigned r = (s->echo_at + BM_ECHO_LEN - s->echo_delay) &
                     (unsigned)(BM_ECHO_LEN - 1);
        float    d = s->echo_buf[r];

        s->echo_buf[s->echo_at] = y + s->echo_fb * d;
        s->echo_at = (s->echo_at + 1u) & (unsigned)(BM_ECHO_LEN - 1);

        y = (y + s->echo_wet * d) * s->echo_trim;
    }

    /* ---- reverb ----
     *
     * Last of everything, because a room is the outermost thing: a voice goes
     * into a room, a room does not go into a voice. Put anywhere earlier and
     * the later stages act on the tail as well as on the words - drive after
     * reverb pumps the whole room in time with the syllables, which is a known
     * and unpleasant effect and not this one.
     *
     * Four combs in parallel, summed, then two allpasses in series. The combs
     * make repeats; the allpasses smear each repeat across a few milliseconds
     * so they stop being countable. Neither alone is a reverb - combs by
     * themselves are four flutter echoes, allpasses by themselves are a
     * dispersion with nothing to disperse. */
    if (s->verb_wet > 0.0f) {
        float in = y * BM_VERB_GAIN;
        float acc = 0.0f;
        int   k;

        for (k = 0; k < BM_REVERB_COMBS; k++) {
            float *line = s->verb_buf + s->verb_off[k];
            unsigned at = s->verb_at[k];
            float    out = line[at];

            /* One-pole lowpass inside the feedback loop. A room absorbs treble
             * faster than bass, so each pass round the loop has to come back
             * duller than it went in; without this the tail keeps its
             * brightness all the way down and rings like a pipe. */
            s->verb_damp_z[k] = out * (1.0f - BM_VERB_DAMP) +
                                s->verb_damp_z[k] * BM_VERB_DAMP;
            line[at] = in + s->verb_damp_z[k] * s->verb_fb;

            at++;
            if (at >= s->verb_len[k]) at = 0u;
            s->verb_at[k] = at;

            acc += out;
        }

        for (k = BM_REVERB_COMBS; k < BM_REVERB_LINES; k++) {
            float *line = s->verb_buf + s->verb_off[k];
            unsigned at = s->verb_at[k];
            float    buf = line[at];
            float    out = buf - acc;

            line[at] = acc + buf * BM_VERB_AP;

            at++;
            if (at >= s->verb_len[k]) at = 0u;
            s->verb_at[k] = at;

            acc = out;
        }

        /* Added, like the echo and for the same reason: the dry voice is not
         * replaced by the room it is standing in. */
        y = y + acc * s->verb_wet;
    }

    return y * s->out_level;
}

#else /* !BM_WITH_EFFECTS */

/* The stage is compiled out. The parameters are still accepted and still
 * stored - a build without effects should reject a voice file no differently
 * from one with them - they simply do nothing. */
float bm_effects_tick(bm_effects_state *s, float x)
{
    (void)s;
    return x;
}

#endif /* BM_WITH_EFFECTS */
