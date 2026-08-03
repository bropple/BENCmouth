/*
 * BENCmouth - phoneme sequence to parameter frames
 * See bm_frames.h for the model.
 */

#include "bm_frames.h"
#include "bm_math.h"
#include "bm_prosody.h"
#include "bm_text.h"   /* BM_WITH_MARKUP */

#include <stddef.h>

enum {
    SEG_TRANSITION = 0,
    SEG_CLOSURE,
    SEG_BURST,
    SEG_STEADY,
    SEG_COUNT
};

/* Frication level driving a stop or affricate release. The per-phoneme
 * burst_amp values shape the spectrum; this sets how loud the release is. */
#define BM_BURST_AF_DB 36.0f

/* Duration multipliers by CMUdict stress digit: 1 primary, 2 secondary,
 * 0 unstressed. Unstressed vowels reduce noticeably in English and leaving
 * them full length is one of the loudest tells that speech is synthetic. */
#define DUR_STRESS_PRIMARY   1.25f
#define DUR_STRESS_SECONDARY 1.10f
#define DUR_STRESS_NONE      0.85f

/* The contour used when voice.prosody is 0: one linear decline across the whole
 * utterance, plus a flat bump on stressed phonemes. Superseded by bm_prosody.c
 * for any voice that asks for it, and kept exactly as it was for the ones that
 * do not - BENCmouth Retro among them. */
#define F0_DECLINATION 0.82f    /* end pitch as a fraction of start */
#define F0_STRESS_BUMP 1.06f    /* on primary-stressed phonemes */

/* Pitch chases its planned target rather than stepping to it at each phoneme
 * boundary. Real pitch cannot jump, and a stepped contour is audible as a
 * warble. ~40 ms at the nominal 100 Hz frame rate. */
#define F0_SMOOTH 0.25f

/* ------------------------------------------------------------------ */

static float stress_duration_scale(unsigned char stress, const bm_phoneme *p)
{
    if (p->cls != BM_CLS_VOWEL && p->cls != BM_CLS_DIPHTHONG) return 1.0f;

    /* No stress digit at all is not the same as a digit saying "unstressed".
     * The letter-to-sound rules emit no stress marks, so treating absence as
     * stress 0 reduced every vowel in every word and left sentences with no
     * stressed syllable anywhere - which is a large part of why longer words
     * were hard to make out. Absence means "no information", so leave the
     * nominal duration alone. */
    if (stress == BM_STRESS_UNMARKED) return 1.0f;

    if (stress == 1u) return DUR_STRESS_PRIMARY;
    if (stress == 2u) return DUR_STRESS_SECONDARY;
    return DUR_STRESS_NONE;
}

static int ms_to_frames(const bm_frame_gen *g, unsigned short ms,
                        float scale, float speed)
{
    float f;
    int   n;

    if (speed <= 0.01f) speed = 1.0f;
    f = (float)ms * scale * g->frame_rate / (1000.0f * speed);
    n = (int)(f + 0.5f);
    return (n < 1) ? 1 : n;
}

/* Markup can override speed per phoneme; otherwise the voice decides. */
static float phoneme_speed(const bm_frame_gen *g, int index)
{
    float m = g->mod[index].speed;
    return (m > 0.0f) ? m : g->voice.speed;
}

static int segment_frames(const bm_frame_gen *g, int index, int seg)
{
    const bm_phoneme *p = g->seq[index];
    float scale = stress_duration_scale(g->stress[index], p);
    float speed = phoneme_speed(g, index);
    unsigned short steady = g->mod[index].dur_ms;

    if (steady == 0u) {
        float planned = g->dur_plan[index];
        steady = p->steady_ms;
        if (planned > 0.0f && planned != 1.0f) {
            float v = (float)steady * planned;
            steady = (unsigned short)((v > 65000.0f) ? 65000.0f : v);
        }
    } else {
        /* An explicit [pause N] means N milliseconds, not N scaled by stress
         * and rate. A pause the author asked for should be the length they
         * asked for. */
        scale = 1.0f;
        speed = 1.0f;
    }

    switch (seg) {
    case SEG_TRANSITION: return ms_to_frames(g, p->transition_ms, 1.0f, speed);
    case SEG_CLOSURE:    return (p->closure_ms > 0u)
                                ? ms_to_frames(g, p->closure_ms, 1.0f, speed) : 0;
    case SEG_BURST:      return (p->burst_ms > 0u)
                                ? ms_to_frames(g, p->burst_ms, 1.0f, speed) : 0;
    default:             return ms_to_frames(g, steady, scale, speed);
    }
}

/* ------------------------------------------------------------------ */

/* How far this phoneme falls short of its own targets, pulled toward its
 * neighbours. Zero unless the voice asks for coarticulation. */
static float undershoot_weight(const bm_frame_gen *g, int index)
{
    const bm_phoneme *p = g->seq[index];
    float coart = g->voice.coarticulation;
    float dur, w;

    if (coart <= 0.0f) return 0.0f;

    /* Shorter segments undershoot more, because there is less time to travel.
     * This is exactly why unstressed syllables reduce so heavily in English,
     * and why a synthesizer that hits every target sounds over-enunciated. */
    dur = (float)p->steady_ms * stress_duration_scale(g->stress[index], p);
    if (dur < 1.0f) dur = 1.0f;

    w = 80.0f / dur;
    w = bm_clampf(w, 0.15f, 1.0f);

    /* Cap at half way to the neighbours - beyond that phonemes stop being
     * distinguishable from each other. */
    return coart * w * 0.5f;
}

/* Mean of the adjacent phonemes' targets for formant `i`, or a negative value
 * when there are no usable neighbours. */
static float neighbour_freq(const bm_frame_gen *g, int index, int i)
{
    float sum = 0.0f;
    int   n = 0;

    if (index > 0 && g->seq[index - 1]->cls != BM_CLS_SILENCE) {
        sum += g->seq[index - 1]->freq_end[i];
        n++;
    }
    if (index + 1 < g->count && g->seq[index + 1]->cls != BM_CLS_SILENCE) {
        sum += g->seq[index + 1]->freq[i];
        n++;
    }
    if (n == 0) return -1.0f;
    return sum / (float)n;
}

/* Interpolates a formant frequency, blending between linear-in-hertz and
 * geometric by `logness`.
 *
 * At 0 this is exactly what blend() always did, which is what keeps voices with
 * formant_glide off bit-identical. Above 0 it moves toward equal ratios per
 * unit time - a glide from 300 Hz to 2300 Hz passes through 830 Hz at the
 * halfway point rather than 1300 Hz, which is where the ear expects it. */
static float glide_freq(float a, float b, float t, float logness)
{
    float lin = a + (b - a) * t;

    if (logness <= 0.0f || a <= 1.0f || b <= 1.0f) return lin;

    {
        float geo = a * bm_exp2f(t * bm_log2f(b / a));
        return lin + (geo - lin) * logness;
    }
}

/* Builds the parameter target for one phoneme in one segment. `pos` is the
 * normalized position within the segment, used only by diphthongs. */
static void build_target(const bm_frame_gen *g, int index, int seg, float pos,
                         bm_frame *out)
{
    const bm_phoneme *p = g->seq[index];
    float cw = undershoot_weight(g, index);
    int   i;

    for (i = 0; i < BM_NFORMANTS; i++) {
        out->freq[i] = 0.0f;
        out->bw[i] = 100.0f;
        out->par_amp[i] = 0.0f;
    }

    for (i = 0; i < BM_PH_NTARGETS; i++) {
        float f = p->freq[i];
        if (p->cls == BM_CLS_DIPHTHONG && seg == SEG_STEADY) {
            /* Glide across the steady segment rather than jumping at its end.
             * The glide is what a diphthong *is* - and it is the longest
             * formant movement in the language, so it is where the spacing
             * matters most. */
            f = glide_freq(p->freq[i], p->freq_end[i], pos, g->voice.formant_glide);
        }
        if (cw > 0.0f) {
            float nb = neighbour_freq(g, index, i);
            if (nb > 0.0f) f += (nb - f) * cw;
        }
        out->freq[i] = f * bm_voice_formant_scale(&g->voice, i);
        out->bw[i] = p->bw[i];
    }
    out->freq[3] = BM_F4_HZ * bm_voice_formant_scale(&g->voice, 3);
    out->bw[3] = BM_F4_BW;
    out->freq[4] = BM_F5_HZ * bm_voice_formant_scale(&g->voice, 4);
    out->bw[4] = BM_F5_BW;

    out->open_quotient = g->voice.open_quotient;
    out->tilt = g->voice.tilt;

    /* Nasal pole and zero coincide unless the phoneme is nasal, and a
     * coincident pair cancels exactly - so this is how the branch switches
     * off without needing a flag. */
    out->nasal_pole_f  = BM_NASAL_POLE_HZ;
    out->nasal_pole_bw = BM_NASAL_BW;
    if (p->nasal_zero_f > 0.0f) {
        /* The nasal zero is an oral-cavity feature, so it tracks the same
         * scaling as the mid formants rather than the pharyngeal axis. */
        out->nasal_zero_f  = p->nasal_zero_f * bm_voice_formant_scale(&g->voice, 1);
        out->nasal_zero_bw = BM_NASAL_BW;
    } else {
        out->nasal_zero_f  = BM_NASAL_POLE_HZ;
        out->nasal_zero_bw = BM_NASAL_BW;
    }

    out->f0 = 0.0f;   /* filled in globally by the caller */

    switch (seg) {
    case SEG_CLOSURE:
        /* Occluded: no airflow at the lips. Voiced stops keep a faint voice
         * bar, which is most of what distinguishes /b/ from /p/. */
        out->av = p->av;
        out->ah = 0.0f;
        out->af = 0.0f;
        out->par_bypass = 0.0f;
        break;

    case SEG_BURST:
        out->av = p->av;
        out->ah = 0.0f;
        out->af = BM_BURST_AF_DB;
        for (i = 0; i < BM_NFORMANTS; i++) out->par_amp[i] = p->burst_amp[i];
        out->par_bypass = p->burst_bypass;
        break;

    default:
        out->av = p->av;
        out->ah = p->ah;
        out->af = p->af;
        for (i = 0; i < BM_NFORMANTS; i++) out->par_amp[i] = p->par_amp[i];
        out->par_bypass = p->par_bypass;
        /* Breathiness rides on top of whatever aspiration the phoneme has. */
        if (out->av > 0.0f && g->voice.breathiness > 0.0f) {
            float b = out->av - 40.0f + g->voice.breathiness;
            if (b > out->ah) out->ah = b;
        }
        break;
    }
}

static float smoothstep(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    /* Zero first derivative at both ends, so a transition neither starts nor
     * stops abruptly. Linear interpolation puts a velocity discontinuity at
     * every phoneme boundary and it is audible as a faint tick. */
    return t * t * (3.0f - 2.0f * t);
}

static void blend(const bm_frame *a, const bm_frame *b, float t,
                  float logness, bm_frame *out)
{
    int i;

    for (i = 0; i < BM_NFORMANTS; i++) {
        out->freq[i] = glide_freq(a->freq[i], b->freq[i], t, logness);
        out->bw[i]   = a->bw[i]   + (b->bw[i]   - a->bw[i])   * t;
        out->par_amp[i] = a->par_amp[i] + (b->par_amp[i] - a->par_amp[i]) * t;
    }
    out->av = a->av + (b->av - a->av) * t;
    out->ah = a->ah + (b->ah - a->ah) * t;
    out->af = a->af + (b->af - a->af) * t;
    out->par_bypass = a->par_bypass + (b->par_bypass - a->par_bypass) * t;

    out->open_quotient = a->open_quotient + (b->open_quotient - a->open_quotient) * t;
    out->tilt = a->tilt + (b->tilt - a->tilt) * t;

    out->nasal_pole_f  = a->nasal_pole_f  + (b->nasal_pole_f  - a->nasal_pole_f)  * t;
    out->nasal_pole_bw = a->nasal_pole_bw + (b->nasal_pole_bw - a->nasal_pole_bw) * t;
    out->nasal_zero_f  = a->nasal_zero_f  + (b->nasal_zero_f  - a->nasal_zero_f)  * t;
    out->nasal_zero_bw = a->nasal_zero_bw + (b->nasal_zero_bw - a->nasal_zero_bw) * t;

    out->f0 = a->f0 + (b->f0 - a->f0) * t;
}

/* ------------------------------------------------------------------ */

void bm_frame_gen_init(bm_frame_gen *g, float frame_rate, const bm_voice *voice)
{
    if (g == 0) return;

    g->frame_rate = (frame_rate > 0.0f) ? frame_rate : 100.0f;
    if (voice != 0) {
        g->voice = *voice;
    } else {
        bm_voice_default(&g->voice);
    }
    g->count = 0;
    bm_frame_gen_reset(g);
}

void bm_frame_gen_reset(bm_frame_gen *g)
{
    int   i;

    if (g == 0) return;

    g->index = 0;
    g->segment = SEG_TRANSITION;
    g->frame_in_seg = 0;
    g->frames_done = 0;
    g->f0_smooth = 0.0f;
    g->f0_started = 0;

    /* Start from silence at a neutral vocal tract, so the first transition
     * glides out of rest rather than starting mid-articulation.
     *
     * The rest position is scaled by formant_scale like every other target: a
     * longer vocal tract is longer at rest too, and leaving it unscaled would
     * make every utterance open with a transition from the wrong speaker. */
    for (i = 0; i < BM_NFORMANTS; i++) {
        g->last.bw[i] = 100.0f;
        g->last.par_amp[i] = 0.0f;
    }
    g->last.freq[0] =  500.0f * bm_voice_formant_scale(&g->voice, 0);
    g->last.freq[1] = 1500.0f * bm_voice_formant_scale(&g->voice, 1);
    g->last.freq[2] = 2500.0f * bm_voice_formant_scale(&g->voice, 2);
    g->last.freq[3] = BM_F4_HZ * bm_voice_formant_scale(&g->voice, 3);
    g->last.freq[4] = BM_F5_HZ * bm_voice_formant_scale(&g->voice, 4);
    g->last.av = 0.0f;
    g->last.ah = 0.0f;
    g->last.af = 0.0f;
    g->last.par_bypass = 0.0f;
    g->last.open_quotient = g->voice.open_quotient;
    g->last.tilt = g->voice.tilt;
    g->last.nasal_pole_f = BM_NASAL_POLE_HZ;
    g->last.nasal_pole_bw = BM_NASAL_BW;
    g->last.nasal_zero_f = BM_NASAL_POLE_HZ;
    g->last.nasal_zero_bw = BM_NASAL_BW;
    g->last.f0 = g->voice.f0_base;

    g->from = g->last;
    g->to = g->last;
}


/* ------------------------------------------------------------------ *
 * Inline markup
 *
 * Commands ride in the phoneme stream as bracketed tokens, so `bm -t` shows
 * them and bm_speak_phonemes() honours them. They set state that applies to
 * every phoneme after them until changed or reset.
 * ------------------------------------------------------------------ */

#if BM_WITH_MARKUP

static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* No strtod in the core, and it would be overkill anyway - these are knob
 * values typed by a person, not scientific notation. */
static float parse_number(const char *s, size_t len, int *ok)
{
    float v = 0.0f, frac = 0.1f;
    size_t i = 0;
    int    dot = 0, digits = 0, neg = 0;

    while (i < len && is_ws(s[i])) i++;
    if (i < len && s[i] == '-') { neg = 1; i++; }

    for (; i < len; i++) {
        if (s[i] == '.' && !dot) { dot = 1; continue; }
        if (s[i] < '0' || s[i] > '9') break;
        digits++;
        if (!dot) {
            v = v * 10.0f + (float)(s[i] - '0');
        } else {
            v += (float)(s[i] - '0') * frac;
            frac *= 0.1f;
        }
    }
    while (i < len && is_ws(s[i])) i++;

    *ok = (digits > 0 && i == len);
    return neg ? -v : v;
}

static int word_is(const char *s, size_t len, const char *word)
{
    size_t i;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (word[i] == '\0' || word[i] != c) return 0;
    }
    return word[i] == '\0';
}

/* Parses one command's contents (between the brackets). Sets `cur` for
 * subsequent phonemes, or reports a pause to insert here. */
static bm_result parse_command(const char *s, size_t len,
                               bm_phoneme_mod *cur, unsigned *pause_ms)
{
    size_t k = 0, name_end;
    int    ok = 0;
    float  value;

    *pause_ms = 0u;

    while (k < len && is_ws(s[k])) k++;
    name_end = k;
    while (name_end < len && !is_ws(s[name_end])) name_end++;
    if (name_end == k) return BM_ERR_ARG;

    if (word_is(s + k, name_end - k, "reset")) {
        cur->f0 = 0.0f;
        cur->speed = 0.0f;
        cur->dur_ms = 0u;
        return BM_OK;
    }

    value = parse_number(s + name_end, len - name_end, &ok);
    /* An unparseable or missing argument is an error rather than a default.
     * "[pitch]" almost certainly means the author mistyped something, and
     * quietly ignoring it produces speech that is subtly not what they
     * asked for. */
    if (!ok) return BM_ERR_ARG;

    if (word_is(s + k, name_end - k, "pitch")) {
        if (value < 20.0f || value > 500.0f) return BM_ERR_ARG;
        cur->f0 = value;
    } else if (word_is(s + k, name_end - k, "speed")) {
        if (value < 0.1f || value > 10.0f) return BM_ERR_ARG;
        cur->speed = value;
    } else if (word_is(s + k, name_end - k, "pause")) {
        if (value < 0.0f || value > 10000.0f) return BM_ERR_ARG;
        *pause_ms = (unsigned)value;
    } else {
        return BM_ERR_UNSUPPORTED;
    }
    return BM_OK;
}

#endif /* BM_WITH_MARKUP */

bm_result bm_frame_gen_set_phonemes(bm_frame_gen *g, const char *phonemes,
                                    size_t len)
{
    bm_phoneme_mod cur;
    size_t i = 0;
    int    total = 0, k, s;

    if (g == 0 || phonemes == 0) return BM_ERR_ARG;

    if (len == 0) {
        while (phonemes[len] != '\0') len++;
    }

    g->count = 0;
    cur.f0 = 0.0f;
    cur.speed = 0.0f;
    cur.dur_ms = 0u;

    while (i < len) {
        size_t start;
        unsigned char stress = BM_STRESS_UNMARKED;
        const bm_phoneme *p;

        while (i < len && (phonemes[i] == ' ' || phonemes[i] == '\t' ||
                           phonemes[i] == '\n' || phonemes[i] == '\r')) i++;
        if (i >= len) break;

        if (phonemes[i] == '[') {
#if BM_WITH_MARKUP
            size_t   cs = ++i;
            unsigned pause_ms = 0u;
            bm_result rc;

            /* Scan to the closing bracket regardless of whitespace: a command
             * such as "[pitch 90]" spans what would otherwise be two tokens. */
            while (i < len && phonemes[i] != ']') i++;
            if (i >= len) return BM_ERR_ARG;      /* unterminated */

            rc = parse_command(phonemes + cs, i - cs, &cur, &pause_ms);
            if (rc != BM_OK) return rc;
            i++;                                  /* past the ']' */

            if (pause_ms > 0u) {
                if (g->count >= BM_MAX_PHONEMES) return BM_ERR_OVERFLOW;
                g->seq[g->count] = bm_phoneme_silence();
                g->stress[g->count] = BM_STRESS_UNMARKED;
                g->mod[g->count] = cur;
                g->mod[g->count].dur_ms = (unsigned short)pause_ms;
                g->count++;
            }
            continue;
#else
            /* Markup compiled out, so a bracket cannot mean anything. Saying
             * so beats silently speaking the command as if it were phonemes. */
            return BM_ERR_UNSUPPORTED;
#endif
        }

        start = i;
        while (i < len && phonemes[i] != ' ' && phonemes[i] != '\t' &&
               phonemes[i] != '\n' && phonemes[i] != '\r') {
            if (phonemes[i] >= '0' && phonemes[i] <= '9') {
                stress = (unsigned char)(phonemes[i] - '0');
            }
            i++;
        }

        p = bm_phoneme_lookup(phonemes + start, i - start);
        if (p == 0) return BM_ERR_UNSUPPORTED;

        if (g->count >= BM_MAX_PHONEMES) return BM_ERR_OVERFLOW;
        g->seq[g->count] = p;
        g->stress[g->count] = stress;
        g->mod[g->count] = cur;
        g->mod[g->count].dur_ms = 0u;   /* only [pause] sets an absolute length */
        g->count++;
    }

    if (g->count == 0) return BM_ERR_ARG;

    /* Plan before measuring the total, because the plan can lengthen phrase
     * final syllables and the frame count has to include that. */
    bm_prosody_plan(g->seq, g->stress, g->count, &g->voice,
                    g->f0_plan, g->dur_plan);

    bm_frame_gen_reset(g);

    for (k = 0; k < g->count; k++) {
        for (s = 0; s < SEG_COUNT; s++) total += segment_frames(g, k, s);
    }
    g->frames_total = total;

    return BM_OK;
}

int bm_frame_gen_length(const bm_frame_gen *g)
{
    return (g == 0) ? 0 : g->frames_total;
}

int bm_frame_gen_next(bm_frame_gen *g, bm_frame *out)
{
    if (g == 0 || out == 0) return 0;

    while (g->index < g->count) {
        int n = segment_frames(g, g->index, g->segment);

        if (g->frame_in_seg >= n) {
            g->frame_in_seg = 0;
            g->segment++;
            if (g->segment >= SEG_COUNT) {
                g->segment = SEG_TRANSITION;
                g->index++;
            }
            continue;
        }

        if (g->segment == SEG_TRANSITION) {
            if (g->frame_in_seg == 0) {
                g->from = g->last;
                build_target(g, g->index, SEG_STEADY, 0.0f, &g->to);
            }
            blend(&g->from, &g->to, smoothstep((float)g->frame_in_seg / (float)n),
                  g->voice.formant_glide, out);
        } else {
            float pos = (n > 1) ? (float)g->frame_in_seg / (float)(n - 1) : 0.0f;
            build_target(g, g->index, g->segment, pos, out);
        }

        /* Pitch is applied globally rather than per phoneme, so the contour
         * stays smooth across boundaries instead of being interpolated
         * piecewise from phoneme targets that know nothing about each other. */
        if (g->voice.prosody > 0.0f) {
            /* Follow the planned contour, smoothed. Markup pitch does not
             * replace the contour, it transposes it - otherwise [pitch 90]
             * would flatten the intonation of everything after it. */
            float target = g->f0_plan[g->index];
            if (g->mod[g->index].f0 > 0.0f && g->voice.f0_base > 1.0f) {
                target *= g->mod[g->index].f0 / g->voice.f0_base;
            }
            if (!g->f0_started) { g->f0_smooth = target; g->f0_started = 1; }
            g->f0_smooth += F0_SMOOTH * (target - g->f0_smooth);
            out->f0 = g->f0_smooth;
        } else {
            /* Pre-bm_prosody.c contour, preserved verbatim for voices that do
             * not opt in. Note it is a function of elapsed frames, not phoneme
             * index, so it cannot simply be expressed as a plan. */
            float t = (g->frames_total > 1)
                    ? (float)g->frames_done / (float)(g->frames_total - 1)
                    : 0.0f;
            float base = (g->mod[g->index].f0 > 0.0f)
                       ? g->mod[g->index].f0 : g->voice.f0_base;
            float f0 = base * (1.0f + (F0_DECLINATION - 1.0f) * t);
            if (g->stress[g->index] == 1u) f0 *= F0_STRESS_BUMP;
            out->f0 = f0;
        }

        g->last = *out;
        g->frame_in_seg++;
        g->frames_done++;
        return 1;
    }

    return 0;
}
