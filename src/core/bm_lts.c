/*
 * BENCmouth - letter-to-sound rule engine
 * See bm_lts.h for the context syntax.
 */

#include "bm_lts.h"

#include <stddef.h>

#define MAX_WORD 64

/* ------------------------------------------------------------------ *
 * Character classes. No ctype.h - the core does not link libc, and these are
 * ASCII-only by construction since the text is uppercased first.
 * ------------------------------------------------------------------ */

static int is_alpha(char c)  { return c >= 'A' && c <= 'Z'; }
static int is_vowel(char c)  { return c=='A'||c=='E'||c=='I'||c=='O'||c=='U'||c=='Y'; }
static int is_cons(char c)   { return is_alpha(c) && !is_vowel(c); }
static int is_front(char c)  { return c=='E'||c=='I'||c=='Y'; }

static int in_set(char c, const char *set)
{
    while (*set != '\0') { if (*set == c) return 1; set++; }
    return 0;
}

static int is_voiced(char c) { return in_set(c, "BDVGJLMNRWZ"); }

/* ------------------------------------------------------------------ *
 * Right context: walk forward from the end of the matched span.
 * ------------------------------------------------------------------ */

static int match_right(const char *ctx, const char *t, size_t len, size_t q)
{
    size_t i;

    for (i = 0; ctx[i] != '\0'; i++) {
        char c = ctx[i];

        switch (c) {
        case '#': {                                  /* one or more vowels */
            size_t start = q;
            while (q < len && is_vowel(t[q])) q++;
            if (q == start) return 0;
            break;
        }
        case '*': {                              /* one or more consonants */
            size_t start = q;
            while (q < len && is_cons(t[q])) q++;
            if (q == start) return 0;
            break;
        }
        case ':':                               /* zero or more consonants */
            while (q < len && is_cons(t[q])) q++;
            break;

        case '^':                                    /* single consonant  */
            if (q >= len || !is_cons(t[q])) return 0;
            q++;
            break;

        case '.':                                    /* voiced consonant  */
            if (q >= len || !is_voiced(t[q])) return 0;
            q++;
            break;

        case '+':                                    /* front vowel       */
            if (q >= len || !is_front(t[q])) return 0;
            q++;
            break;

        case '$':                    /* one consonant followed by I or E  */
            if (q >= len || !is_cons(t[q])) return 0;
            q++;
            if (q >= len || (t[q] != 'I' && t[q] != 'E')) return 0;
            q++;
            break;

        case '%':      /* a suffix: E, ES, ED, ER, ELY, ING - longest first */
            if (q < len && t[q] == 'E') {
                if (q + 2 < len && t[q+1] == 'L' && t[q+2] == 'Y') q += 3;
                else if (q + 1 < len &&
                         (t[q+1] == 'R' || t[q+1] == 'S' || t[q+1] == 'D')) q += 2;
                else q += 1;
            } else if (q + 2 < len && t[q] == 'I' && t[q+1] == 'N' && t[q+2] == 'G') {
                q += 3;
            } else {
                return 0;
            }
            break;

        case '&':                                            /* sibilant  */
            if (q < len && in_set(t[q], "SCGZXJ")) q++;
            else if (q < len && t[q] == 'H' && q > 0 &&
                     (t[q-1] == 'C' || t[q-1] == 'S')) q++;
            else return 0;
            break;

        case '@':                 /* consonant after which long U is /UW/  */
            if (q < len && in_set(t[q], "TSRDLZNJ")) q++;
            else if (q < len && t[q] == 'H' && q > 0 &&
                     (t[q-1] == 'T' || t[q-1] == 'C' || t[q-1] == 'S')) q++;
            else return 0;
            break;

        case ' ':                                       /* word boundary  */
            if (q < len && is_alpha(t[q])) return 0;
            break;

        default:                                              /* literal  */
            if (q >= len || t[q] != c) return 0;
            q++;
            break;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ *
 * Left context: walk backward from the start of the matched span, consuming
 * the context string right-to-left.
 * ------------------------------------------------------------------ */

static int match_left(const char *ctx, const char *t, size_t q)
{
    size_t n = 0;
    size_t i;

    while (ctx[n] != '\0') n++;

    for (i = n; i > 0; i--) {
        char c = ctx[i - 1];

        switch (c) {
        case '#': {
            size_t start = q;
            while (q > 0 && is_vowel(t[q-1])) q--;
            if (q == start) return 0;
            break;
        }
        case '*': {
            size_t start = q;
            while (q > 0 && is_cons(t[q-1])) q--;
            if (q == start) return 0;
            break;
        }
        case ':':
            while (q > 0 && is_cons(t[q-1])) q--;
            break;

        case '^':
            if (q == 0 || !is_cons(t[q-1])) return 0;
            q--;
            break;

        case '.':
            if (q == 0 || !is_voiced(t[q-1])) return 0;
            q--;
            break;

        case '+':
            if (q == 0 || !is_front(t[q-1])) return 0;
            q--;
            break;

        case '$':
            /* Reading backwards: I or E, then the consonant before it. */
            if (q == 0 || (t[q-1] != 'I' && t[q-1] != 'E')) return 0;
            q--;
            if (q == 0 || !is_cons(t[q-1])) return 0;
            q--;
            break;

        case '&':
            if (q > 0 && in_set(t[q-1], "SCGZXJ")) q--;
            else if (q > 1 && t[q-1] == 'H' &&
                     (t[q-2] == 'C' || t[q-2] == 'S')) q -= 2;
            else return 0;
            break;

        case '@':
            if (q > 0 && in_set(t[q-1], "TSRDLZNJ")) q--;
            else if (q > 1 && t[q-1] == 'H' &&
                     (t[q-2] == 'T' || t[q-2] == 'C' || t[q-2] == 'S')) q -= 2;
            else return 0;
            break;

        case ' ':
            if (q > 0 && is_alpha(t[q-1])) return 0;
            break;

        default:
            if (q == 0 || t[q-1] != c) return 0;
            q--;
            break;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */

static size_t append(char *out, size_t cap, size_t at, const char *s)
{
    if (s[0] == '\0') return at;

    if (at > 0) {
        if (at + 1 >= cap) return cap + 1;   /* signal overflow */
        out[at++] = ' ';
    }
    while (*s != '\0') {
        if (at + 1 >= cap) return cap + 1;
        out[at++] = *s++;
    }
    out[at] = '\0';
    return at;
}

bm_result bm_lts_word(const char *word, size_t len,
                      char *out, size_t out_cap, size_t *out_len)
{
    char   buf[MAX_WORD + 2];
    size_t n = 0, pos = 0, at = 0;
    size_t i;

    if (word == 0 || out == 0 || out_cap == 0) return BM_ERR_ARG;

    if (len == 0) {
        while (word[len] != '\0') len++;
    }
    if (len > MAX_WORD) return BM_ERR_OVERFLOW;

    /* Uppercase into a private buffer. The rules assume upper case, and the
     * caller's string is not ours to modify. */
    for (i = 0; i < len; i++) {
        char c = word[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        buf[n++] = c;
    }
    buf[n] = '\0';

    out[0] = '\0';

    while (pos < n) {
        int r, matched = 0;

        for (r = 0; r < BM_RULES_EN_COUNT; r++) {
            const bm_lts_rule *rule = &BM_RULES_EN[r];
            size_t m = 0;

            /* Literal span first - cheapest test, rejects almost everything. */
            while (rule->match[m] != '\0') {
                if (pos + m >= n || buf[pos + m] != rule->match[m]) break;
                m++;
            }
            if (rule->match[m] != '\0') continue;
            if (m == 0) continue;

            if (!match_left(rule->left, buf, pos)) continue;
            if (!match_right(rule->right, buf, n, pos + m)) continue;

            at = append(out, out_cap, at, rule->phonemes);
            if (at > out_cap) return BM_ERR_OVERFLOW;

            pos += m;
            matched = 1;
            break;
        }

        if (!matched) {
            /* No rule covers this character. The original program reported it
             * and moved on; refusing to speak the whole word because of one
             * odd letter would be worse. */
            pos++;
        }
    }

    if (out_len != 0) *out_len = at;
    return BM_OK;
}
