/*
 * BENCmouth - CMU dictionary compiler
 *
 * Reads ref/cmudict-0.7b.txt and emits src/core/bm_dict_data.c.
 *
 *   ./mkdict ref/cmudict-0.7b.txt src/core/bm_dict_data.c
 *
 * Why a dictionary at all, when the letter-to-sound rules already work: the
 * rules reproduce cmudict's exact phonemes for only about 28% of its 125k
 * entries. That is not a contradiction of the "90% of running text" figure in
 * the NRL report - common words are far more regular than the proper nouns and
 * rare words that make up most of a dictionary - but it does mean an
 * exceptions-only table would have to hold most of the dictionary anyway, so
 * there is nothing to be gained by being clever about which entries to keep.
 *
 * The other thing the dictionary brings, which the rules cannot, is stress.
 * Every cmudict vowel carries a stress digit; the rules emit none at all.
 *
 * Format
 * ------
 * Words are sorted, so they share long prefixes. Storing them front-coded -
 * each entry as "characters shared with the previous entry" plus the rest -
 * takes the word blob from 1.06 MB to about 0.6 MB. Front coding destroys
 * random access, so the table restarts every BM_DICT_BLOCK entries with a
 * complete word; a lookup binary-searches those restart points and then walks
 * at most BM_DICT_BLOCK entries.
 *
 *   words[]   per entry: [shared][n][chars...]   (shared == 0 at a block start)
 *   phones[]  per entry: [n][byte...]  byte = (phoneme index << 2) | stress
 *   blocks[]  per block: word offset, phoneme offset
 *
 * One byte per phoneme works because there are 40 phonemes and 4 stress
 * values: six bits and two bits, exactly.
 */

#include "bm_phonemes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM_DICT_BLOCK 8
#define MAX_ENTRIES 200000
#define MAX_WORD    64
#define MAX_PHONES  64

typedef struct {
    char          word[MAX_WORD];
    unsigned char phone[MAX_PHONES];
    int           nphone;
} entry;

static entry        *entries;
static int           nentries = 0;
static unsigned char wordbuf[4u * 1024u * 1024u];
static unsigned char phonebuf[4u * 1024u * 1024u];
static unsigned      blockw[MAX_ENTRIES / BM_DICT_BLOCK + 2];
static unsigned      blockp[MAX_ENTRIES / BM_DICT_BLOCK + 2];

/* Resolve an ARPABET symbol against the live inventory rather than a private
 * copy of it, so the generated indices cannot drift out of step with
 * bm_phonemes.c. The runtime asserts the count still matches. */
static int phoneme_index(const char *name, size_t len)
{
    const bm_phoneme *p = bm_phoneme_lookup(name, len);
    int i;

    if (p == 0) return -1;
    for (i = 0; i < bm_phoneme_count(); i++) {
        if (bm_phoneme_at(i) == p) return i;
    }
    return -1;
}

static int cmp_entry(const void *a, const void *b)
{
    return strcmp(((const entry *)a)->word, ((const entry *)b)->word);
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    char  line[1024];
    long  dropped_alt = 0, dropped_char = 0, dropped_phone = 0;
    size_t wlen = 0, plen = 0;
    int    i, nblocks = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s cmudict.txt out.c\n", argv[0]);
        return 1;
    }

    entries = (entry *)calloc(MAX_ENTRIES, sizeof *entries);
    if (entries == 0) { fprintf(stderr, "out of memory\n"); return 1; }

    in = fopen(argv[1], "r");
    if (in == 0) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }

    while (fgets(line, sizeof line, in) != 0) {
        char *sp, *tok;
        entry e;
        int   ok = 1;

        if (line[0] == ';') continue;
        sp = strchr(line, ' ');
        if (sp == 0) continue;
        *sp = '\0';

        /* "WORD(2)" is an alternate pronunciation. Keeping only the first
         * keeps the table single-valued, which is what a synthesizer wants -
         * it has no basis for choosing between them. */
        if (strchr(line, '(') != 0) { dropped_alt++; continue; }

        if (strlen(line) >= MAX_WORD) { dropped_char++; continue; }
        for (i = 0; line[i] != '\0'; i++) {
            char c = line[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (!((c >= 'A' && c <= 'Z') || c == '\'')) { ok = 0; break; }
            e.word[i] = c;
        }
        if (!ok) { dropped_char++; continue; }
        e.word[i] = '\0';
        if (i == 0) continue;

        e.nphone = 0;
        for (tok = strtok(sp + 1, " \t\r\n"); tok != 0; tok = strtok(0, " \t\r\n")) {
            int stress = 0, idx;
            size_t n = strlen(tok);

            if (n > 0 && tok[n - 1] >= '0' && tok[n - 1] <= '9') {
                stress = tok[n - 1] - '0';
                if (stress > 3) stress = 0;
                n--;
            }
            idx = phoneme_index(tok, n);
            if (idx < 0 || idx > 63) { ok = 0; break; }
            if (e.nphone >= MAX_PHONES) { ok = 0; break; }
            e.phone[e.nphone++] = (unsigned char)((idx << 2) | stress);
        }
        if (!ok || e.nphone == 0) { dropped_phone++; continue; }

        if (nentries >= MAX_ENTRIES) { fprintf(stderr, "too many entries\n"); return 1; }
        entries[nentries++] = e;
    }
    fclose(in);

    /* cmudict ships sorted, but sorting again costs nothing and the binary
     * search is only correct if it really is sorted. */
    qsort(entries, (size_t)nentries, sizeof *entries, cmp_entry);

    for (i = 0; i < nentries; i++) {
        const char *w = entries[i].word;
        size_t len = strlen(w);
        int shared = 0;

        if (i % BM_DICT_BLOCK == 0) {
            blockw[nblocks] = (unsigned)wlen;
            blockp[nblocks] = (unsigned)plen;
            nblocks++;
        } else {
            const char *prev = entries[i - 1].word;
            while (prev[shared] != '\0' && w[shared] != '\0' &&
                   prev[shared] == w[shared] && shared < 255) shared++;
        }

        wordbuf[wlen++] = (unsigned char)shared;
        wordbuf[wlen++] = (unsigned char)(len - (size_t)shared);
        memcpy(wordbuf + wlen, w + shared, len - (size_t)shared);
        wlen += len - (size_t)shared;

        phonebuf[plen++] = (unsigned char)entries[i].nphone;
        memcpy(phonebuf + plen, entries[i].phone, (size_t)entries[i].nphone);
        plen += (size_t)entries[i].nphone;
    }

    out = fopen(argv[2], "w");
    if (out == 0) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }

    fprintf(out,
        "/*\n"
        " * BENCmouth - compiled CMU Pronouncing Dictionary\n"
        " *\n"
        " * GENERATED FILE - do not edit, and do not commit.\n"
        " *   ./mkdict ref/cmudict-0.7b.txt src/core/bm_dict_data.c\n"
        " *\n"
        " * Derived from the CMU Pronouncing Dictionary, Copyright (C) 1993-2015\n"
        " * Carnegie Mellon University, 2-clause BSD. The full notice is in NOTICE\n"
        " * and ref/cmudict-LICENSE.txt, and must accompany any redistribution of a\n"
        " * binary built with this file.\n"
        " *\n"
        " * %d entries. See tools/mkdict.c for the format.\n"
        " */\n\n"
        "#include \"bm_dict.h\"\n"
        "\n"
        "/* Guarded so that a plain build, which globs the core sources, compiles\n"
        " * this to nothing rather than carrying 1.5 MB it will never read. */\n"
        "#if BM_WITH_DICT\n\n"
        "/* Stored phoneme indices are positions in bm_phonemes.c's table. If that\n"
        " * table changes size, every index here is wrong - so fail the build rather\n"
        " * than mispronounce everything by one phoneme. Regenerate with `make dict`. */\n"
        "typedef char bm_dict_generated_for_a_different_inventory[\n"
        "    (BM_PHONEME_COUNT == %d) ? 1 : -1];\n\n",
        nentries, bm_phoneme_count());

    fprintf(out, "const int BM_DICT_COUNT = %d;\n", nentries);
    fprintf(out, "const int BM_DICT_BLOCKS = %d;\n", nblocks);
    fprintf(out, "const int BM_DICT_PHONEME_COUNT = %d;\n\n", bm_phoneme_count());

    fprintf(out, "const unsigned char BM_DICT_WORDS[%lu] = {\n", (unsigned long)wlen);
    for (i = 0; i < (int)wlen; i++) {
        fprintf(out, "%u,", wordbuf[i]);
        if ((i % 40) == 39) fputc('\n', out);
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "const unsigned char BM_DICT_PHONES[%lu] = {\n", (unsigned long)plen);
    for (i = 0; i < (int)plen; i++) {
        fprintf(out, "%u,", phonebuf[i]);
        if ((i % 40) == 39) fputc('\n', out);
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "const unsigned int BM_DICT_BLOCK_WORD[%d] = {\n", nblocks);
    for (i = 0; i < nblocks; i++) {
        fprintf(out, "%u,", blockw[i]);
        if ((i % 20) == 19) fputc('\n', out);
    }
    fprintf(out, "\n};\n\n");

    fprintf(out, "const unsigned int BM_DICT_BLOCK_PHONE[%d] = {\n", nblocks);
    for (i = 0; i < nblocks; i++) {
        fprintf(out, "%u,", blockp[i]);
        if ((i % 20) == 19) fputc('\n', out);
    }
    fprintf(out, "\n};\n\n#endif /* BM_WITH_DICT */\n");

    fclose(out);

    fprintf(stderr,
        "%d entries, %d blocks\n"
        "  words   %.2f MB\n"
        "  phones  %.2f MB\n"
        "  index   %.2f MB\n"
        "  dropped: %ld alternates, %ld non-alphabetic, %ld unmappable phonemes\n",
        nentries, nblocks,
        (double)wlen / 1e6, (double)plen / 1e6,
        (double)nblocks * 8.0 / 1e6,
        dropped_alt, dropped_char, dropped_phone);

    free(entries);
    return 0;
}
