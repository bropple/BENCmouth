/*
 * BENCmouth - text front end
 * See bm_text.h.
 */

#include "bm_text.h"
#include "bm_dict.h"
#include "bm_lts.h"

#include <stddef.h>

#if BM_WITH_MARKUP
/* Own copy: the core links no libc. */
static void bm_memcpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}
#endif

#define MAX_WORD  64
#define MAX_DIGITS 12

static const char *ONES[] = {
    "ZERO", "ONE", "TWO", "THREE", "FOUR",
    "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"
};
static const char *TEENS[] = {
    "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN",
    "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN", "NINETEEN"
};
static const char *TENS[] = {
    "", "", "TWENTY", "THIRTY", "FORTY",
    "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY"
};

/* Spoken names of the letters, for text that contains an isolated one -
 * "written in C" should say "see", not a clipped /k/. Indexed A..Z.
 *
 * 'A' is handled separately: a lone "a" in running text is the article far
 * more often than it is the letter, so it reduces to a schwa. A lone "I" needs
 * no special case because the pronoun and the letter name coincide. */
static const char *LETTER_NAME[26] = {
    "EY1",              "B IY1",            "S IY1",   "D IY1",
    "IY1",              "EH1 F",            "JH IY1",  "EY1 CH",
    "AY1",              "JH EY1",           "K EY1",   "EH1 L",
    "EH1 M",            "EH1 N",            "OW1",     "P IY1",
    "K Y UW1",          "AA1 R",            "EH1 S",   "T IY1",
    "Y UW1",            "V IY1",            "D AH1 B AH0 L Y UW1",
    "EH1 K S",          "W AY1",            "Z IY1"
};

/* Words the letter-to-sound rules get wrong, with stress marked.
 *
 * This is a stopgap for the ~10% the rules miss, not the real answer - CMUdict
 * is, and it has 135k entries against this handful. Kept small on purpose: an
 * exceptions table that grows by accretion becomes its own maintenance problem
 * and hides how well or badly the rules are actually doing. Add to it only for
 * words that matter to a demo, and delete it entirely once the dictionary
 * lands. */
static const struct { const char *word; const char *phonemes; } EXCEPTIONS[] = {
    { "MACHINE",         "M AH0 SH IY1 N" },
    { "MICRO",           "M AY1 K R OW0" },
    { "MICROCONTROLLER", "M AY1 K R OW0 K AH0 N T R OW1 L ER0" },
    { "MICROPHONE",      "M AY1 K R AH0 F OW1 N" },
    { "SYNTHESIZER",     "S IH1 N TH AH0 S AY1 Z ER0" },
    { "WRITTEN",         "R IH1 T AH0 N" },
    { "COMPUTER",        "K AH0 M P Y UW1 T ER0" },
    { "SOFTWARE",        "S AO1 F T W EH1 R" },
    { "AUTOMATIC",       "AO1 T AH0 M AE1 T IH0 K" }
};
#define NEXCEPTIONS ((int)(sizeof EXCEPTIONS / sizeof EXCEPTIONS[0]))

/* Common abbreviations. Kept short deliberately: an over-eager table
 * mispronounces ordinary words, and "st" is a street far less often than it is
 * the end of a word. */
static const struct { const char *from; const char *to; } ABBREV[] = {
    { "MR",   "MISTER"      },
    { "MRS",  "MISSUS"      },
    { "DR",   "DOCTOR"      },
    { "PROF", "PROFESSOR"   },
    { "ETC",  "ET CETERA"   },
    { "VS",   "VERSUS"      },
    { "AKA",  "AY KAY AY"   }
};
#define NABBREV ((int)(sizeof ABBREV / sizeof ABBREV[0]))

/* ------------------------------------------------------------------ */

static int str_eq_ci(const char *a, const char *b, size_t blen)
{
    size_t i;
    for (i = 0; i < blen; i++) {
        char c = b[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (a[i] == '\0' || a[i] != c) return 0;
    }
    return a[i] == '\0';
}

static size_t put(char *out, size_t cap, size_t at, const char *s, char sep)
{
    if (s == 0 || s[0] == '\0') return at;

    if (at > 0 && sep != '\0') {
        if (at + 1 >= cap) return cap + 1;
        out[at++] = sep;
    }
    while (*s != '\0') {
        if (at + 1 >= cap) return cap + 1;
        out[at++] = *s++;
    }
    out[at] = '\0';
    return at;
}

/* ------------------------------------------------------------------ *
 * Numbers
 * ------------------------------------------------------------------ */

static size_t under_thousand(unsigned v, char *out, size_t cap, size_t at)
{
    if (v >= 100u) {
        at = put(out, cap, at, ONES[v / 100u], ' ');
        at = put(out, cap, at, "HUNDRED", ' ');
        v %= 100u;
    }
    if (v >= 20u) {
        at = put(out, cap, at, TENS[v / 10u], ' ');
        v %= 10u;
        if (v > 0u) at = put(out, cap, at, ONES[v], ' ');
    } else if (v >= 10u) {
        at = put(out, cap, at, TEENS[v - 10u], ' ');
    } else if (v > 0u) {
        at = put(out, cap, at, ONES[v], ' ');
    }
    return at;
}

bm_result bm_text_number_to_words(const char *digits, size_t len,
                                  char *out, size_t out_cap)
{
    static const char *SCALE[] = { "", "THOUSAND", "MILLION", "BILLION" };
    unsigned groups[4];
    unsigned value = 0u;
    size_t   at = 0;
    int      ngroups = 0, i;

    if (digits == 0 || out == 0 || out_cap == 0) return BM_ERR_ARG;

    /* len 0 means NUL-terminated, matching every other length-taking function
     * in the library. */
    if (len == 0) {
        while (digits[len] != '\0') len++;
    }
    if (len == 0) return BM_ERR_ARG;
    if (len > MAX_DIGITS) return BM_ERR_OVERFLOW;

    out[0] = '\0';

    /* Long digit strings are almost never quantities - they are phone numbers,
     * serials, years read as digits. Speaking them digit by digit is both
     * easier and usually what was meant. */
    if (len > 9) {
        for (i = 0; i < (int)len; i++) {
            if (digits[i] < '0' || digits[i] > '9') return BM_ERR_ARG;
            at = put(out, out_cap, at, ONES[digits[i] - '0'], ' ');
            if (at > out_cap) return BM_ERR_OVERFLOW;
        }
        return BM_OK;
    }

    for (i = 0; i < (int)len; i++) {
        if (digits[i] < '0' || digits[i] > '9') return BM_ERR_ARG;
        value = value * 10u + (unsigned)(digits[i] - '0');
    }

    if (value == 0u) {
        at = put(out, out_cap, at, "ZERO", ' ');
        return (at > out_cap) ? BM_ERR_OVERFLOW : BM_OK;
    }

    while (value > 0u && ngroups < 4) {
        groups[ngroups++] = value % 1000u;
        value /= 1000u;
    }

    for (i = ngroups - 1; i >= 0; i--) {
        if (groups[i] == 0u) continue;
        at = under_thousand(groups[i], out, out_cap, at);
        if (i > 0) at = put(out, out_cap, at, SCALE[i], ' ');
        if (at > out_cap) return BM_ERR_OVERFLOW;
    }

    return BM_OK;
}

/* ------------------------------------------------------------------ *
 * Text to phonemes
 * ------------------------------------------------------------------ */

static int is_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'';
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* Runs one word through the rules and appends the result. */
static size_t speak_word(const char *w, size_t len, char *out, size_t cap,
                         size_t at, bm_result *rc)
{
    char   phon[MAX_WORD * 6];
    size_t n = 0;
    int    a;

    /* A single letter is spoken by name, not applied to the rules - "C" alone
     * is "see", and the rules would give a bare /k/. */
    if (len == 1) {
        char c = w[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == 'A') return put(out, cap, at, "AH0", ' ');
        if (c >= 'A' && c <= 'Z') return put(out, cap, at, LETTER_NAME[c - 'A'], ' ');
    }

    /* Abbreviations first, and before the dictionary: cmudict has entries for
     * things like "DR" as spelled-out initials, which is not what someone
     * writing "Dr. Smith" meant. An explicit expansion beats a lookup. */
    for (a = 0; a < NABBREV; a++) {
        if (str_eq_ci(ABBREV[a].from, w, len)) {
            const char *s = ABBREV[a].to;
            size_t start = 0, i = 0;
            for (;; i++) {
                if (s[i] == ' ' || s[i] == '\0') {
                    if (i > start) at = speak_word(s + start, i - start, out, cap, at, rc);
                    start = i + 1;
                    if (s[i] == '\0') break;
                }
            }
            return at;
        }
    }

    /* The dictionary, when one is compiled in. It supplies stress digits,
     * which the rules cannot produce at all. */
    if (bm_dict_lookup(w, len, phon, sizeof phon, &n) == BM_OK && n > 0) {
        return put(out, cap, at, phon, ' ');
    }

    /* Hand-written fixes for rule failures. Largely redundant once the
     * dictionary is compiled in - it reaches here only on a dictionary miss -
     * but it is what a build without the dictionary has, so it stays. */
    for (a = 0; a < NEXCEPTIONS; a++) {
        if (str_eq_ci(EXCEPTIONS[a].word, w, len)) {
            return put(out, cap, at, EXCEPTIONS[a].phonemes, ' ');
        }
    }

    *rc = bm_lts_word(w, len, phon, sizeof phon, &n);
    if (*rc != BM_OK) return at;
    if (n == 0) return at;

    return put(out, cap, at, phon, ' ');
}

bm_result bm_text_to_phonemes(const char *text, size_t text_len,
                              char *out, size_t out_cap, size_t *out_len)
{
    return bm_text_to_phonemes_ex(text, text_len, out, out_cap, out_len, 0u);
}

bm_result bm_text_to_phonemes_ex(const char *text, size_t text_len,
                                 char *out, size_t out_cap, size_t *out_len,
                                 unsigned flags)
{
    size_t    i = 0, at = 0;
    bm_result rc = BM_OK;

    if (text == 0 || out == 0 || out_cap == 0) return BM_ERR_ARG;

#if !BM_WITH_MARKUP
    (void)flags;   /* the parser is compiled out; nothing consults it */
#endif

    if (text_len == 0) {
        while (text[text_len] != '\0') text_len++;
    }
    out[0] = '\0';

    while (i < text_len) {
        char c = text[i];

        if (is_letter(c)) {
            size_t start = i;
            while (i < text_len && is_letter(text[i])) i++;
            at = speak_word(text + start, i - start, out, out_cap, at, &rc);
            if (rc != BM_OK) return rc;
            if (at > out_cap) return BM_ERR_OVERFLOW;

        } else if (is_digit(c)) {
            char   words[MAX_WORD * 8];
            size_t start = i, w = 0, ws;

            while (i < text_len && is_digit(text[i])) i++;
            rc = bm_text_number_to_words(text + start, i - start,
                                         words, sizeof words);
            if (rc != BM_OK) return rc;

            ws = 0;
            for (;; w++) {
                if (words[w] == ' ' || words[w] == '\0') {
                    if (w > ws) {
                        at = speak_word(words + ws, w - ws, out, out_cap, at, &rc);
                        if (rc != BM_OK) return rc;
                    }
                    ws = w + 1;
                    if (words[w] == '\0') break;
                }
            }
            if (at > out_cap) return BM_ERR_OVERFLOW;

#if BM_WITH_MARKUP
        } else if (c == '[' && (flags & BM_TEXT_MARKUP) != 0u) {
            /* Copy the command through verbatim, brackets included. It is not
             * interpreted here - bm_frames.c does that - so the phoneme string
             * stays the one interface between front end and synthesizer, and
             * `bm -t` shows exactly what the synthesizer will act on.
             *
             * An unterminated bracket is an error rather than a guess: silently
             * treating the rest of the line as a command would swallow it. */
            size_t start = i;
            while (i < text_len && text[i] != ']') i++;
            if (i >= text_len) return BM_ERR_ARG;
            i++;                                   /* include the ']' */
            {
                char cmd[64];
                size_t n = i - start;
                if (n >= sizeof cmd) return BM_ERR_OVERFLOW;
                bm_memcpy(cmd, text + start, n);
                cmd[n] = '\0';
                at = put(out, out_cap, at, cmd, ' ');
            }
            if (at > out_cap) return BM_ERR_OVERFLOW;
#endif
        } else {
            /* Punctuation becomes silence. Sentence-final marks get a longer
             * pause than a comma by repeating it - crude, but the frame
             * generator has no notion of pause length yet and this is audible
             * in the right direction. */
            if (c == '.' || c == '!' || c == '?') {
                at = put(out, out_cap, at, "SIL SIL", ' ');
            } else if (c == ',' || c == ';' || c == ':' || c == '-') {
                at = put(out, out_cap, at, "SIL", ' ');
            }
            if (at > out_cap) return BM_ERR_OVERFLOW;
            i++;
        }
    }

    if (out_len != 0) *out_len = at;
    return BM_OK;
}
