/*
 * BENCmouth - note roll tests
 *
 * The roll is a model, a compiler and a file format, and none of the three
 * needs a window - so all three are tested here rather than by looking at the
 * screen. What the drawing does with any of it is a separate question.
 *
 * The property that matters: a roll compiles to a score that sings the notes
 * that were drawn, at the times they were drawn at. So the last test compiles
 * one and measures it, and checks the notes land where the roll said.
 */

/* The host layer is not in the library, so it is included as source rather than
 * linked - the same arrangement test_song.c uses, and for the same reason: it
 * keeps `make test` a loop over one file at a time. */
#include "../src/host/bm_roll.c"
#include "../src/host/bm_songfile.c"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ */

static void test_note_names(void)
{
    char name[8];

    printf("note names\n");

    check(bm_roll_note_parse("C4") == 60, "C4 is middle C");
    check(bm_roll_note_parse("A4") == 69, "A4 is 69, which is 440 Hz");
    check(bm_roll_note_parse("A#3") == bm_roll_note_parse("Bb3"),
          "sharps and flats agree");
    check(bm_roll_note_parse("c4") == 60, "lower case is a note too");
    check(bm_roll_note_parse("H4") == -1, "H is not");
    check(bm_roll_note_parse("C9") == -1, "and neither is one the engine "
                                          "will not sing");

    bm_roll_note_name(60, name, sizeof name);
    check(strcmp(name, "C4") == 0, "60 comes back as C4");
    bm_roll_note_name(61, name, sizeof name);
    check(strcmp(name, "C#4") == 0, "and 61 as C#4, sharps being the spelling");
}

static void test_ordering(void)
{
    bm_roll r;

    printf("a roll stays in time order\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 800, 400, 60, "M IY1", "me");
    bm_roll_add(&r, 0,   400, 62, "M IY1", "me");
    bm_roll_add(&r, 400, 400, 64, "M IY1", "me");

    check(r.count == 3, "three notes");
    check(r.note[0].start == 0 && r.note[1].start == 400 &&
          r.note[2].start == 800, "added out of order, held in order");

    /* Move the first note past the last and it should end up last. */
    r.note[0].start = 1600;
    check(bm_roll_sort(&r, 0) == 2, "a note dragged to the end reports its "
                                    "new index");
    check(r.note[2].start == 1600, "and is where it was dragged to");
}

static void test_no_two_notes_at_once(void)
{
    bm_roll r;

    printf("a monophone cannot sing two notes at once\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 0,   400, 60, "M IY1", "me");
    bm_roll_add(&r, 400, 400, 62, "M IY1", "me");

    /* Drag the second note back on top of the first. */
    r.note[1].start = 200;
    (void)bm_roll_sort(&r, 1);
    check(bm_roll_deoverlap(&r, 1) != 0, "an overlap is noticed");
    check(r.note[0].start + r.note[0].length <= r.note[1].start,
          "and the note that was there gives way");
    check(r.note[0].length == 200, "by ending where the dragged note begins");

    /* Drag it all the way to the left. Without an undo, a drag that could erase
     * what it passes over would lose work that cannot be got back - so the note
     * being dragged is what stops. */
    r.note[1].start = 0;
    (void)bm_roll_sort(&r, 1);
    (void)bm_roll_deoverlap(&r, 1);
    check(r.note[0].length >= BM_ROLL_MIN_MS,
          "a note cannot be crushed out of existence by another one");
    check(r.note[1].start >= r.note[0].start + BM_ROLL_MIN_MS,
          "the dragged note is stopped instead");

    /* And a note pushed rightwards moves the ones after it without shortening
     * them, which is how room is made in the middle of a line. */
    {
        bm_roll q;
        bm_roll_init(&q);
        bm_roll_add(&q, 0,   400, 60, "M IY1", "me");
        bm_roll_add(&q, 400, 400, 62, "M IY1", "me");
        bm_roll_add(&q, 800, 400, 64, "M IY1", "me");

        q.note[0].length = 700;               /* lengthen the first */
        (void)bm_roll_deoverlap(&q, 0);
        check(q.note[1].start == 700 && q.note[1].length == 400,
              "notes pushed along keep their lengths");
        check(q.note[2].start == 1100, "and the push carries down the line");
    }
}

static void test_compile(void)
{
    bm_roll r;
    char    score[1024];
    int     skipped;

    printf("compiling to a score\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 0,    400, 60, "M IY1", "me");
    bm_roll_add(&r, 400,  400, 62, "S T R EY1 T", "straight");
    bm_roll_add(&r, 1200, 400, 64, "M IY1", "me");     /* after a 400 ms rest */

    skipped = bm_roll_compile(&r, score, sizeof score);
    printf("%s", score);
    check(skipped == 0, "nothing skipped");
    check(strstr(score, "[dur 400][note C4] M IY1") != 0, "the first note");
    check(strstr(score, "[dur 400][note D4] S T R EY1 T") != 0, "the second");
    check(strstr(score, "[pause 400]") != 0, "and the gap between two notes "
                                             "becomes a rest");

    /* A note drawn but not yet spelled is a normal thing to be halfway
     * through, so it is reported rather than refused. */
    bm_roll_add(&r, 1600, 400, 65, "", "");
    skipped = bm_roll_compile(&r, score, sizeof score);
    check(skipped == 1, "a note with no phonemes is counted, not sung");

    {
        char tiny[16];
        check(bm_roll_compile(&r, tiny, sizeof tiny) == -1,
              "a score that will not fit is refused, not truncated");
    }
}

/* The whole point of the exercise: what was drawn is what is heard, at the
 * times it was drawn at. Compiling and then measuring closes that loop, and
 * the note lengths here are deliberately awkward - one of them is a consonant
 * cluster that does not fit inside the note it was given. */
static void test_what_is_drawn_is_what_sounds(void)
{
    static bm_engine_storage scratch;
    bm_roll  r;
    char     score[1024];
    bm_span  spans[128];
    size_t   n = 0, i;
    uint32_t total = 0;
    uint32_t onset[4] = { 0, 0, 0, 0 };
    int      seen[4] = { 0, 0, 0, 0 };

    printf("what was drawn is what sounds\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 0,    400, 60, "M IY1", "me");
    bm_roll_add(&r, 400,  400, 62, "S T R EY1 T", "straight");
    bm_roll_add(&r, 1200, 600, 64, "M IY1", "me");    /* after a 400 ms rest */

    check(bm_roll_compile(&r, score, sizeof score) == 0, "compiles");
    check(bm_measure(&scratch, 0, score, 0, spans,
                     sizeof spans / sizeof spans[0], &n, &total) == BM_OK,
          "and measures");

    /* The first span of each [dur] group is where that note begins. */
    for (i = 0; i < n; i++) {
        uint16_t g = spans[i].group;
        if (g >= 1u && g <= 3u && !seen[g]) {
            seen[g] = 1;
            onset[g] = spans[i].start_ms;
        }
    }

    printf("    drawn at 0, 400, 1200 - sounds at %u, %u, %u\n",
           onset[1], onset[2], onset[3]);

    check(onset[1] == 0u, "the first note starts where it was drawn");
    check(onset[2] >= 390u && onset[2] <= 410u, "and so does the second");
    check(onset[3] >= 1190u && onset[3] <= 1210u,
          "and the third, on the far side of a rest");
    check(total >= 1790u && total <= 1810u,
          "and the song is as long as the roll says it is");
}

/* Legato: a second note on the vowel already sounding. Two things have to be
 * true of the compiled score - no consonant is re-articulated, and the note is
 * glided onto rather than stepped onto - and one thing has to be true of the
 * roll: a tie that cannot mean what it says is dropped rather than compiled. */
static void test_ties(void)
{
    bm_roll r;
    char    score[1024];

    printf("ties\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 0,    400, 60, "S T R EY1 T", "straight");
    bm_roll_add(&r, 400,  400, 67, "", "");
    r.note[1].tie = 1u;

    check(bm_roll_check_ties(&r) == 0, "a tie onto a touching note stands");
    check(bm_roll_tied_vowel(&r, 1) != 0 &&
          strcmp(bm_roll_tied_vowel(&r, 1), "EY1") == 0,
          "and carries the vowel of the syllable, not its last phoneme");

    check(bm_roll_compile(&r, score, sizeof score) == 0, "it compiles");
    printf("%s", score);
    check(strstr(score, "[glide 60]") != 0, "the tied note is glided onto");
    check(strstr(score, "[dur 400][note G4] EY1") != 0,
          "and sings the held vowel with no consonant in front of it");

    /* A third, ordinary note has to stop the gliding - [glide] applies onward
     * like everything else, so a slur would otherwise bend the whole song. */
    bm_roll_add(&r, 800, 400, 60, "M IY1", "me");
    (void)bm_roll_compile(&r, score, sizeof score);
    check(strstr(score, "[glide 0]") != 0,
          "and the note after it is stepped onto again");

    /* A chain of ties is one held vowel however long it is. */
    bm_roll_init(&r);
    bm_roll_add(&r, 0,   400, 60, "M IY1", "me");
    bm_roll_add(&r, 400, 400, 62, "", "");
    bm_roll_add(&r, 800, 400, 64, "", "");
    r.note[1].tie = r.note[2].tie = 1u;
    check(bm_roll_check_ties(&r) == 0, "a chain of ties stands");
    check(bm_roll_tied_vowel(&r, 2) != 0 &&
          strcmp(bm_roll_tied_vowel(&r, 2), "IY1") == 0,
          "and every note in it is the same vowel");

    /* And the cases where a tie stops meaning anything. */
    bm_roll_init(&r);
    bm_roll_add(&r, 0,   400, 60, "M IY1", "me");
    bm_roll_add(&r, 600, 400, 62, "", "");        /* a gap in front of it */
    r.note[1].tie = 1u;
    check(bm_roll_check_ties(&r) == 1, "a tie across a rest is dropped");
    check(r.note[1].tie == 0u, "because the tone stops in the rest whatever "
                               "the file says");

    bm_roll_init(&r);
    bm_roll_add(&r, 0, 400, 60, "", "");
    r.note[0].tie = 1u;
    check(bm_roll_check_ties(&r) == 1, "and so is one on the first note");
}

static void test_retime(void)
{
    bm_roll r;

    printf("tempo\n");

    bm_roll_init(&r);
    bm_roll_add(&r, 0,   500, 60, "M IY1", "me");
    bm_roll_add(&r, 500, 500, 62, "M IY1", "me");

    bm_roll_retime(&r, 120.0f, 240.0f);
    check(r.note[0].length == 250, "twice the tempo, half the length");
    check(r.note[1].start == 250, "and the notes after it move with it");
}

static void test_file_round_trip(void)
{
    bm_song song, back;
    char    score[BM_SONG_SCORE_MAX];
    char    err[192];
    const char *path = "bm_roll_test.bmsong";

    printf("through a .bmsong and back\n");

    bm_song_init(&song);
    snprintf(song.title, sizeof song.title, "%s", "Round trip");
    song.tempo = 120.0f;
    bm_roll_add(&song.roll, 0,   400, 60, "M IY1", "me");
    bm_roll_add(&song.roll, 400, 400, 62, "S T R EY1 T", "straight");
    bm_roll_add(&song.roll, 800, 400, 64, "M IY1", 0);   /* no lyric */

    bm_roll_compile(&song.roll, score, sizeof score);
    check(bm_song_save(path, &song, score) == 0, "saved");

    if (bm_song_load(path, &back, score, sizeof score, err, sizeof err) != 0) {
        printf("    %s\n", err);
        check(0, "loaded");
        return;
    }
    check(1, "loaded");
    check(back.roll.count == 3, "three notes came back");
    check(back.roll.note[1].midi == 62 && back.roll.note[1].start == 400 &&
          back.roll.note[1].length == 400,
          "pitch, start and length survive");
    check(strcmp(back.roll.note[1].phon, "S T R EY1 T") == 0,
          "and so do the phonemes, spaces and all");
    check(strcmp(back.roll.note[1].lyric, "straight") == 0, "and the lyric");
    check(back.roll.note[2].lyric[0] == '\0',
          "a note with no lyric comes back without one");
    check(strstr(score, "[dur 400][note C4]") != 0,
          "and the score is in the file too, so bm -S still sings it");

    remove(path);
}

static void test_a_song_without_a_roll_still_loads(void)
{
    bm_song song;
    char    score[BM_SONG_SCORE_MAX];
    char    err[192];
    static const char TEXT[] =
        "# BENCmouth song\n"
        "title = Typed\n"
        "tempo = 120\n"
        "score =\n"
        "[hold 400][note C4] M IY1\n";

    printf("a song that was typed rather than drawn\n");

    check(bm_song_parse(TEXT, sizeof TEXT - 1, &song, score, sizeof score,
                        err, sizeof err) == 0, "parses");
    check(song.roll.count == 0, "and opens with an empty roll rather than a "
                                "guess at one");
}

static void test_bad_note_lines(void)
{
    bm_note n;
    char    err[128];

    printf("note lines that are wrong\n");

    check(bm_roll_note_read("C4 0 400 M IY1", &n, err, sizeof err) == 0,
          "a good one reads");
    check(bm_roll_note_read("H4 0 400 M IY1", &n, err, sizeof err) == -1,
          "a bad pitch is rejected");
    check(bm_roll_note_read("C4", &n, err, sizeof err) == -1,
          "and so is a line with no times on it");
    printf("    %s\n", err);

    check(bm_roll_note_read("C4 0 400 AA AA AA AA AA AA AA AA AA AA AA AA",
                            &n, err, sizeof err) == -1,
          "a syllable longer than any syllable is rejected, not truncated");
}

int main(void)
{
    printf("\nBENCmouth note roll tests\n\n");
    test_note_names();
    test_ordering();
    test_no_two_notes_at_once();
    test_compile();
    test_what_is_drawn_is_what_sounds();
    test_ties();
    test_retime();
    test_file_round_trip();
    test_a_song_without_a_roll_still_loads();
    test_bad_note_lines();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
