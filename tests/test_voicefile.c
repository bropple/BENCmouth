/*
 * BENCmouth - the shipped voice files against the presets they duplicate
 *
 * Ten voices exist twice: once as an entry in BM_PRESETS, so the GUI dropdown
 * and `bm -l` can offer them, and once as a file in voices/, which is where the
 * working-out for each one is written down and what people copy when they write
 * their own. Two copies of the same numbers is a thing that drifts, and this is
 * what stops it: every voice file whose name matches a preset has to
 * produce that preset exactly, chain included.
 *
 * It is a file-reading test, which the rest of the suite is not, so it runs
 * from the repository root - the only place `make test` invokes it from. If the
 * directory is not there, that is a failure and not a skip: a drift guard that
 * quietly passes when it cannot find anything to check is worse than no guard.
 *
 * The loader is host code rather than library code, so it comes in as source.
 * See tests/test_song.c for the same arrangement.
 */

#include "../src/host/bm_voicefile.c"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* Both structs are a name pointer followed by a block of floats, in the order
 * their set_param accepts. Reading them positionally means this test does not
 * carry its own copy of the field list to fall out of date. */
static const char *VOICE_KEYS[] = {
    "f0_base", "f0_range", "f0_flutter", "vibrato", "vibrato_rate", "source",
    "speed", "throat", "mouth", "breathiness", "tilt", "open_quotient",
    "whisper", "gain", "coarticulation", "prosody", "formant_glide",
    "bandwidth_track", "flatten"
};

static const char *FX_KEYS[] = {
    "ring", "ring_hz", "ring_drift", "comb", "comb_hz", "chorus", "chorus_hz",
    "drive", "crush", "level"
};

#define NVOICE_KEYS ((int)(sizeof VOICE_KEYS / sizeof VOICE_KEYS[0]))
#define NFX_KEYS    ((int)(sizeof FX_KEYS / sizeof FX_KEYS[0]))

/* A stored level of 0 means unity, and so does 1. A file is free to write
 * either, so neither the file nor the preset is wrong for disagreeing here -
 * this is the one place where two different numbers are the same setting. */
static float unity(float level)
{
    return level <= 0.0f ? 1.0f : level;
}

static int floats_match(const float *a, const float *b, int n,
                        const char **keys, const char *who, const char *what)
{
    int i, ok = 1;

    for (i = 0; i < n; i++) {
        float x = a[i], y = b[i];
        if (strcmp(keys[i], "level") == 0) { x = unity(x); y = unity(y); }
        /* Exact. Both sides are decimal literals that went through the same
         * strtod-to-float or compiler conversion, so anything but equality
         * means somebody edited one copy. */
        if (x != y) {
            printf("    %s: %s %s = %g in the file, %g in the preset\n",
                   who, what, keys[i], (double)x, (double)y);
            ok = 0;
        }
    }
    return ok;
}

static void test_files_match_presets(void)
{
    int i, checked = 0;

    for (i = 0; i < bm_voice_preset_count(); i++) {
        const bm_voice   *preset = bm_voice_preset_at(i);
        const char       *chain  = bm_voice_chain(preset);
        const bm_effects *want_fx;
        bm_config   config;
        bm_voice    voice;
        bm_effects  effects;
        char        path[256], name[64], err[256], what[128];
        FILE       *probe;

        snprintf(path, sizeof path, "voices/%s.voice", preset->name);
        probe = fopen(path, "r");
        if (probe == 0) continue;      /* most presets have no file, and need none */
        fclose(probe);

        /* Start where the CLI starts, so a key the file leaves out is compared
         * against the same default the file would inherit in real use. */
        bm_config_default(&config);
        voice   = config.voice;
        effects = config.effects;

        err[0] = '\0';
        snprintf(what, sizeof what, "%s.voice loads", preset->name);
        if (bm_voicefile_load(path, &voice, &effects, name, sizeof name,
                              err, sizeof err) != 0) {
            printf("    %s\n", err);
            check(0, what);
            continue;
        }
        checked++;

        snprintf(what, sizeof what, "%s.voice names itself the same", preset->name);
        check(voice.name != 0 && strcmp(voice.name, preset->name) == 0, what);

        snprintf(what, sizeof what, "%s.voice matches the preset", preset->name);
        check(floats_match((const float *)(const void *)&voice.f0_base,
                           (const float *)(const void *)&preset->f0_base,
                           NVOICE_KEYS, VOICE_KEYS, preset->name, "voice"), what);

        want_fx = (chain != 0) ? bm_effects_preset(chain) : bm_effects_preset("None");
        snprintf(what, sizeof what, "%s.voice carries the %s chain", preset->name,
                 chain != 0 ? chain : "None");
        if (want_fx == 0) {
            printf("    no effects preset named \"%s\"\n", chain);
            check(0, what);
            continue;
        }
        check(floats_match((const float *)(const void *)&effects.ring,
                           (const float *)(const void *)&want_fx->ring,
                           NFX_KEYS, FX_KEYS, preset->name, "chain"), what);
    }

    /* Ten of them at the time of writing. The count is asserted loosely - a new
     * paired voice should not have to edit this - but not at zero, because zero
     * is what a wrong working directory looks like and it would otherwise pass. */
    check(checked >= 10, "found the shipped voice files at all");
}

/* Every chain named by a voice has to exist. A typo here would otherwise show
 * up only as a voice that quietly plays dry. */
static void test_chains_resolve(void)
{
    int i, ok = 1;

    for (i = 0; i < bm_voice_preset_count(); i++) {
        const bm_voice *v = bm_voice_preset_at(i);
        const char *chain = bm_voice_chain(v);
        if (chain != 0 && bm_effects_preset(chain) == 0) {
            printf("    %s asks for chain \"%s\", which does not exist\n",
                   v->name, chain);
            ok = 0;
        }
    }
    check(ok, "every chain a voice names is a real effects preset");
}

/* The pairing is looked up by name, so two presets sharing one would make the
 * second unreachable. */
static void test_preset_names_are_unique(void)
{
    int i, j, ok = 1;

    for (i = 0; i < bm_voice_preset_count(); i++) {
        for (j = i + 1; j < bm_voice_preset_count(); j++) {
            if (strcmp(bm_voice_preset_at(i)->name,
                       bm_voice_preset_at(j)->name) == 0) {
                printf("    two presets are called \"%s\"\n",
                       bm_voice_preset_at(i)->name);
                ok = 0;
            }
        }
    }
    check(ok, "no two presets share a name");

    ok = 1;
    for (i = 0; i < bm_effects_preset_count(); i++) {
        for (j = i + 1; j < bm_effects_preset_count(); j++) {
            if (strcmp(bm_effects_preset_at(i)->name,
                       bm_effects_preset_at(j)->name) == 0) {
                printf("    two chains are called \"%s\"\n",
                       bm_effects_preset_at(i)->name);
                ok = 0;
            }
        }
    }
    check(ok, "no two effects presets share a name");
}

/* A preset must be findable by the name it displays. bm_voice_preset matches
 * loosely and falls back to a suffix, and a name that collides with another's
 * tail would resolve to the wrong entry - "Sentry" and "Sentinel" are close
 * enough to be worth asserting rather than assuming. */
static void test_presets_find_themselves(void)
{
    int i, ok = 1;

    for (i = 0; i < bm_voice_preset_count(); i++) {
        const bm_voice *v = bm_voice_preset_at(i);
        if (bm_voice_preset(v->name) != v) {
            printf("    \"%s\" looks up as \"%s\"\n", v->name,
                   bm_voice_preset(v->name) != 0
                       ? bm_voice_preset(v->name)->name : "(nothing)");
            ok = 0;
        }
    }
    check(ok, "every preset resolves to itself by name");
}

int main(void)
{
    printf("\nBENCmouth voice file tests\n\n");
    test_files_match_presets();
    test_chains_resolve();
    test_preset_names_are_unique();
    test_presets_find_themselves();
    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
