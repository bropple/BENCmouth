/*
 * BENCmouth - text front end
 *
 * Plain English in, whitespace-separated ARPABET out. Normalization (numbers,
 * abbreviations, punctuation) then letter-to-sound rules.
 *
 * Exposed as a pure string transform because that makes the front end testable
 * without rendering a single sample - front-end bugs are far easier to read
 * than to hear.
 */

#ifndef BM_TEXT_H
#define BM_TEXT_H

#include "bencmouth.h"

#include <stddef.h>

/* Set to 0 to compile the inline-markup parser out entirely. The public
 * BM_TEXT_MARKUP flag and bm_config.markup then do nothing, and brackets stay
 * ordinary characters. */
#ifndef BM_WITH_MARKUP
#define BM_WITH_MARKUP 1
#endif

/* Declared in bencmouth.h; repeated here so this header stands alone:
 *
 *   bm_result bm_text_to_phonemes(const char *text, size_t text_len,
 *                                 char *out, size_t out_cap, size_t *out_len);
 */

/* Expands a run of digits into English words, e.g. "205" -> "TWO HUNDRED
 * FIVE". Written into `out`. Returns BM_ERR_OVERFLOW if it will not fit. */
bm_result bm_text_number_to_words(const char *digits, size_t len,
                                  char *out, size_t out_cap);

#endif /* BM_TEXT_H */
