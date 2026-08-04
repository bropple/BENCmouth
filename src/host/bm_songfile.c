/*
 * BENCmouth - .bmsong loading and saving
 * See bm_songfile.h for the format.
 */

#include "bm_songfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 512

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *trim(char *s)
{
    char *end;

    while (is_space(*s)) s++;
    if (*s == '\0') return s;

    end = s + strlen(s) - 1;
    while (end >= s && is_space(*end)) *end-- = '\0';
    return s;
}

/* A whole-line comment, which is the only kind this format has. See the header
 * for why: '#' is a sharp in [note A#4]. */
static int is_comment(const char *s)
{
    while (is_space(*s)) s++;
    return *s == '#';
}

/* Errors carry the line number, because a song is hand-written and "unknown
 * setting" without one means reading the whole file to find it. */
#define FAIL(...) \
    do { if (err != 0 && err_cap > 0) snprintf(err, err_cap, __VA_ARGS__); \
         return -1; } while (0)

void bm_song_init(bm_song *song)
{
    if (song == 0) return;

    song->title[0] = '\0';
    bm_voice_default(&song->voice);
    /* Copy the preset's name into our own storage so the self-reference below
     * is the only thing voice.name ever points at. */
    snprintf(song->voice_name, sizeof song->voice_name, "%s",
             song->voice.name ? song->voice.name : "BENCmouth");
    song->voice.name = song->voice_name;
    bm_effects_default(&song->effects);
    song->tempo = 0.0f;
}

int bm_song_parse(const char *text, size_t len, bm_song *song,
                  char *score, size_t score_cap, char *err, size_t err_cap)
{
    size_t i = 0;
    int    lineno = 0;
    size_t out = 0;
    int    in_score = 0;

    if (text == 0 || song == 0 || score == 0 || score_cap == 0) return -1;
    if (len == 0) len = strlen(text);

    bm_song_init(song);
    score[0] = '\0';

    while (i < len) {
        size_t start = i, n;
        char   line[MAX_LINE];
        char  *key, *value, *eq;

        while (i < len && text[i] != '\n') i++;
        n = i - start;
        if (n > 0 && text[start + n - 1] == '\r') n--;
        if (i < len) i++;                       /* past the newline */
        lineno++;

        /* ---- inside the score: verbatim, minus whole-line comments ---- */
        if (in_score) {
            if (n < MAX_LINE) {
                memcpy(line, text + start, n);
                line[n] = '\0';
                if (is_comment(line)) continue;
            }
            if (out + n + 2 > score_cap) {
                FAIL("line %d: the score is longer than this build allows "
                     "(%d bytes)", lineno, (int)score_cap);
            }
            memcpy(score + out, text + start, n);
            out += n;
            score[out++] = '\n';
            score[out] = '\0';
            continue;
        }

        /* ---- header ---- */
        if (n >= MAX_LINE) FAIL("line %d: header line too long", lineno);
        memcpy(line, text + start, n);
        line[n] = '\0';

        if (is_comment(line)) continue;
        key = trim(line);
        if (*key == '\0') continue;

        eq = strchr(key, '=');
        if (eq == 0) {
            FAIL("line %d: expected key = value, or a bare \"score =\"", lineno);
        }
        *eq = '\0';
        value = trim(eq + 1);
        key = trim(key);

        /* The header ends here and everything below is score. Deliberately a
         * key with no value rather than a marker of its own, so the whole file
         * is one lexical form and there is nothing extra to explain. */
        if (strcmp(key, "score") == 0 && *value == '\0') {
            in_score = 1;
            continue;
        }

        if (strcmp(key, "title") == 0) {
            snprintf(song->title, sizeof song->title, "%s", value);
            continue;
        }

        if (strcmp(key, "voice") == 0) {
            const bm_voice *p = bm_voice_preset(value);
            /* An unknown voice is not fatal. A song is mostly its score, and
             * refusing to open one because it names a preset this build does
             * not have would lose the score along with the voice - so the name
             * is kept, the parameters below still apply, and what is lost is
             * only the starting point. */
            if (p != 0) song->voice = *p;
            snprintf(song->voice_name, sizeof song->voice_name, "%s", value);
            song->voice.name = song->voice_name;
            continue;
        }

        if (strcmp(key, "effects") == 0) {
            const bm_effects *fx = bm_effects_preset(value);
            /* Unknown effects preset: same reasoning as an unknown voice. The
             * score is the irreplaceable part, so keep the file openable. */
            if (fx != 0) song->effects = *fx;
            continue;
        }

        if (strcmp(key, "tempo") == 0) {
            char  *endp;
            double v = strtod(value, &endp);
            if (endp == value || *trim(endp) != '\0' || v <= 0.0 || v > 1000.0) {
                FAIL("line %d: tempo must be a positive number of beats "
                     "per minute", lineno);
            }
            song->tempo = (float)v;
            continue;
        }

        {
            char  *endp;
            double v = strtod(value, &endp);

            if (endp == value || *trim(endp) != '\0') {
                FAIL("line %d: \"%s\" is not a number", lineno, value);
            }
            if (bm_voice_set_param(&song->voice, key, 0, (float)v) != BM_OK &&
                bm_effects_set_param(&song->effects, key, 0, (float)v) != BM_OK) {
                FAIL("line %d: unknown setting \"%s\"", lineno, key);
            }
        }
    }

    if (!in_score) {
        FAIL("no \"score =\" line, so this file has no song in it");
    }

    return 0;
}

int bm_song_load(const char *path, bm_song *song,
                 char *score, size_t score_cap, char *err, size_t err_cap)
{
    FILE  *f;
    char  *buf;
    long   size;
    size_t got;
    int    rc;

    if (path == 0 || song == 0) return -1;

    f = fopen(path, "rb");
    if (f == 0) {
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }

    /* Read whole rather than line at a time: the score is taken verbatim, and
     * assembling "verbatim" out of a line reader means reproducing exactly the
     * line endings that were stripped. */
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "cannot read %s", path);
        fclose(f);
        return -1;
    }
    if (size > (long)(BM_SONG_SCORE_MAX * 2)) {
        if (err != 0 && err_cap > 0)
            snprintf(err, err_cap, "%s is too large to be a song", path);
        fclose(f);
        return -1;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (buf == 0) {
        if (err != 0 && err_cap > 0) snprintf(err, err_cap, "out of memory");
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);

    rc = bm_song_parse(buf, got, song, score, score_cap, err, err_cap);
    free(buf);
    return rc;
}

int bm_song_save(const char *path, const bm_song *song, const char *score)
{
    FILE *f;
    int   to_stdout;
    const bm_voice *v;

    if (path == 0 || song == 0) return -1;
    v = &song->voice;

    to_stdout = (path[0] == '-' && path[1] == '\0');
    f = to_stdout ? stdout : fopen(path, "w");
    if (f == 0) return -1;

    fprintf(f, "# BENCmouth song\n");
    fprintf(f, "title          = %s\n", song->title);
    fprintf(f, "voice          = %s\n", song->voice_name);
    if (song->tempo > 0.0f) fprintf(f, "tempo          = %.6g\n", (double)song->tempo);
    fprintf(f, "\n");

    fprintf(f, "f0_base        = %.6g\n", (double)v->f0_base);
    fprintf(f, "f0_range       = %.6g\n", (double)v->f0_range);
    fprintf(f, "f0_flutter     = %.6g\n", (double)v->f0_flutter);
    fprintf(f, "vibrato        = %.6g\n", (double)v->vibrato);
    fprintf(f, "vibrato_rate   = %.6g\n", (double)v->vibrato_rate);
    fprintf(f, "speed          = %.6g\n", (double)v->speed);
    fprintf(f, "throat         = %.6g\n", (double)v->throat);
    fprintf(f, "mouth          = %.6g\n", (double)v->mouth);
    fprintf(f, "breathiness    = %.6g\n", (double)v->breathiness);
    fprintf(f, "tilt           = %.6g\n", (double)v->tilt);
    fprintf(f, "open_quotient  = %.6g\n", (double)v->open_quotient);
    fprintf(f, "whisper        = %.6g\n", (double)v->whisper);
    fprintf(f, "gain           = %.6g\n", (double)v->gain);
    fprintf(f, "coarticulation = %.6g\n", (double)v->coarticulation);
    fprintf(f, "prosody        = %.6g\n", (double)v->prosody);
    fprintf(f, "formant_glide  = %.6g\n", (double)v->formant_glide);
    fprintf(f, "bandwidth_track= %.6g\n", (double)v->bandwidth_track);
    fprintf(f, "flatten        = %.6g\n", (double)v->flatten);

    if (song->effects.ring > 0.0f || song->effects.comb > 0.0f ||
        song->effects.drive > 0.0f || song->effects.crush > 0.0f) {
        fprintf(f, "\nring           = %.6g\n", (double)song->effects.ring);
        fprintf(f, "ring_hz        = %.6g\n", (double)song->effects.ring_hz);
        fprintf(f, "comb           = %.6g\n", (double)song->effects.comb);
        fprintf(f, "comb_hz        = %.6g\n", (double)song->effects.comb_hz);
        fprintf(f, "drive          = %.6g\n", (double)song->effects.drive);
        fprintf(f, "crush          = %.6g\n", (double)song->effects.crush);
    }

    fprintf(f, "\nscore =\n");
    if (score != 0 && score[0] != '\0') {
        size_t n = strlen(score);
        fwrite(score, 1, n, f);
        if (score[n - 1] != '\n') fputc('\n', f);
    }

    if (to_stdout) { fflush(f); return 0; }
    return (fclose(f) == 0) ? 0 : -1;
}
