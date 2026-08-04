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
    { "None",      0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.0f,  0.0f },

    /* Ring modulation alone, at a carrier low enough that the sidebands stay
     * inside the formants rather than scattering above them. This is the one
     * to listen to first: it is the effect that most obviously is not a
     * person. */
    { "Metal",     0.85f, 62.0f,  0.0f, 0.0f,   0.0f, 0.0f,  0.0f },

    /* Drive alone, so the waveshaper can be heard without anything else
     * happening. Loud in character and not in level - the trim compensates. */
    { "Overdrive", 0.0f, 0.0f,   0.0f, 0.0f,   0.72f, 0.0f,  0.0f },

    /* Sample-rate reduction alone. The aliasing is the sound. */
    { "Crushed",   0.0f, 0.0f,   0.0f, 0.0f,   0.0f, 0.62f, 0.0f },

    /* The metallic sentry. Ring modulation for the inharmonic edge, a comb
     * tuned low for the sense of a voice coming out of a chest cavity, and
     * just enough drive to harden the consonants. Deliberately less driven
     * than Enforcer: this one is meant to sound inhuman, not angry.
     *
     * The only preset carrying an output level. Ring at 0.62 sits in the dip
     * where the dry and wet paths partly cancel, and stacking a comb on top of
     * it put the whole thing 3.7 dB down - far enough that selecting it in a
     * dropdown read as a fault. 1.5 brings it back to within 0.2 dB. */
    { "Sentinel",  0.62f, 108.0f, 0.55f, 150.0f, 0.30f, 0.22f, 1.5f },

    /* The aggressive one. Drive carries it - harmonics that were not in the
     * voice are what the ear reads as force - with a tight comb for the metal
     * and only a trace of ring, because too much of it turns menace into
     * novelty. Crush left off: it makes a thing sound old, and this is not
     * supposed to sound old. */
    { "Enforcer",  0.22f, 47.0f,  0.42f, 240.0f, 0.88f, 0.0f,  0.0f }
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
    else if (key_equals("comb",    key, key_len)) effects->comb = value;
    else if (key_equals("comb_hz", key, key_len)) effects->comb_hz = value;
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
#if BM_WITH_EFFECTS
    s->comb_delay = 1u;
    s->comb_fb = 0.0f;
    s->comb_wet = 0.0f;
    s->comb_norm = 1.0f;
#endif
    bm_effects_state_reset(s);
}

void bm_effects_state_reset(bm_effects_state *s)
{
    if (s == 0) return;

    s->ring_phase = 0.0f;
    s->crush_count = 0u;
    s->crush_held = 0.0f;

#if BM_WITH_EFFECTS
    {
        int i;
        for (i = 0; i < BM_COMB_LEN; i++) s->comb_buf[i] = 0.0f;
        s->comb_at = 0u;
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
    s->p.ring_hz = bm_clampf(s->p.ring_hz, 0.0f, s->sample_rate * 0.45f);
    s->p.comb_hz = bm_clampf(s->p.comb_hz, 0.0f, s->sample_rate * 0.45f);

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

    /* Zero means unity, so that an all-zero bm_effects stays a bypass. The
     * ceiling stops a typo in a voice file from producing something that
     * deafens someone, the same way bm_synth_set_gain does. */
    s->out_level = (s->p.level > 0.0f) ? bm_clampf(s->p.level, 0.0f, 8.0f)
                                       : 1.0f;

    s->active = (s->p.ring > 0.0f && s->p.ring_hz > 0.0f) ||
                (s->p.comb > 0.0f && s->p.comb_hz > 0.0f) ||
                (s->p.drive > 0.0f) ||
                (s->crush_step > 1u) ||
                (s->out_level != 1.0f);
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
        float c = bm_sinf(BM_TWO_PI * s->ring_phase);

        s->ring_phase += s->p.ring_hz / s->sample_rate;
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

    /* ---- drive ---- */
    if (s->p.drive > 0.0f) {
        y = softclip(y * s->drive_gain) * s->drive_trim;
    }

    /* ---- sample-rate reduction ----
     *
     * Last, because it is a property of the converter rather than of the
     * signal path: whatever the chain produced, this is what a slower one
     * would have captured of it. */
    if (s->crush_step > 1u) {
        if (s->crush_count == 0u) s->crush_held = y;
        s->crush_count++;
        if (s->crush_count >= s->crush_step) s->crush_count = 0u;
        y = s->crush_held;
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
