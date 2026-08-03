/*
 * BENCmouth - dictionary tests
 *
 * Runs in both builds. Without -DBM_WITH_DICT=1 there is no data to test, so
 * the lookup assertions are skipped and only the contract that matters to a
 * dictionary-free build is checked: that a miss is reported cleanly and the
 * caller falls through to the rules. Skipping loudly beats a suite that
 * silently tests nothing.
 */

#include "bm_dict.h"
#include "bm_text.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int skipped  = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void expect(const char *word, const char *want)
{
    char   got[256];
    size_t n = 0;
    bm_result rc = bm_dict_lookup(word, 0, got, sizeof got, &n);
    int ok = (rc == BM_OK) && strcmp(got, want) == 0;

    printf("  %-16s -> %-28s %s\n", word, (rc == BM_OK) ? got : "(miss)",
           ok ? "ok" : "FAIL");
    if (!ok) {
        printf("      wanted: %s\n", want);
        failures++;
    }
}

static void test_absent_build(void)
{
    char   out[64];
    size_t n = 0;

    printf("without the dictionary\n");
    check(bm_dict_count() == 0, "bm_dict_count() reports zero");
    check(bm_dict_lookup("machine", 0, out, sizeof out, &n) == BM_ERR_UNSUPPORTED,
          "every lookup misses, so callers fall back to the rules");

    /* The front end must still work, just with rule pronunciations. */
    {
        char phon[256];
        size_t m = 0;
        check(bm_text_to_phonemes("hello world", 0, phon, sizeof phon, &m) == BM_OK
              && m > 0, "text still converts via the rules alone");
    }
    skipped = 1;
}

static void test_present_build(void)
{
    char   out[256];
    size_t n = 0;

    printf("dictionary (%d entries)\n", bm_dict_count());
    check(bm_dict_count() > 100000, "a plausible number of entries is compiled in");

    printf("\n  words no rule could ever get right\n");
    expect("colonel",   "K ER1 N AH0 L");
    expect("Wednesday", "W EH1 N Z D IY0");
    expect("choir",     "K W AY1 ER0");
    expect("machine",   "M AH0 SH IY1 N");

    printf("\n  ordinary words\n");
    expect("hello",     "HH AH0 L OW1");
    expect("world",     "W ER1 L D");

    printf("\n  boundaries of the table\n");
    /* Front coding means the first and last entries exercise different paths
     * from the middle: the first has no predecessor to share a prefix with,
     * and the last ends a partial block. */
    check(bm_dict_lookup("a", 0, out, sizeof out, &n) == BM_OK,
          "the very first entries resolve");
    check(bm_dict_lookup("zzzzzzzzzzzzz", 0, out, sizeof out, &n) == BM_ERR_UNSUPPORTED,
          "a word past the end misses rather than reading off the table");
    check(bm_dict_lookup("qqqxyzzy", 0, out, sizeof out, &n) == BM_ERR_UNSUPPORTED,
          "a word inside the range but absent misses");

    printf("\n  robustness\n");
    check(bm_dict_lookup("HELLO", 0, out, sizeof out, &n) == BM_OK,
          "lookup is case-insensitive");
    check(bm_dict_lookup("", 0, out, sizeof out, &n) == BM_ERR_UNSUPPORTED,
          "empty word misses");
    check(bm_dict_lookup("hello", 0, out, 4, &n) == BM_ERR_OVERFLOW,
          "a short buffer overflows rather than truncating");

    printf("\n  stress, which the rules cannot supply at all\n");
    {
        char phon[512];
        size_t m = 0;
        bm_text_to_phonemes("the colonel arrived", 0, phon, sizeof phon, &m);
        printf("    %s\n", phon);
        check(strstr(phon, "ER1") != 0, "primary stress reaches the phoneme stream");
        check(strstr(phon, "AH0") != 0, "unstressed vowels are marked too");
    }
}

int main(void)
{
    printf("\nBENCmouth dictionary tests\n\n");

    if (bm_dict_count() == 0) {
        test_absent_build();
    } else {
        test_present_build();
    }

    printf("\n%s (%d failure%s)%s\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s",
           skipped ? "  [built without the dictionary; run `make dict` to test it]" : "");
    return failures ? 1 : 0;
}
