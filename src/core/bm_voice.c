/*
 * BENCmouth - voices, presets, and configuration
 *
 * These numbers plus the phoneme table are the entire personality of the
 * synthesizer. Everything else is mechanism.
 *
 * On BENCmouth Retro: it is the original voice, and it is pinned. As the
 * synthesizer gains naturalness, every such feature arrives as a parameter
 * whose off setting reproduces the older behaviour, and Retro leaves them off.
 * tests/test_voices.c holds it to that with a golden reference, because a
 * preset that drifts is not a preset - it is just the current defaults wearing
 * an old name.
 */

#include "bencmouth.h"

#include <stddef.h>
#include <stdint.h>

/* How much of each formant is governed by the throat rather than the mouth.
 * F1 tracks the pharyngeal cavity almost entirely, F3 and above track the oral
 * cavity, and F2 answers to both - which is why it carries so much of the cue
 * to vowel identity. */
static const float BM_THROAT_WEIGHT[BM_NFORMANTS] = {
    1.00f, 0.35f, 0.10f, 0.00f, 0.00f
};

/* ------------------------------------------------------------------ */

static const bm_voice BM_PRESETS[] = {
    /* The default voice. Identical to Retro except that coarticulation is on -
     * which is exactly how naturalness features are meant to arrive: a new
     * preset that switches them on, never an edit to Retro. As more of them
     * land, this is the entry that changes. */
    { "BENCmouth",
      118.0f, 4.0f, 0.30f, 1.0f,
      1.0f, 1.0f,
      0.0f, 6.0f, 0.50f, 1.0f,
      0.60f, 0.85f, 1.0f },

    /* BENCmouth Retro - the original voice.
     *
     * Every naturalness control sits at its off setting. Do not "improve"
     * these values; add a new preset instead. The whole point of this entry is
     * that it still sounds like this in five years. */
    { "BENCmouth Retro",
      118.0f,   /* f0_base       */
      4.0f,     /* f0_range      */
      0.30f,    /* f0_flutter    */
      1.0f,     /* speed         */
      1.0f,     /* throat        */
      1.0f,     /* mouth         */
      0.0f,     /* breathiness   */
      6.0f,     /* tilt          */
      0.50f,    /* open_quotient */
      1.0f,     /* gain          */
      0.0f,     /* coarticulation - off, and staying off */
      0.0f,     /* prosody       - likewise */
      0.0f },   /* formant_glide - likewise */

    /* Retro taken further: no flutter, no intonation. The gain trim is not
     * cosmetic - with flutter at zero the pulse train is perfectly periodic,
     * so the cascade resonators are excited in lockstep and peaks stack up.
     * At unity this voice hit 0.96 where the others sit near 0.55. */
    { "BENCmouth Monotone",
      120.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 1.0f,
      0.0f, 4.0f, 0.50f, 0.62f,
      0.0f, 0.0f, 0.0f },

    /* Larger speaker. The tuning here is deliberate: an earlier version
     * dropped f0 to 92 with only a slight tract change and was heard as Retro
     * pitched down rather than as somebody else. Pitch is a weaker speaker cue
     * than vocal tract length, so this pulls the pitch drop back and pushes the
     * cavities much further - a bigger person, not a slower tape. */
    /* Deep and Bright are meant to read as different people, not as retro
     * variants, so they carry the naturalness controls the default voice does.
     * Only the BENCmouth Retro family keeps them at zero. */
    { "Deep",
      100.0f, 4.5f, 0.35f, 0.93f,
      0.78f, 0.84f,
      0.0f, 9.0f, 0.58f, 1.0f,
      0.60f, 0.85f, 1.0f },

    /* Smaller speaker: shorter tract, higher pitch, brighter because less
     * spectral tilt. */
    { "Bright",
      168.0f, 5.5f, 0.30f, 1.05f,
      1.10f, 1.16f,
      0.0f, 3.0f, 0.46f, 1.0f,
      0.60f, 0.90f, 1.0f }
};

#define BM_PRESET_COUNT ((int)(sizeof BM_PRESETS / sizeof BM_PRESETS[0]))

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

/* Preset names are matched loosely - spaces, hyphens and underscores are
 * ignored - so "BENCmouth Retro", "bencmouth-retro" and "retro" all resolve.
 * A voice file people hand-edit should not fail over punctuation. */
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

/* Also accept the distinctive tail of a preset name, so "retro" finds
 * "BENCmouth Retro" without the user typing the prefix every time. */
static int name_matches_suffix(const char *full, const char *query)
{
    size_t fi = 0;

    while (full[fi] != '\0') {
        if (fi == 0 || full[fi - 1] == ' ' || full[fi - 1] == '-') {
            if (name_matches(full + fi, query)) return 1;
        }
        fi++;
    }
    return 0;
}

const bm_voice *bm_voice_preset(const char *name)
{
    int i;

    if (name == 0) return 0;

    for (i = 0; i < BM_PRESET_COUNT; i++) {
        if (name_matches(BM_PRESETS[i].name, name)) return &BM_PRESETS[i];
    }
    for (i = 0; i < BM_PRESET_COUNT; i++) {
        if (name_matches_suffix(BM_PRESETS[i].name, name)) return &BM_PRESETS[i];
    }
    return 0;
}

int bm_voice_preset_count(void)
{
    return BM_PRESET_COUNT;
}

const bm_voice *bm_voice_preset_at(int index)
{
    if (index < 0 || index >= BM_PRESET_COUNT) return 0;
    return &BM_PRESETS[index];
}

void bm_voice_default(bm_voice *voice)
{
    if (voice == 0) return;
    *voice = BM_PRESETS[0];   /* "BENCmouth" - see the table above */
}

float bm_voice_formant_scale(const bm_voice *voice, int index)
{
    float w, throat, mouth;

    if (voice == 0 || index < 0 || index >= BM_NFORMANTS) return 1.0f;

    throat = (voice->throat > 0.1f) ? voice->throat : 1.0f;
    mouth  = (voice->mouth  > 0.1f) ? voice->mouth  : 1.0f;

    w = BM_THROAT_WEIGHT[index];
    return throat * w + mouth * (1.0f - w);
}

/* xorshift32: deterministic, tiny, and the core links no rand(). */
static uint32_t rnd_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rnd_range(uint32_t *state, float lo, float hi)
{
    /* Top bits only - xorshift's low bits are its weakest. */
    float u = (float)(rnd_next(state) >> 8) * (1.0f / 16777216.0f);
    return lo + (hi - lo) * u;
}

void bm_voice_random(bm_voice *voice, uint32_t seed)
{
    uint32_t st = (seed != 0u) ? seed : 0x9E3779B9u;
    int i;

    if (voice == 0) return;

    /* Warm up: consecutive seeds otherwise produce visibly similar first
     * draws, and "seed 1 and seed 2 sound the same" defeats the point. */
    for (i = 0; i < 8; i++) (void)rnd_next(&st);

    voice->name = "Random";

    voice->f0_base       = rnd_range(&st, 82.0f, 215.0f);
    voice->f0_range      = rnd_range(&st, 2.0f, 7.0f);
    voice->f0_flutter    = rnd_range(&st, 0.12f, 0.50f);
    voice->speed         = rnd_range(&st, 0.85f, 1.20f);

    voice->throat        = rnd_range(&st, 0.74f, 1.22f);
    voice->mouth         = rnd_range(&st, 0.80f, 1.26f);

    voice->breathiness   = rnd_range(&st, 0.0f, 8.0f);
    voice->tilt          = rnd_range(&st, 2.0f, 12.0f);
    voice->open_quotient = rnd_range(&st, 0.42f, 0.62f);

    /* Trim by flutter rather than picking a gain at random. Low flutter means
     * a near-periodic pulse train, the cascade resonators ring in lockstep and
     * peaks stack up - the effect that sent BENCmouth Monotone to 0.96 where
     * flutter-bearing voices sat near 0.55. Without this a random voice would
     * hit the limiter roughly a third of the time. */
    {
        float f = voice->f0_flutter / 0.35f;
        if (f > 1.0f) f = 1.0f;
        voice->gain = 0.72f + 0.28f * f;
    }

    voice->coarticulation = rnd_range(&st, 0.35f, 0.80f);
    voice->prosody        = rnd_range(&st, 0.55f, 1.00f);
    voice->formant_glide  = rnd_range(&st, 0.40f, 1.00f);
}

bm_result bm_voice_set_param(bm_voice *voice, const char *key, size_t key_len,
                             float value)
{
    if (voice == 0 || key == 0) return BM_ERR_ARG;

    if (key_len == 0) {
        while (key[key_len] != '\0') key_len++;
    }
    if (key_len == 0) return BM_ERR_ARG;

    if      (key_equals("f0_base",        key, key_len)) voice->f0_base = value;
    else if (key_equals("f0_range",       key, key_len)) voice->f0_range = value;
    else if (key_equals("f0_flutter",     key, key_len)) voice->f0_flutter = value;
    else if (key_equals("speed",          key, key_len)) voice->speed = value;
    else if (key_equals("throat",         key, key_len)) voice->throat = value;
    else if (key_equals("mouth",          key, key_len)) voice->mouth = value;
    else if (key_equals("breathiness",    key, key_len)) voice->breathiness = value;
    else if (key_equals("tilt",           key, key_len)) voice->tilt = value;
    else if (key_equals("open_quotient",  key, key_len)) voice->open_quotient = value;
    else if (key_equals("gain",           key, key_len)) voice->gain = value;
    else if (key_equals("coarticulation", key, key_len)) voice->coarticulation = value;
    else if (key_equals("prosody",        key, key_len)) voice->prosody = value;
    else if (key_equals("formant_glide",  key, key_len)) voice->formant_glide = value;
    else return BM_ERR_ARG;

    return BM_OK;
}

/* ------------------------------------------------------------------ */

void bm_config_default(bm_config *config)
{
    if (config == 0) return;

    /* 22050 comfortably clears the 4.5 kHz fifth formant while staying cheap.
     * Below about 16 kHz the top formant folds and vowels lose their edge. */
    config->sample_rate = 22050u;
    config->frame_rate = 100u;
    config->markup = 0;      /* brackets are ordinary text unless asked for */
    bm_voice_default(&config->voice);
}

const char *bm_strerror(bm_result result)
{
    switch (result) {
    case BM_OK:              return "ok";
    case BM_ERR_ARG:         return "invalid argument";
    case BM_ERR_OVERFLOW:    return "input exceeds a compile-time capacity";
    case BM_ERR_BUSY:        return "still speaking";
    case BM_ERR_UNSUPPORTED: return "unsupported or unknown phoneme";
    }
    return "unknown error";
}
