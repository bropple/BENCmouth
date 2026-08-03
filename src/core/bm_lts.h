/*
 * BENCmouth - letter-to-sound rules
 *
 * The NRL Report 7948 rule engine. Rules are ordered; the first whose match
 * and contexts both fit wins, and consumes its bracketed span.
 *
 * Context syntax, from the header of the original SNOBOL source:
 *
 *   #  one or more vowels        ^  a single consonant
 *   *  one or more consonants    +  a front vowel: E, I, Y
 *   .  a voiced consonant        :  zero or more consonants
 *   $  single consonant then I or E
 *   %  a suffix: E, ES, ED, ER, ING, ELY
 *   &  a sibilant
 *   @  a consonant after which long U is /UW/ (rule) not /YUW/ (mule)
 *      a space means a word boundary
 *
 * Left contexts are matched right-to-left from the start of the span, right
 * contexts left-to-right from its end.
 */

#ifndef BM_LTS_H
#define BM_LTS_H

#include "bencmouth.h"

#include <stddef.h>

typedef struct bm_lts_rule {
    const char *left;
    const char *match;
    const char *right;
    const char *phonemes;   /* space-separated ARPABET; may be empty */
} bm_lts_rule;

extern const bm_lts_rule BM_RULES_EN[];
extern const int         BM_RULES_EN_COUNT;

/* Converts one word to whitespace-separated ARPABET. `word` should be plain
 * letters; case is handled internally. Returns BM_ERR_OVERFLOW if `out` is too
 * small. Characters no rule covers are skipped, which is the original
 * program's behaviour - the alternative is refusing to speak a word because
 * one letter is unusual. */
bm_result bm_lts_word(const char *word, size_t len,
                      char *out, size_t out_cap, size_t *out_len);

#endif /* BM_LTS_H */
