/*
 * BENCmouth GUI - song mode
 *
 * The second tab. Where the text tab hands words to the front end and lets it
 * decide the tune, this one hands the synthesizer a score directly: phonemes
 * with [note] and [hold] threaded through them, which is what singing is here.
 *
 * It is a separate file rather than more of main.c because it owns real state -
 * a score buffer, a title, a scratch translator, four caret positions - and
 * because none of that has anything to say to the text tab. The two tabs share
 * the voice, the transport and the scope, and nothing else.
 *
 * File dialogs are deliberately not opened from here. The panel reports what
 * the user asked for and main.c performs it, so that everything touching the
 * filesystem stays in one place next to the code that already knows what to do
 * when there is no dialog available at all.
 */

#ifndef BM_SONG_UI_H
#define BM_SONG_UI_H

#include "bm_gui.h"
#include "bm_songfile.h"

#define BM_SONG_WORD_MAX 64

typedef struct bm_song_ui {
    bm_song song;                            /* voice, tempo, title */
    char    score[BM_SONG_SCORE_MAX];
    char    title[BM_SONG_TITLE_MAX];

    /* The word-to-phoneme scratch pad. Writing a score means knowing how to
     * spell a word in ARPABET, and looking that up elsewhere every few seconds
     * is the difference between writing a song and giving up on one. */
    char    word[BM_SONG_WORD_MAX];
    char    word_out[BM_SONG_WORD_MAX * 6];
    int     word_dirty;

    /* One per scrolling thing on screen. out_st and ref_st are separate even
     * though only one is ever visible at a time: a shared bm_edit means the
     * reference dialog inherits the translator's scroll offset the moment it
     * opens, which reads as the dialog opening halfway down. */
    bm_edit score_st, title_st, word_st, out_st, ref_st;

    /* The tempo the score's [hold] values currently reflect.
     *
     * `song.tempo` is what the control shows and the file stores; this is what
     * the text was last written against. When they differ, the score is
     * retimed by their ratio and this catches up. Keeping the two apart is
     * what makes the retime happen once per change rather than once per frame
     * of a slider drag, which would round 260 down a millisecond at a time. */
    float   tempo_applied;

    /* The format reference, as a modal. It is two screens of text and it is
     * needed while writing, not while listening. */
    int     ref_open;
} bm_song_ui;

void bm_song_ui_init(bm_song_ui *s);

/* What the panel is asking main.c to do. */
enum {
    BM_SONG_ACT_NONE = 0,
    BM_SONG_ACT_LOAD,
    BM_SONG_ACT_SAVE
};

/* Draws the panel inside `area` and returns one of the actions above.
 * `use_dict` follows the main window's dictionary toggle, so the translator
 * shows the same pronunciation the engine would use. */
int  bm_song_panel(bm_ui *ui, bm_song_ui *s, Rectangle area, int use_dict);

/* The format reference, drawn over everything. Call it where the other modal
 * is drawn, and only while s->ref_open. */
void bm_song_reference(bm_ui *ui, bm_song_ui *s, float w, float h);

#endif /* BM_SONG_UI_H */
