/*
 * BENCmouth - compiled dictionary lookup
 * See bm_dict.h for the contract and tools/mkdict.c for the data format.
 */

#include "bm_dict.h"
#include "bm_phonemes.h"

#include <stddef.h>

#if BM_WITH_DICT

/* The inventory-size check lives in the generated data file, where the count at
 * generation time is a literal and can therefore be checked at compile time.
 * It cannot live here: BM_DICT_PHONEME_COUNT is an extern const int, not a
 * constant expression, so an array-size assert against it is a
 * variably-modified type rather than an assertion - it compiles, checks
 * nothing, and is an error under -Werror. */

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Reconstructs the word at `offset`, given the previous word already in `buf`.
 * Returns its length and advances `*offset` past the entry. */
static size_t expand(size_t *offset, char *buf, size_t cap)
{
    size_t o = *offset;
    size_t shared = BM_DICT_WORDS[o];
    size_t extra  = BM_DICT_WORDS[o + 1];
    size_t i;

    o += 2;
    if (shared + extra >= cap) return 0;

    for (i = 0; i < extra; i++) buf[shared + i] = (char)BM_DICT_WORDS[o + i];
    buf[shared + extra] = '\0';

    *offset = o + extra;
    return shared + extra;
}

static int compare(const char *a, const char *b, size_t blen)
{
    size_t i;
    for (i = 0; i < blen; i++) {
        char x = a[i];
        char y = upper(b[i]);
        if (x == '\0') return -1;
        if (x != y) return (x < y) ? -1 : 1;
    }
    return (a[blen] == '\0') ? 0 : 1;
}

bm_result bm_dict_lookup(const char *word, size_t len,
                         char *out, size_t out_cap, size_t *out_len)
{
    char   buf[64];
    size_t lo = 0, hi, wo, po;
    int    b, i;

    if (word == 0 || out == 0 || out_cap == 0) return BM_ERR_ARG;
    if (len == 0) { while (word[len] != '\0') len++; }
    if (len == 0 || len >= sizeof buf) return BM_ERR_UNSUPPORTED;

    /* Binary search the block restart points. Each block begins with a
     * complete word, so those are the only entries comparable without
     * reconstructing their predecessors. */
    hi = (size_t)BM_DICT_BLOCKS;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        size_t o = BM_DICT_BLOCK_WORD[mid];
        buf[0] = '\0';
        (void)expand(&o, buf, sizeof buf);
        if (compare(buf, word, len) < 0) lo = mid + 1; else hi = mid;
    }

    /* `lo` is the first block whose head is >= the query, so the entry, if it
     * exists, is in that block or the one before it. */
    for (b = (int)lo - 1; b <= (int)lo && b < BM_DICT_BLOCKS; b++) {
        if (b < 0) continue;

        wo = BM_DICT_BLOCK_WORD[b];
        po = BM_DICT_BLOCK_PHONE[b];
        buf[0] = '\0';

        for (i = 0; i < BM_DICT_BLOCK; i++) {
            size_t n, count, k, at = 0;
            int    entry = b * BM_DICT_BLOCK + i;

            if (entry >= BM_DICT_COUNT) break;

            n = expand(&wo, buf, sizeof buf);
            count = BM_DICT_PHONES[po];

            if (n > 0 && compare(buf, word, len) == 0) {
                for (k = 0; k < count; k++) {
                    unsigned char code = BM_DICT_PHONES[po + 1 + k];
                    const bm_phoneme *p = bm_phoneme_at((int)(code >> 2));
                    int stress = code & 3u;
                    const char *s;

                    if (p == 0) return BM_ERR_UNSUPPORTED;

                    if (at > 0) {
                        if (at + 1 >= out_cap) return BM_ERR_OVERFLOW;
                        out[at++] = ' ';
                    }
                    for (s = p->name; *s != '\0'; s++) {
                        if (at + 1 >= out_cap) return BM_ERR_OVERFLOW;
                        out[at++] = *s;
                    }
                    /* Stress rides on vowels only; writing a digit after a
                     * consonant would be noise the frame generator has to
                     * ignore. */
                    if (p->cls == BM_CLS_VOWEL || p->cls == BM_CLS_DIPHTHONG) {
                        if (at + 1 >= out_cap) return BM_ERR_OVERFLOW;
                        out[at++] = (char)('0' + stress);
                    }
                    out[at] = '\0';
                }
                if (out_len != 0) *out_len = at;
                return BM_OK;
            }

            po += 1 + count;
        }
    }

    return BM_ERR_UNSUPPORTED;
}

int bm_dict_count(void)
{
    return BM_DICT_COUNT;
}

#else /* !BM_WITH_DICT */

bm_result bm_dict_lookup(const char *word, size_t len,
                         char *out, size_t out_cap, size_t *out_len)
{
    (void)word; (void)len; (void)out; (void)out_cap; (void)out_len;
    return BM_ERR_UNSUPPORTED;
}

int bm_dict_count(void)
{
    return 0;
}

#endif /* BM_WITH_DICT */
