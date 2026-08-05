/*
 * BENCmouth - the .bmsong file format
 *
 * A song is a phoneme score plus the voice that should sing it. Both halves
 * matter: a melody written for a voice with a 90 Hz base and half a semitone of
 * vibrato sounds wrong out of a 200 Hz voice with none, and losing the voice on
 * save means every reload is a re-tuning session.
 *
 * The format is text, and deliberately the same shape as a .bmvoice file - a
 * header of `key = value` lines - so that anything already true of voice files
 * is true here too. The one addition is that the score has to be many lines of
 * free-form text, which `key = value` cannot carry. A line that is exactly
 *
 *     score =
 *
 * ends the header, and everything after it is the score.
 *
 *     # BENCmouth song
 *     title    = Daisy Bell
 *     voice    = BENCmouth
 *     tempo    = 120
 *     vibrato  = 0.35
 *     score =
 *     # Daisy, Daisy
 *     [hold 380][note D4] D EY1 [note B3] Z IY0 [note G3] .
 *
 * Comments: a line whose first non-blank character is '#' is dropped, in the
 * header and in the score alike. A '#' anywhere else is literal, and that is
 * not an oversight - `[note A#4]` is a sharp, and stripping from the first '#'
 * to end of line the way the .bmvoice loader does would silently eat half of
 * every accidental in the file.
 *
 * Unknown header keys are errors, for the reason voice files give: a setting
 * that is quietly dropped produces a song that mysteriously sounds wrong.
 */

#ifndef BM_SONGFILE_H
#define BM_SONGFILE_H

#include "bencmouth.h"

#include <stddef.h>

#define BM_SONG_TITLE_MAX 96
#define BM_SONG_NAME_MAX  64

/* Enough for a few hundred lines of score. Songs are hand-written; this is a
 * bound on a text file somebody typed, not on a data set. */
#define BM_SONG_SCORE_MAX 16384

typedef struct bm_song {
    char     title[BM_SONG_TITLE_MAX];
    char     voice_name[BM_SONG_NAME_MAX];
    bm_voice voice;

    /* The effects chain the song was written against. Carried for the same
     * reason the voice is: a melody written through a ring modulator is a
     * different piece without it. */
    bm_effects effects;

    /* Beats per minute. Nothing in the engine reads this - note lengths are
     * absolute milliseconds, set by [hold N] - so it is metadata for the
     * editor, which uses it to show what a quarter note is worth (60000/tempo
     * milliseconds). Kept in the file because working it out again every time
     * you reopen a song is exactly the sort of thing a file should remember.
     * 0 means unset. */
    float    tempo;
} bm_song;

/* IMPORTANT: after a successful parse, `song->voice.name` points into
 * `song->voice_name` - the struct refers to itself. Copying a bm_song by value
 * leaves the copy's name pointing at the original, so either keep it in one
 * place or fix the pointer up after copying. */

/* Empty title, default voice, no tempo. */
void bm_song_init(bm_song *song);

/* Parses from memory. `score` receives the score text, NUL-terminated. Returns
 * 0, or -1 with a message in `err`.
 *
 * Separate from bm_song_load so the parser can be tested on strings without
 * touching the filesystem, which is most of what makes a text format testable
 * at all. */
int bm_song_parse(const char *text, size_t len, bm_song *song,
                  char *score, size_t score_cap, char *err, size_t err_cap);

int bm_song_load(const char *path, bm_song *song,
                 char *score, size_t score_cap, char *err, size_t err_cap);

/* Writes the header and then the score verbatim. Every voice key is written,
 * not only the ones that differ from a preset, so a song reloads as exactly the
 * voice it was saved with even if the preset it started from later moves. */
int bm_song_save(const char *path, const bm_song *song, const char *score);

#endif /* BM_SONGFILE_H */
