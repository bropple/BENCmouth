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

/* The roll comes with it: a song now carries the notes it was drawn from, so
 * the parser calls into bm_roll.c and the two travel together. */
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

/* The chain has to survive too, field by field, and it had not been:
 * bm_song_save wrote eight of the fourteen fields bm_effects had, because the
 * list was written before echo, reverb, ring_drift and level existed and
 * nothing went back to it. A song saved with a room on it loaded without one,
 * silently, and nothing here would have noticed - there was no coverage of the
 * effects half of a song file at all.
 *
 * Named keys rather than a memcmp of the struct, so a failure says which field
 * went missing instead of only that one did. */
static void test_effects_round_trip(void)
{
    static const char *KEYS[] = {
        "ring", "ring_hz", "ring_drift", "comb", "comb_hz", "chorus",
        "chorus_hz", "drive", "vocoder", "vocoder_hz", "crush", "echo",
        "echo_ms", "reverb", "reverb_size", "level"
    };
    const int n = (int)(sizeof KEYS / sizeof KEYS[0]);
    bm_song a, b;
    char    score_a[BM_SONG_SCORE_MAX], score_b[BM_SONG_SCORE_MAX];
    char    err[192];
    const char *path = "bm_song_fx_roundtrip.bmsong";
    const float *fa, *fb;
    int     k, bad = 0;

    printf("round trip, effects\n");

    if (bm_song_parse(GOOD, 0, &a, score_a, sizeof score_a, err, sizeof err) != 0) {
        check(0, "the source song parses");
        return;
    }

    /* Sixty-fourths, for the same reason test_voicefile.c uses them: the
     * comparison below is exact, so the values fed to it have to be ones that
     * survive being written as text and read back. */
    fa = (const float *)(const void *)&a.effects.ring;
    for (k = 0; k < n; k++) {
        bm_effects_set_param(&a.effects, KEYS[k], 0,
                             (float)(k + 1) * (1.0f / 64.0f));
    }

    check(bm_song_save(path, &a, score_a) == 0, "saves");
    check(bm_song_load(path, &b, score_b, sizeof score_b, err, sizeof err) == 0,
          "loads back");

    fb = (const float *)(const void *)&b.effects.ring;
    for (k = 0; k < n; k++) {
        if (fa[k] != fb[k]) {
            printf("      %s: saved %.6g, loaded back %.6g\n", KEYS[k],
                   (double)fa[k], (double)fb[k]);
            bad++;
        }
    }
    check(bad == 0, "every effect parameter survives a save and a load");

    /* And the span check the other two files carry, so a field added to
     * bm_effects without a line in bm_song_save fails here rather than in a
     * saved file six months later. */
    {
        size_t span = offsetof(bm_effects, level) + sizeof(float)
                    - offsetof(bm_effects, ring);
        check((size_t)n == span / sizeof(float),
              "and the list above covers every field there is");
    }

    remove(path);
}

int main(void)
{
    printf("\nBENCmouth song file tests\n\n");
    test_parse();
    test_score_is_verbatim();
    test_errors();
    test_round_trip();
    test_effects_round_trip();
    printf("\n%s (%d failure%s)\n\n", failures ? "FAILURES" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
