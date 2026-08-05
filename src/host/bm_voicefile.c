/*
 * BENCmouth - voice file loading
 * See bm_voicefile.h for the format.
 */

#include "bm_voicefile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 256

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim(char *s)
{
    char *end;

    /* Leading skip must include newlines, so that a blank line reduces to the
     * empty string and is recognised as one. */
    while (is_space(*s)) s++;
    if (*s == '\0') return s;

    /* `end >= s` rather than `end > s`: the earlier return guarantees s points
     * at a non-space, so this terminates, and the stricter bound would leave
     * the final character untrimmed. */
    end = s + strlen(s) - 1;
    while (end >= s && is_space(*end)) *end-- = '\0';
    return s;
}

int bm_voicefile_load(const char *path, bm_voice *voice, bm_effects *effects,
                      char *name_buf, size_t name_cap,
                      char *err, size_t err_cap)
{
    FILE *f;
    char  line[MAX_LINE];
    int   lineno = 0;

    if (path == 0 || voice == 0 || effects == 0) return -1;

    f = fopen(path, "r");
    if (f == 0) {
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }

    while (fgets(line, sizeof line, f) != 0) {
        char *key, *value, *eq, *hash;

        lineno++;

        /* Comments run to end of line, and a comment may follow a value. */
        hash = strchr(line, '#');
        if (hash != 0) *hash = '\0';

        key = trim(line);
        if (*key == '\0') continue;

        eq = strchr(key, '=');
        if (eq == 0) {
            if (err != 0) snprintf(err, err_cap, "line %d: expected key = value", lineno);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        value = trim(eq + 1);
        key = trim(key);

        if (strcmp(key, "name") == 0) {
            if (name_buf != 0 && name_cap > 0) {
                strncpy(name_buf, value, name_cap - 1);
                name_buf[name_cap - 1] = '\0';
                voice->name = name_buf;
            }
            continue;
        }

        if (strcmp(key, "effects") == 0) {
            const bm_effects *fx = bm_effects_preset(value);
            if (fx == 0) {
                if (err != 0) snprintf(err, err_cap,
                                       "line %d: unknown effects preset \"%s\"",
                                       lineno, value);
                fclose(f);
                return -1;
            }
            *effects = *fx;
            continue;
        }

        if (strcmp(key, "preset") == 0) {
            const bm_voice *p = bm_voice_preset(value);
            const char *keep = voice->name;
            int named = (name_buf != 0 && name_buf[0] != '\0' &&
                         voice->name == name_buf);

            if (p == 0) {
                if (err != 0) snprintf(err, err_cap, "line %d: unknown preset \"%s\"",
                                       lineno, value);
                fclose(f);
                return -1;
            }
            *voice = *p;
            /* A preset is a whole bm_voice, name included, so copying one over
             * a voice that has already been named throws the name away. Every
             * shipped file happens to put `name` first, which is the natural
             * order to write them in, so this made `preset = retro` silently
             * rename the voice to "BENCmouth Retro" - Gravel.bmvoice announced
             * itself as Retro for as long as it existed. The inherited numbers
             * are the point of the key; the inherited name never is. */
            if (named) voice->name = keep;
            continue;
        }

        {
            char  *endp;
            double v = strtod(value, &endp);

            if (endp == value || *trim(endp) != '\0') {
                if (err != 0) snprintf(err, err_cap, "line %d: \"%s\" is not a number",
                                       lineno, value);
                fclose(f);
                return -1;
            }
            /* Voice first, then effects. The two key sets are disjoint, so
             * the order only decides which lookup pays for a miss. */
            if (bm_voice_set_param(voice, key, 0, (float)v) != BM_OK &&
                bm_effects_set_param(effects, key, 0, (float)v) != BM_OK) {
                if (err != 0) snprintf(err, err_cap, "line %d: unknown setting \"%s\"",
                                       lineno, key);
                fclose(f);
                return -1;
            }
        }
    }

    fclose(f);
    return 0;
}

int bm_voicefile_save(const char *path, const bm_voice *voice,
                      const bm_effects *effects)
{
    FILE *f;
    int   to_stdout;

    /* `effects` may be null - a caller that has none should not have to
     * manufacture an empty one to write a voice file. */
    if (path == 0 || voice == 0) return -1;

    to_stdout = (path[0] == '-' && path[1] == '\0');
    f = to_stdout ? stdout : fopen(path, "w");
    if (f == 0) return -1;

    fprintf(f, "# BENCmouth voice\n");
    fprintf(f, "name           = %s\n\n", voice->name ? voice->name : "unnamed");
    fprintf(f, "f0_base        = %.6g\n", (double)voice->f0_base);
    fprintf(f, "f0_range       = %.6g\n", (double)voice->f0_range);
    fprintf(f, "f0_flutter     = %.6g\n", (double)voice->f0_flutter);
    fprintf(f, "vibrato        = %.6g\n", (double)voice->vibrato);
    fprintf(f, "vibrato_rate   = %.6g\n", (double)voice->vibrato_rate);
    fprintf(f, "source         = %.6g\n", (double)voice->source);
    fprintf(f, "speed          = %.6g\n\n", (double)voice->speed);
    fprintf(f, "throat         = %.6g\n", (double)voice->throat);
    fprintf(f, "mouth          = %.6g\n\n", (double)voice->mouth);
    fprintf(f, "breathiness    = %.6g\n", (double)voice->breathiness);
    fprintf(f, "tilt           = %.6g\n", (double)voice->tilt);
    fprintf(f, "open_quotient  = %.6g\n", (double)voice->open_quotient);
    fprintf(f, "whisper        = %.6g\n", (double)voice->whisper);
    fprintf(f, "gain           = %.6g\n\n", (double)voice->gain);
    fprintf(f, "# naturalness controls; 0 is the original BENCmouth behaviour\n");
    fprintf(f, "coarticulation = %.6g\n", (double)voice->coarticulation);
    fprintf(f, "prosody        = %.6g\n", (double)voice->prosody);
    fprintf(f, "formant_glide  = %.6g\n", (double)voice->formant_glide);
    fprintf(f, "bandwidth_track= %.6g\n", (double)voice->bandwidth_track);
    fprintf(f, "flatten        = %.6g\n", (double)voice->flatten);

    /* Only when there is something to say. A file full of zeroed effect keys
     * would suggest the voice has an effects chain that happens to be off,
     * when in fact it has none - and it is one more block to read past in
     * every voice file that will never use one.
     *
     * Every field, once there is. Two had been missing: `level`, since the day
     * it was added, so saving Sentry lost the 1.5 that stops it playing 3.7 dB
     * down; and `ring_drift`, immediately. Both were invisible because nothing
     * saved a file and loaded it back - tests/test_voicefile.c does now, field
     * by field, so the next one added here cannot go quiet the same way. */
    if (effects != 0 &&
        (effects->ring > 0.0f || effects->comb > 0.0f ||
         effects->chorus > 0.0f ||
         effects->drive > 0.0f || effects->crush > 0.0f)) {
        fprintf(f, "\n# effects; applied after the voice, see CLASSIC-VOICES.md\n");
        fprintf(f, "ring           = %.6g\n", (double)effects->ring);
        fprintf(f, "ring_hz        = %.6g\n", (double)effects->ring_hz);
        fprintf(f, "ring_drift     = %.6g\n", (double)effects->ring_drift);
        fprintf(f, "comb           = %.6g\n", (double)effects->comb);
        fprintf(f, "comb_hz        = %.6g\n", (double)effects->comb_hz);
        fprintf(f, "chorus         = %.6g\n", (double)effects->chorus);
        fprintf(f, "chorus_hz      = %.6g\n", (double)effects->chorus_hz);
        fprintf(f, "drive          = %.6g\n", (double)effects->drive);
        fprintf(f, "crush          = %.6g\n", (double)effects->crush);
        fprintf(f, "level          = %.6g\n", (double)effects->level);
    }

    if (to_stdout) { fflush(f); return 0; }
    return (fclose(f) == 0) ? 0 : -1;
}
