/*
 * BENCmouth - .bmsong format tests
 *
 * The parser lives in src/host/ and is not part of libbencmouth.a, so it is
 * included as source rather than linked. That is what lets `make test` keep its
 * one-file-plus-the-library rule while still covering a host component - and
 * the parser was deliberately written to work on a memory buffer, with the file
 * reader as a thin wrapper, precisely so that it could be tested this way
 * without touching the filesystem.
 */

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

static const char *GOOD =
    "# a comment\n"
    "title    = Test Song\n"
    "voice    = BENCmouth Retro\n"
    "tempo    = 120\n"
    "vibrato  = 0.4\n"
    "\n"
    "score =\n"
    "# the first line of lyrics\n"
    "[note A#4][hold 300] AA1\n"
    "[note C4] IY1\n";

static void test_parse(void)
{
    bm_song song;
    char    score[BM_SONG_SCORE_MAX];
    char    err[192] = "";

    printf("parsing\n");

    check(bm_song_parse(GOOD, 0, &song, score, sizeof score, err, sizeof err) == 0,
          "a well-formed song parses");
    if (err[0] != '\0') printf("      %s\n", err);

    check(strcmp(song.title, "Test Song") == 0, "title is read");
    check(strcmp(song.voice_name, "BENCmouth Retro") == 0, "voice name is read");
    check(song.tempo == 120.0f, "tempo is read");

    /* The named preset supplies the base, and a key after it overrides that
     * one field without disturbing the others. */
    check(song.voice.f0_base == 118.0f, "the named preset supplied the voice");
    check(song.voice.vibrato == 0.4f, "a later key overrides the preset");
    check(song.voice.coarticulation == 0.0f,
          "and leaves everything it did not name alone");

    check(song.voice.name == song.voice_name,
          "voice.name points at the song's own storage");

    /* The header's comment is gone, the score's comment is gone, and the sharp
     * in A#4 survived - which is the whole reason comments are whole-line. */
    check(strstr(score, "A#4") != 0, "a sharp in a note name survives");
    check(strstr(score, "the first line of lyrics") == 0,
          "a whole-line comment in the score is dropped");
    check(strstr(score, "[note C4] IY1") != 0, "the rest of the score survives");
}

static void test_score_is_verbatim(void)
{
    bm_song song;
    char    score[BM_SONG_SCORE_MAX];
    char    err[192];
    const char *src =
        "title = x\n"
        "score =\n"
        "line one\n"
        "\n"
        "line three # not a comment, it is not at the start\n";

    printf("the score is taken verbatim\n");

    check(bm_song_parse(src, 0, &song, score, sizeof score, err, sizeof err) == 0,
          "parses");
    check(strcmp(score, "line one\n\nline three # not a comment, "
                        "it is not at the start\n") == 0,
          "blank lines and mid-line hashes are preserved exactly");
    if (strcmp(score, "line one\n\nline three # not a comment, "
                      "it is not at the start\n") != 0) {
        printf("      got: [%s]\n", score);
    }
}

static void test_errors(void)
{
    bm_song song;
    char    score[BM_SONG_SCORE_MAX];
    char    err[192];

    printf("errors\n");

    err[0] = '\0';
    check(bm_song_parse("title = x\nwibble = 3\nscore =\nAA1\n", 0, &song,
                        score, sizeof score, err, sizeof err) != 0,
          "an unknown setting is rejected");
    printf("      %s\n", err);
    check(strstr(err, "line 2") != 0, "and the error names the line");

    err[0] = '\0';
    /* A valid header and nothing else: the file is well-formed and still has no
     * song in it, which is the case worth the separate message. */
    check(bm_song_parse("title = x\nspeed = 1\n", 0, &song,
                        score, sizeof score, err, sizeof err) != 0,
          "a file with no score = line is rejected");
    printf("      %s\n", err);

    err[0] = '\0';
    check(bm_song_parse("title = x\ntempo = fast\nscore =\nAA1\n", 0, &song,
                        score, sizeof score, err, sizeof err) != 0,
          "a non-numeric tempo is rejected");

    /* An unknown voice must NOT be fatal: the score is the part that cannot be
     * reconstructed, and refusing to open the file would lose it. */
    err[0] = '\0';
    check(bm_song_parse("voice = Nonexistent\nspeed = 1.5\nscore =\nAA1\n", 0,
                        &song, score, sizeof score, err, sizeof err) == 0,
          "an unknown voice name is kept rather than refused");
    check(strcmp(song.voice_name, "Nonexistent") == 0, "the name is preserved");
    check(song.voice.speed == 1.5f, "and the settings after it still apply");
}

static void test_round_trip(void)
{
    bm_song a, b;
    char    score_a[BM_SONG_SCORE_MAX], score_b[BM_SONG_SCORE_MAX];
    char    err[192];
    const char *path = "bm_song_roundtrip.bmsong";
    int     same_voice;

    printf("round trip\n");

    if (bm_song_parse(GOOD, 0, &a, score_a, sizeof score_a, err, sizeof err) != 0) {
        check(0, "the source song parses");
        return;
    }

    check(bm_song_save(path, &a, score_a) == 0, "saves");
    check(bm_song_load(path, &b, score_b, sizeof score_b, err, sizeof err) == 0,
          "loads back");

    check(strcmp(a.title, b.title) == 0, "title survives");
    check(strcmp(a.voice_name, b.voice_name) == 0, "voice name survives");
    check(a.tempo == b.tempo, "tempo survives");
    check(strcmp(score_a, score_b) == 0, "the score survives byte for byte");

    /* Every voice field, compared as a block from the first float onward - the
     * name pointers legitimately differ, since each song owns its own copy. */
    same_voice = memcmp(&a.voice.f0_base, &b.voice.f0_base,
                        sizeof(bm_voice) - offsetof(bm_voice, f0_base)) == 0;
    check(same_voice, "the whole voice survives");

    remove(path);
}

int main(void)
{
    printf("\nBENCmouth song file tests\n\n");
    test_parse();
    test_score_is_verbatim();
    test_errors();
    test_round_trip();
    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
