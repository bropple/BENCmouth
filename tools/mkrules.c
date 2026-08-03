/*
 * BENCmouth - NRL rule table generator
 *
 * Reads ref/NRL-7948-TRANS.SNO - the original public-domain SNOBOL4 source of
 * the 1976 Naval Research Laboratory letter-to-sound program - and emits
 * src/core/bm_rules_en.c.
 *
 * Generated rather than transcribed on purpose. There are 330 productions and
 * hand-copying them would introduce errors that are individually invisible and
 * collectively ruinous: a single wrong context character produces one
 * mispronounced word class that nobody notices until it says "GHOTI".
 *
 * Run this once and commit the output; the build does not depend on it.
 *
 *   ./mkrules ref/NRL-7948-TRANS.SNO > src/core/bm_rules_en.c
 *
 * Rule syntax in the source file is  left[MATCH]right=/PHONEMES/  where the
 * bracketed part is consumed on a match. Context characters are documented in
 * ref/README.md and in the header of the SNOBOL file itself.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RULES 512

typedef struct {
    char left[32];
    char match[32];
    char right[32];
    char phonemes[64];
} rule;

static rule rules[MAX_RULES];
static int  nrules = 0;

/* The NRL phoneme set is almost ARPABET. These are the differences, plus the
 * punctuation markers, which become explicit silence or nothing at all. */
static const char *map_symbol(const char *s)
{
    if (strcmp(s, "AX") == 0) return "AH";   /* schwa; our AH covers it     */
    if (strcmp(s, "NX") == 0) return "NG";   /* velar nasal                 */
    if (strcmp(s, "WH") == 0) return "W";    /* voiceless w, merged         */
    if (strcmp(s, "<.>") == 0) return "SIL";
    if (strcmp(s, "<,>") == 0) return "SIL";
    if (strcmp(s, "<?>") == 0) return "SIL";
    if (strcmp(s, "<->") == 0) return "";
    if (strcmp(s, "<")   == 0) return "";
    if (strcmp(s, ">")   == 0) return "";
    return s;
}

static void translate_phonemes(const char *in, char *out, size_t cap)
{
    char token[32];
    size_t ti = 0, oi = 0;
    size_t i;

    out[0] = '\0';

    for (i = 0; ; i++) {
        char c = in[i];

        if (c != '\0' && c != ' ') {
            if (ti + 1 < sizeof token) token[ti++] = c;
            continue;
        }

        if (ti > 0) {
            const char *mapped;
            size_t len;

            token[ti] = '\0';
            mapped = map_symbol(token);
            len = strlen(mapped);
            if (len > 0 && oi + len + 2 < cap) {
                if (oi > 0) out[oi++] = ' ';
                memcpy(out + oi, mapped, len);
                oi += len;
                out[oi] = '\0';
            }
            ti = 0;
        }

        if (c == '\0') break;
    }
}

static void emit_c_string(const char *s)
{
    putchar('"');
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    FILE *f;
    char  line[512];
    int   i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s TRANS.SNO > bm_rules_en.c\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "r");
    if (f == 0) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }

    while (fgets(line, sizeof line, f) != 0) {
        char *p, quote, *open, *close, *eq, *slash2, *body;
        char  raw[64];

        /* Continuation lines carrying rules begin with '+'. */
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '+') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;

        /* Rules are single-quoted, except those containing an apostrophe,
         * which are double-quoted. */
        if (*p != '\'' && *p != '"') continue;
        quote = *p++;
        body = p;

        /* Terminated by a backslash immediately before the closing quote. */
        p = strchr(body, quote);
        if (p == 0 || p == body) continue;
        if (p[-1] != '\\') continue;
        p[-1] = '\0';

        open  = strchr(body, '[');
        close = strchr(body, ']');
        eq    = strstr(body, "=/");
        if (open == 0 || close == 0 || eq == 0 || close < open || eq < close) continue;

        slash2 = strrchr(eq + 2, '/');
        if (slash2 == 0) continue;

        if (nrules >= MAX_RULES) { fprintf(stderr, "too many rules\n"); return 1; }

        *open = '\0'; *close = '\0'; *eq = '\0'; *slash2 = '\0';

        snprintf(rules[nrules].left,  sizeof rules[nrules].left,  "%s", body);
        snprintf(rules[nrules].match, sizeof rules[nrules].match, "%s", open + 1);
        snprintf(rules[nrules].right, sizeof rules[nrules].right, "%s", close + 1);

        snprintf(raw, sizeof raw, "%s", eq + 2);
        translate_phonemes(raw, rules[nrules].phonemes,
                           sizeof rules[nrules].phonemes);

        nrules++;
    }
    fclose(f);

    printf("/*\n"
           " * BENCmouth - English letter-to-sound rules\n"
           " *\n"
           " * GENERATED FILE - do not edit by hand.\n"
           " *   ./mkrules ref/NRL-7948-TRANS.SNO > src/core/bm_rules_en.c\n"
           " *\n"
           " * Source: NRL Report 7948 (Elovitz, Johnson, McHugh & Shore, 1976),\n"
           " * US Naval Research Laboratory. Work of the US federal government and\n"
           " * therefore public domain. See ref/README.md.\n"
           " *\n"
           " * %d rules. Roughly 90%% word accuracy on running text, which is why a\n"
           " * dictionary sits in front of these rather than replacing them.\n"
           " */\n\n", nrules);

    printf("#include \"bm_lts.h\"\n\n");
    printf("const bm_lts_rule BM_RULES_EN[] = {\n");

    for (i = 0; i < nrules; i++) {
        printf("    { ");
        emit_c_string(rules[i].left);   printf(", ");
        emit_c_string(rules[i].match);  printf(", ");
        emit_c_string(rules[i].right);  printf(", ");
        emit_c_string(rules[i].phonemes);
        printf(" },\n");
    }

    printf("};\n\n");
    printf("const int BM_RULES_EN_COUNT = %d;\n", nrules);

    fprintf(stderr, "%d rules written\n", nrules);
    return 0;
}
