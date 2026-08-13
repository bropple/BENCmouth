/*
 * BENCmouth - the note roll
 *
 * A song written as notes on a grid rather than as a line of text: each note is
 * a pitch, a start, a length, and the phonemes to sing there. This is the model
 * and the compiler; src/gui/bm_roll_ui.c draws it, and nothing here knows that
 * a screen exists.
 *
 * The roll compiles to a score and is played through bm_speak_phonemes() like
 * any other. That direction is the only one that works. A score can say things
 * a grid cannot draw - a [speed] that applies across notes, a [pitch] halfway
 * through a syllable, two commands with no phonemes between them - so a roll
 * becomes a score and a score does not become a roll. The editor keeps them
 * apart rather than pretending at a round trip it cannot make.
 *
 * Times are milliseconds throughout, because that is what the engine takes:
 * [dur] and [pause] are milliseconds and always were. Tempo is the editor's
 * arithmetic, exactly as it is for the score tab - see bm_roll_retime.
 */

#ifndef BM_ROLL_H
#define BM_ROLL_H

#include "bencmouth.h"

#include <stddef.h>

/* The engine buffers BM_MAX_PHONEMES (512) phonemes per utterance and a note is
 * at least one of them, plus a rest between any two that do not touch. So this
 * is not the real ceiling - the engine's is - and a roll near this size will
 * report BM_ERR_OVERFLOW when sung rather than being quietly truncated here. */
#define BM_ROLL_NOTES_MAX 256

/* "S T R EY1 T" is 11 characters. Twice that is a syllable nobody has written
 * yet, and a note is one syllable. */
#define BM_NOTE_PHON_MAX  32

/* The word that was typed, kept beside the phonemes it produced so that
 * reopening a song shows what was meant and not only what was said. */
#define BM_NOTE_LYRIC_MAX 24

/* How long a tied note takes to be reached. Where a singer's slur lives - fast
 * enough to be a note change rather than a swoop, slow enough to hear as one
 * sound bending. Not per note: a control for it would be a fourth number on
 * something that is meant to be one click. */
#define BM_ROLL_GLIDE_MS 60

typedef struct bm_note {
    int  start;    /* ms from the beginning of the song */
    int  length;   /* ms this note asks to last; see [dur] */
    int  midi;     /* MIDI note number; 60 is C4. 12..108, as [note] accepts */
    char phon[BM_NOTE_PHON_MAX];
    char lyric[BM_NOTE_LYRIC_MAX];

    /* Sung on the vowel already sounding rather than on a syllable of its own:
     * the second half of a slur. It carries no phonemes - it inherits the vowel
     * of the note it is tied to - and it is reached by gliding rather than by
     * stepping, which is the other half of what legato means.
     *
     * Only meaningful when the note begins exactly where the one before it
     * ended. A tie across a gap would be a lie: the tone stops in the gap, so
     * whatever came after it was re-articulated whatever the file said. See
     * bm_roll_check_ties. */
    unsigned char tie;

    /* Selected in an editor. Transient: never written to a file, never read
     * from one, and meaningless to anything that only sings a song.
     *
     * It lives on the note rather than in a set the editor keeps beside the
     * roll, and that is the whole reason this works: notes are sorted, inserted
     * and removed constantly, so a set of indices would be wrong after every
     * one of those and would have to be repaired at each. A flag on the note is
     * carried by the same memmove that moves the note. */
    unsigned char sel;
} bm_note;

typedef struct bm_roll {
    bm_note note[BM_ROLL_NOTES_MAX];
    int     count;
} bm_roll;

void bm_roll_init(bm_roll *r);

/* Returns the index of the new note, or -1 if the roll is full. Keeps the roll
 * sorted by start time, which is what the compiler and every hit test rely on;
 * an unsorted roll would compile to a score that plays in a different order
 * from the one on screen. */
int  bm_roll_add(bm_roll *r, int start, int length, int midi,
                 const char *phon, const char *lyric);

void bm_roll_remove(bm_roll *r, int index);

/* Re-sorts after a note has been moved, and returns where the note that was at
 * `index` ended up - a drag has to keep hold of the note it is dragging. */
int  bm_roll_sort(bm_roll *r, int index);

/* The shortest a note may be left by something else moving. A note can be made
 * as short as its own right edge is dragged, but nothing another note does to
 * it may take it below this - see bm_roll_deoverlap. */
#define BM_ROLL_MIN_MS 60

/* Two notes at one instant is a thing a monophonic synthesizer cannot sing, so
 * a note dragged onto its neighbour makes the neighbour give way. `keep` is the
 * note being dragged, which is the one that does not move. Returns nonzero if
 * anything did. */
int  bm_roll_deoverlap(bm_roll *r, int keep);

/* Scales every start and length by from/to, for a tempo change. Same reasoning
 * as retime_score in bm_song_ui.c: the engine has no notion of tempo, so the
 * only honest meaning of changing it is changing the numbers written against
 * it. */
void bm_roll_retime(bm_roll *r, float from, float to);

/* ------------------------------------------------------------------ *
 * Selection
 *
 * More than one note at a time, because the things people want to do to a run
 * of notes - move them, delete them, tie a pair - are the same things they want
 * to do to one, and doing them one at a time is not the same as doing them
 * together: a group moved note by note passes through arrangements nobody asked
 * for on the way.
 * ------------------------------------------------------------------ */

int  bm_roll_selected(const bm_roll *r);         /* how many */
int  bm_roll_first_selected(const bm_roll *r);   /* lowest index, or -1 */
int  bm_roll_last_selected(const bm_roll *r);    /* highest index, or -1 */
void bm_roll_select_none(bm_roll *r);
void bm_roll_select_only(bm_roll *r, int index);
void bm_roll_select_toggle(bm_roll *r, int index);

/* Moves every selected note by the same amount of time and the same number of
 * semitones, and returns nonzero if anything moved.
 *
 * Clamped rather than pushed: the group stops where it would collide with a
 * note that is not part of it, and where the earliest of it would go before
 * zero or any of it outside the range [note] accepts. A single note pushes its
 * neighbours along, because that is how room is made for a syllable; a group
 * doing the same would rewrite the rest of the song to make way for a gesture
 * that was only meant to nudge three notes.
 *
 * Because the group cannot cross anything, the roll stays in time order and no
 * index changes - which is what keeps a drag holding the notes it started
 * with. */
int  bm_roll_move_selected(bm_roll *r, int dt, int semitones);

/* Ties `later` to `earlier`: it gives up its own word and carries on the
 * vowel of the note before it. They have to be next to each other in time,
 * because a tie is "keep singing what is already sounding" and there is nothing
 * to keep singing across a note in between. Returns 0 if they are not, or if
 * either index is wrong. */
int  bm_roll_tie_pair(bm_roll *r, int earlier, int later);

/* Drops any tie that has stopped meaning anything - one on the first note, one
 * on a note that no longer touches the note before it, or one whose chain leads
 * back to nothing that can be sung. Call after any edit; the compiler and the
 * drawing both assume it has been. Returns the number cleared. */
int  bm_roll_check_ties(bm_roll *r);

/* The vowel a tied note carries on, or 0 if there is none to carry. Walks back
 * through a run of ties to the note that actually has phonemes in it, because a
 * chain of them is one held vowel however many notes long it is. */
const char *bm_roll_tied_vowel(const bm_roll *r, int index);

/* ------------------------------------------------------------------ *
 * Undo
 *
 * Whole snapshots of the roll rather than a list of things that were done to
 * it. A note grid is a small document - a few hundred notes, twenty kilobytes -
 * and the alternative is a command for every gesture, each with an inverse that
 * has to stay correct as the gestures change. Snapshots cannot desynchronize
 * from the thing they describe, because they *are* the thing.
 *
 * It costs about a megabyte of memory for thirty-two levels each way, which on
 * the desktop this GUI runs on is not a number worth optimizing against the
 * certainty of getting an undo stack right.
 *
 * Coalescing is the caller's to decide, through `token`: marks carrying the
 * same non-zero token are one run and only the first is recorded. That is what
 * makes a drag one undo rather than sixty, and a typed word one undo rather
 * than one per letter. A token of zero never coalesces.
 * ------------------------------------------------------------------ */

#define BM_ROLL_UNDO 32

typedef struct bm_roll_state {
    bm_roll roll;
    float   tempo;
    int     selected;
} bm_roll_state;

typedef struct bm_roll_history {
    bm_roll_state back[BM_ROLL_UNDO];     /* undo */
    bm_roll_state fwd[BM_ROLL_UNDO];      /* redo */
    int      n_back, n_fwd;
    unsigned token;
} bm_roll_history;

void bm_roll_history_init(bm_roll_history *h);

/* Records the state as it is *now*, before whatever is about to change it.
 * Returns nonzero if a snapshot was taken. Anything recorded to redo is thrown
 * away: a new edit is a new future. */
int  bm_roll_mark(bm_roll_history *h, unsigned token, const bm_roll_state *now);

/* Ends the current run, so the next mark records even with the same token.
 * Called when a gesture finishes - a button released, a field left. */
void bm_roll_mark_end(bm_roll_history *h);

/* Steps back or forward. `now` is the state being replaced, which becomes the
 * step in the other direction; the restored state is written over it. Returns 0
 * when there is nothing to go back or forward to. */
int  bm_roll_undo(bm_roll_history *h, bm_roll_state *now);
int  bm_roll_redo(bm_roll_history *h, bm_roll_state *now);

int  bm_roll_can_undo(const bm_roll_history *h);
int  bm_roll_can_redo(const bm_roll_history *h);

/* Compiles to a score string: one line per note, rests between them.
 *
 * Returns the number of notes that were skipped for having no phonemes to sing
 * - which is not an error, because a note being drawn before it is spelled is a
 * normal thing to be halfway through - or -1 if the output did not fit.
 */
int  bm_roll_compile(const bm_roll *r, char *out, size_t cap);

/* Total length in ms: the end of the last note. Not what it will sound like -
 * ask bm_measure() for that, because a note can overrun the length it asked
 * for when its consonants will not fit inside it. */
int  bm_roll_length(const bm_roll *r);

/* MIDI number to name and back. Names are what [note] accepts: a letter, an
 * optional accidental, an optional octave. Sharps on the way out, because one
 * spelling has to be chosen and every accidental key here is drawn as a sharp;
 * flats are accepted on the way in. Returns -1 for a name that is not a note. */
void bm_roll_note_name(int midi, char *out, size_t cap);
int  bm_roll_note_parse(const char *name);

/* One `note =` line's value, read and written. The shape is
 *
 *     note = C4 0 400 M IY1 ; me
 *
 * pitch, start, length, phonemes, and after a semicolon the word they came
 * from. The semicolon is there because ARPABET has no punctuation in it, so
 * there is no phoneme string it could be part of; the lyric is optional and
 * everything before it is not. Returns 0, or -1 with a message in `err`. */
int  bm_roll_note_read(const char *value, bm_note *out, char *err, size_t err_cap);
int  bm_roll_note_write(const bm_note *n, char *out, size_t cap);

#endif /* BM_ROLL_H */
