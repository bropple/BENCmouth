/*
 * BENCmouth - compiled dictionary lookup
 *
 * Optional. Compile with -DBM_WITH_DICT=1 and link src/core/bm_dict_data.c,
 * which tools/mkdict.c generates from ref/cmudict-0.7b.txt. `make dict` does
 * both.
 *
 * Off by default because the data is about 1.5 MB. That is nothing on a
 * desktop and impossible on a microcontroller, and the letter-to-sound rules
 * are the embedded path precisely because they fit in a few kilobytes.
 *
 * What it buys, when it is on:
 *
 *   - correct pronunciation for the ~72% of cmudict entries the rules get
 *     wrong on an exact-match basis
 *   - stress digits, which the rules cannot produce at all, and which the
 *     frame generator needs to give a sentence any rhythm
 */

#ifndef BM_DICT_H
#define BM_DICT_H

#include "bencmouth.h"
#include "bm_phonemes.h"   /* BM_PHONEME_COUNT, checked by the generated data */

#include <stddef.h>

#ifndef BM_WITH_DICT
#define BM_WITH_DICT 0
#endif

/* Restart interval for the front-coded word table. Must match the value
 * tools/mkdict.c was built with; the generator writes it into the data file's
 * comment header and the block count is derived from it. */
#define BM_DICT_BLOCK 8

#if BM_WITH_DICT

extern const int BM_DICT_COUNT;
extern const int BM_DICT_BLOCKS;
extern const int BM_DICT_PHONEME_COUNT;

extern const unsigned char BM_DICT_WORDS[];
extern const unsigned char BM_DICT_PHONES[];
extern const unsigned int  BM_DICT_BLOCK_WORD[];
extern const unsigned int  BM_DICT_BLOCK_PHONE[];

#endif /* BM_WITH_DICT */

/* Looks up `word` and writes whitespace-separated ARPABET with stress digits.
 * Case-insensitive; `len` may be 0 for a NUL-terminated string.
 *
 * Returns BM_OK on a hit, BM_ERR_UNSUPPORTED on a miss - which is the caller's
 * cue to fall back to the rules - and BM_ERR_OVERFLOW if `out` is too small.
 * Always returns BM_ERR_UNSUPPORTED when built without the dictionary, so
 * callers need no conditional compilation of their own. */
bm_result bm_dict_lookup(const char *word, size_t len,
                         char *out, size_t out_cap, size_t *out_len);

/* bm_dict_count() is declared in bencmouth.h: front ends need it, and they are
 * not allowed in here. */

#endif /* BM_DICT_H */
