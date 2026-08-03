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

int bm_voicefile_load(const char *path, bm_voice *voice,
                      char *name_buf, size_t name_cap,
                      char *err, size_t err_cap)
{
    FILE *f;
    char  line[MAX_LINE];
    int   lineno = 0;

    if (path == 0 || voice == 0) return -1;

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

        if (strcmp(key, "preset") == 0) {
            const bm_voice *p = bm_voice_preset(value);
            if (p == 0) {
                if (err != 0) snprintf(err, err_cap, "line %d: unknown preset \"%s\"",
                                       lineno, value);
                fclose(f);
                return -1;
            }
            *voice = *p;
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
            if (bm_voice_set_param(voice, key, 0, (float)v) != BM_OK) {
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

int bm_voicefile_save(const char *path, const bm_voice *voice)
{
    FILE *f;
    int   to_stdout;

    if (path == 0 || voice == 0) return -1;

    to_stdout = (path[0] == '-' && path[1] == '\0');
    f = to_stdout ? stdout : fopen(path, "w");
    if (f == 0) return -1;

    fprintf(f, "# BENCmouth voice\n");
    fprintf(f, "name           = %s\n\n", voice->name ? voice->name : "unnamed");
    fprintf(f, "f0_base        = %.6g\n", (double)voice->f0_base);
    fprintf(f, "f0_range       = %.6g\n", (double)voice->f0_range);
    fprintf(f, "f0_flutter     = %.6g\n", (double)voice->f0_flutter);
    fprintf(f, "speed          = %.6g\n\n", (double)voice->speed);
    fprintf(f, "throat         = %.6g\n", (double)voice->throat);
    fprintf(f, "mouth          = %.6g\n\n", (double)voice->mouth);
    fprintf(f, "breathiness    = %.6g\n", (double)voice->breathiness);
    fprintf(f, "tilt           = %.6g\n", (double)voice->tilt);
    fprintf(f, "open_quotient  = %.6g\n", (double)voice->open_quotient);
    fprintf(f, "gain           = %.6g\n\n", (double)voice->gain);
    fprintf(f, "# naturalness controls; 0 is the original BENCmouth behaviour\n");
    fprintf(f, "coarticulation = %.6g\n", (double)voice->coarticulation);
    fprintf(f, "prosody        = %.6g\n", (double)voice->prosody);
    fprintf(f, "formant_glide  = %.6g\n", (double)voice->formant_glide);
    fprintf(f, "bandwidth_track= %.6g\n", (double)voice->bandwidth_track);

    if (to_stdout) { fflush(f); return 0; }
    return (fclose(f) == 0) ? 0 : -1;
}
