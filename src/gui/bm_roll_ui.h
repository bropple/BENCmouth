/*
 * BENCmouth GUI - the note roll
 *
 * The third tab. Song mode hands the synthesizer a score you have written; this
 * one hands it a score you have drawn - notes on a grid, a syllable typed into
 * each, lengths dragged.
 *
 * It owns no timing of its own. What a note is worth in milliseconds is decided
 * by bm_roll.c, what that turns into is decided by the engine, and what it
 * really came out as is asked for with bm_measure() rather than worked out
 * here. A piano roll that computes its own idea of when things happen ends up
 * drawing one song and playing another, and it is the drawing you would
 * believe.
 *
 * Like the song panel, it opens no file dialogs: it reports what was asked for
 * and main.c performs it, so everything touching the filesystem stays in one
 * place.
 */

#ifndef BM_ROLL_UI_H
#define BM_ROLL_UI_H

#include "bm_gui.h"
#include "bm_roll.h"
#include "bm_songfile.h"

typedef struct bm_roll_ui {
    bm_song song;                   /* voice, effects, tempo, title, and roll */
    char    title[BM_SONG_TITLE_MAX];

    /* The compiled score: what actually sings, and what gets written to the
     * file. Recompiled whenever the roll changes rather than at playback, so
     * that what SING plays and what SAVE writes cannot come apart. */
    char    score[BM_SONG_SCORE_MAX];

    int   selected;                 /* index into song.roll, or -1 */

    /* A drag in progress. The index is held rather than found again each frame
     * because the roll re-sorts as a note moves past its neighbours, so "the
     * note under the mouse" and "the note being dragged" stop agreeing halfway
     * through the gesture - which is exactly when it matters. */
    int   drag;                     /* BM_ROLL_DRAG_* */
    int   drag_index;
    int   drag_grab;                /* ms into the note where it was grabbed */

    /* Where the button went down, and whether the mouse has left that spot.
     *
     * A press that never moves is a selection and must change nothing. Without
     * this it changed something every time: a note is only snapped to the grid
     * when it is dragged, and applying the snap on the press alone moves any
     * note that was not already on the grid - so clicking a note to see what it
     * says nudged it, which is the worst kind of bug to have in an editor. */
    Vector2 drag_from;
    int     drag_moved;

    float scroll_ms;                /* time at the left edge of the grid */
    int   top_midi;                 /* pitch of the top lane */
    float px_per_sec;

    int   snap;                     /* index into the divisions table */

    /* The roll changed: the score has to be recompiled and remeasured. A flag
     * rather than doing it on every edit, because a drag reports a change every
     * frame and measuring is a parse of the whole score. */
    int   dirty;

    /* What each note really sounds for, which is not always what it asked for:
     * a note whose consonants will not fit inside it overruns, and that has to
     * be visible rather than discovered by ear. Filled by bm_measure(). */
    int   measured[BM_ROLL_NOTES_MAX];
    int   skipped;                  /* notes with nothing to sing yet */
    int   unsingable;               /* the score would not measure at all */

    bm_edit lyric_st, phon_st, title_st, help_st;

    /* The tempo the roll's times currently reflect - see bm_song_ui.h, which
     * does the same thing to a score for the same reason. */
    float   tempo_applied;

    /* A newly drawn note wants the caret in the WORD box, and cannot simply
     * take it - see the note where this is set. */
    int     focus_word;

    /* Where playing would start from, in ms. Set by clicking or dragging in the
     * ruler above the grid, which is what a playhead is for.
     *
     * The panel owns it rather than main.c because it is a position in the
     * score being drawn, and reads it back from the transport while that is
     * running - so dragging the head moves the sound, and the sound moves the
     * head, without either being the other's master. */
    float   head_ms;
    int     head_moved;             /* the user just moved it; go there */

    int     help_open;
} bm_roll_ui;

enum {
    BM_ROLL_DRAG_NONE = 0,
    BM_ROLL_DRAG_MOVE,
    BM_ROLL_DRAG_LENGTH,
    BM_ROLL_DRAG_HEAD
};

void bm_roll_ui_init(bm_roll_ui *s);

/* Recompiles the score and remeasures it. Called by the panel when it needs to;
 * exposed because main.c has to do it too after loading a file. */
void bm_roll_ui_refresh(bm_roll_ui *s, const bm_voice *voice);

/* What the panel is asking main.c to do. */
enum {
    BM_ROLL_ACT_NONE = 0,
    BM_ROLL_ACT_LOAD,
    BM_ROLL_ACT_SAVE
};

/* Draws the panel inside `area` and returns one of the actions above.
 *
 * `voice` is the voice in front, which the measurement needs: speed and flatten
 * both change how long a note takes. `playhead_ms` is where the transport has
 * got to, or -1 when nothing is playing.
 */
int  bm_roll_panel(bm_ui *ui, bm_roll_ui *s, Rectangle area, int use_dict,
                   const bm_voice *voice, int playhead_ms);

/* The roll's own help, drawn over everything, while s->help_open. */
void bm_roll_help(bm_ui *ui, bm_roll_ui *s, float w, float h);

#endif /* BM_ROLL_UI_H */
