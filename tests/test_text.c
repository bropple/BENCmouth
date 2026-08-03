/*
 * BENCmouth - text front end tests
 *
 * The front end is a pure string transform, which is the whole reason it is
 * shaped that way: these run in microseconds and their failures are readable,
 * where the same bug found by ear is "that word sounded funny".
 *
 * Expectations are deliberately loose - substring checks rather than exact
 * output. The NRL rules are about 90% accurate by design, so pinning exact
 * phoneme strings would encode their mistakes as requirements and make every
 * future dictionary improvement look like a regression.
 */

#include "bencmouth.h"
#include "bm_text.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void expect_contains(const char *text, const char *needle)
{
    char   out[4096];
    size_t n = 0;
    bm_result rc = bm_text_to_phonemes(text, 0, out, sizeof out, &n);
    int    ok = (rc == BM_OK) && (strstr(out, needle) != 0);

    printf("  %-26s -> %-44s %s\n", text, out, ok ? "ok" : "FAIL");
    if (!ok) {
        printf("      expected to contain: %s\n", needle);
        failures++;
    }
}

static void test_words(void)
{
    printf("letter-to-sound\n");
    expect_contains("hello",   "HH EH L OW");
    expect_contains("world",   "W ER L D");
    expect_contains("speaks",  "S P IY K S");
    expect_contains("thought", "TH");
    expect_contains("knight",  "N AY T");      /* silent K, silent GH   */
    expect_contains("phone",   "F OW N");      /* PH digraph            */
    expect_contains("nation",  "SH AH N");     /* TI before O softens   */
    expect_contains("vision",  "ZH AH N");     /* and voices after a vowel */

    /* Known miss, recorded rather than asserted: "machine" comes out as
     * M AE CH AY N. It is a French loan and the rules have no way to know
     * that - this is exactly the 10% a dictionary in front of them is for. */
}

static void test_numbers(void)
{
    char out[512];

    printf("numbers\n");

    check(bm_text_number_to_words("0", 0, out, sizeof out) == BM_OK &&
          strcmp(out, "ZERO") == 0, "0 is ZERO");

    check(bm_text_number_to_words("42", 0, out, sizeof out) == BM_OK &&
          strcmp(out, "FORTY TWO") == 0, "42 is FORTY TWO");

    check(bm_text_number_to_words("115", 0, out, sizeof out) == BM_OK &&
          strcmp(out, "ONE HUNDRED FIFTEEN") == 0, "115 uses the teens");

    check(bm_text_number_to_words("1024", 0, out, sizeof out) == BM_OK &&
          strcmp(out, "ONE THOUSAND TWENTY FOUR") == 0, "1024 spans a scale");

    check(bm_text_number_to_words("2000000", 0, out, sizeof out) == BM_OK &&
          strcmp(out, "TWO MILLION") == 0, "empty groups are skipped");

    /* Long digit runs are serials and phone numbers far more often than they
     * are quantities, so they are read out digit by digit. */
    check(bm_text_number_to_words("5551234567", 0, out, sizeof out) == BM_OK &&
          strncmp(out, "FIVE FIVE FIVE", 14) == 0,
          "long digit runs are spoken digit by digit");
}

static void test_punctuation_and_abbrev(void)
{
    printf("normalization\n");
    /* A full stop should pause longer than a comma. */
    expect_contains("stop.",  "SIL SIL");
    expect_contains("a, b",   "SIL");
    expect_contains("Dr",     "D AA K T ER");
    /* Expands to two words and both go back through the rules. */
    expect_contains("etc",    "S IY T ER");
}

static void test_limits(void)
{
    char   out[16];
    size_t n = 0;

    printf("limits\n");

    check(bm_text_to_phonemes(0, 0, out, sizeof out, &n) == BM_ERR_ARG,
          "NULL text is rejected");
    check(bm_text_to_phonemes("hi", 0, 0, 10, &n) == BM_ERR_ARG,
          "NULL output is rejected");
    check(bm_text_to_phonemes("the quick brown fox jumps over", 0,
                              out, sizeof out, &n) == BM_ERR_OVERFLOW,
          "a too-small buffer overflows rather than truncating");

    {
        char big[4096];
        check(bm_text_to_phonemes("", 0, big, sizeof big, &n) == BM_OK && n == 0,
              "empty input produces no phonemes");
    }
}

int main(void)
{
    printf("\nBENCmouth text front end tests\n\n");
    test_words();
    test_numbers();
    test_punctuation_and_abbrev();
    test_limits();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
