/*
 * BENCmouth GUI - song mode
 * See bm_song_ui.h for what this is and why it is its own file.
 */

#include "bm_song_ui.h"

#include <stdio.h>
#include <string.h>

/* Focus ids. Unique among every text box on screen at once, which includes the
 * text tab's - the two tabs are never both up, but the id space is shared and
 * accidental reuse would carry the caret across a tab switch. */
#define ID_SCORE 2
#define ID_TITLE 3
#define ID_WORD      4
#define ID_WORD_OUT  5
#define ID_REFERENCE 6

/* Height of a one-line text box: one line of body text plus the inset a short
 * box gets. bm_textbox is a wrapping, scrolling editor - there is no
 * single-line variant - so this has to be at least tall enough for the line,
 * or the box clips its own text and raises a scrollbar for it. */
#define LINE_H (BM_FONT_BODY + 4.0f + 10.0f)

/* The starting score. A scale rather than a tune: it says what the notation
 * looks like in one line, and every note in it is audibly correct or audibly
 * not, which an unfamiliar melody would not be. */
static const char DEFAULT_SCORE[] =
    "[hold 400]\n"
    "[note C4] M IY1 [note D4] M IY1 [note E4] M IY1 [note F4] M IY1\n"
    "[note G4] M IY1 [note A4] M IY1 [note B4] M IY1 [note C5] M IY1\n";

void bm_song_ui_init(bm_song_ui *s)
{
    if (s == 0) return;

    memset(s, 0, sizeof *s);
    bm_song_init(&s->song);

    snprintf(s->title, sizeof s->title, "%s", "Untitled");
    snprintf(s->score, sizeof s->score, "%s", DEFAULT_SCORE);
    snprintf(s->word, sizeof s->word, "%s", "bencmouth");

    /* Singing wants a voice that holds a note. Prosody is speech planning -
     * declination across a phrase, accents on stressed syllables - and against
     * an absolute melody every bit of it is interference, so song mode starts
     * with it off and a little vibrato on instead. The sliders are right there
     * if that is not what was wanted. */
    s->song.voice.prosody = 0.0f;
    s->song.voice.vibrato = 0.28f;
    s->song.voice.vibrato_rate = 5.5f;
    s->song.tempo = 120.0f;

    s->word_dirty = 1;
    s->score_st.caret = s->score_st.sel = (int)strlen(s->score);
    s->title_st.caret = s->title_st.sel = (int)strlen(s->title);
    s->word_st.caret  = s->word_st.sel  = (int)strlen(s->word);
}

/* ------------------------------------------------------------------ */

static void translate(bm_song_ui *s, int use_dict)
{
    size_t n = 0;
    unsigned flags = use_dict ? 0u : BM_TEXT_NO_DICT;

    if (s->word[0] == '\0') {
        s->word_out[0] = '\0';
        return;
    }
    if (bm_text_to_phonemes_ex(s->word, 0, s->word_out, sizeof s->word_out,
                               &n, flags) != BM_OK || n == 0) {
        snprintf(s->word_out, sizeof s->word_out, "%s", "(no pronunciation)");
    }
}

/* Appends to the score at the caret, which is where someone looking at the
 * score expects inserted text to land. */
static void insert_at_caret(bm_song_ui *s, const char *text)
{
    size_t len = strlen(s->score);
    size_t add = strlen(text);
    int    at  = s->score_st.caret;

    if (at < 0 || (size_t)at > len) at = (int)len;
    if (len + add + 2 >= sizeof s->score) return;

    memmove(s->score + at + add + 1, s->score + at, len - (size_t)at + 1);
    memcpy(s->score + at, text, add);
    s->score[at + (int)add] = ' ';

    s->score_st.caret = s->score_st.sel = at + (int)add + 1;
}

/* ------------------------------------------------------------------ */

int bm_song_panel(bm_ui *ui, bm_song_ui *s, Rectangle area, int use_dict)
{
    int   action = BM_SONG_ACT_NONE;
    float split = area.width * 0.62f;
    float rx = area.x + split + BM_PAD;
    float rw = area.width - split - BM_PAD;
    float y;

    if (ui == 0 || s == 0) return BM_SONG_ACT_NONE;

    /* ---- left: the score ---- */
    bm_label(ui, "SCORE  -  PHONEMES AND [NOTE] / [HOLD]", area.x, area.y);
    bm_textbox(ui, ID_SCORE,
               (Rectangle){ area.x, area.y + 18.0f, split - BM_PAD,
                            area.height - 18.0f },
               s->score, (int)sizeof s->score, &s->score_st);

    /* ---- right: title, translator, reference ----
     *
     * The running total, against a panel 210 tall: 18 label + 34 box + 8,
     * 18 + 34 + 4, 34 view + 8, 22 tempo, 28 buttons = 208. It fits with two
     * pixels to spare, so change one of these numbers and check the FORMAT row
     * still lands inside `area` - there is no layout engine to catch it. */
    y = area.y;
    bm_label(ui, "TITLE", rx, y);
    y += 18.0f;
    if (bm_textbox(ui, ID_TITLE, (Rectangle){ rx, y, rw, LINE_H },
                   s->title, (int)sizeof s->title, &s->title_st)) {
        /* Kept in step so a save does not need to remember to copy it. */
        snprintf(s->song.title, sizeof s->song.title, "%s", s->title);
    }
    y += LINE_H + 8.0f;

    bm_label(ui, "WORD TO PHONEMES", rx, y);
    y += 18.0f;
    if (bm_textbox(ui, ID_WORD, (Rectangle){ rx, y, rw - 84.0f, LINE_H },
                   s->word, (int)sizeof s->word, &s->word_st)) {
        s->word_dirty = 1;
    }
    if (bm_button(ui, (Rectangle){ rx + rw - 76.0f, y, 76.0f, LINE_H },
                  "INSERT", s->word_out[0] != '\0')) {
        insert_at_caret(s, s->word_out);
    }
    y += LINE_H + 4.0f;

    if (s->word_dirty) {
        translate(s, use_dict);
        s->word_dirty = 0;
    }
    /* Selectable, so the phonemes a word translated to can be lifted straight
     * out - the INSERT button puts them in the score, and this is for the times
     * you want them somewhere else. */
    bm_textview(ui, ID_WORD_OUT, (Rectangle){ rx, y, rw, LINE_H }, s->word_out,
                &s->out_st, BM_TEXT);
    y += LINE_H + 8.0f;

    /* A quarter note in milliseconds, which is the number actually needed to
     * write a [hold]. The engine knows nothing about tempo - note lengths are
     * absolute - so this is arithmetic the editor does rather than a setting
     * the synthesizer reads. */
    {
        char line[96];
        float t = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;

        snprintf(line, sizeof line, "%.0f BPM  -  quarter %.0f ms  eighth %.0f ms",
                 (double)t, (double)(60000.0f / t), (double)(30000.0f / t));
        bm_text(ui, BM_FONT_SMALL, line, rx, y, BM_DIM);
    }
    y += 22.0f;

    if (bm_button(ui, (Rectangle){ rx, y, 88.0f, 28.0f }, "FORMAT", 1)) {
        s->ref_open = 1;
        s->ref_st.scroll = 0.0f;
    }
    if (bm_button(ui, (Rectangle){ rx + 96.0f, y, 88.0f, 28.0f }, "LOAD", 1)) {
        action = BM_SONG_ACT_LOAD;
    }
    if (bm_button(ui, (Rectangle){ rx + 192.0f, y, 88.0f, 28.0f }, "SAVE", 1)) {
        action = BM_SONG_ACT_SAVE;
    }

    return action;
}

/* ------------------------------------------------------------------ */

/* Not const, because bm_textview terminates the selection in place to hand it
 * to the clipboard and puts the byte straight back. A string literal would be
 * undefined behaviour for that one instant even though the text is identical
 * either side of it. */
static char REFERENCE[] =
"A score is ARPABET phonemes with bracketed commands threaded through them.\n"
"Everything outside brackets is a phoneme; everything inside is an\n"
"instruction that applies to every phoneme after it until changed.\n"
"\n"
"    [hold 400][note C4] M IY1 [note E4] M IY1\n"
"\n"
"================================================================\n"
"  PITCH\n"
"================================================================\n"
"\n"
"  [note NAME]    Sets the pitch absolutely. C4 is middle C and A4 is\n"
"                 440 Hz. Accidentals either way: A#4 and Bb4 are the\n"
"                 same note. The octave digit is optional and defaults\n"
"                 to 4. Range C0 to C8.\n"
"\n"
"  [pitch HZ]     Transposes instead of replacing. The intonation of\n"
"                 everything after it is preserved and shifted to a new\n"
"                 base, which is what you want for speech and not for a\n"
"                 melody. 20 to 500 Hz.\n"
"\n"
"  The difference matters. A sung note is that pitch and not that pitch\n"
"  plus whatever accent the prosody planner had in mind for the syllable\n"
"  - sharing the transposing behaviour once put A4 at 525 Hz.\n"
"\n"
"  For the same reason, turn prosody down for singing. It is speech\n"
"  planning: it declines the pitch across a phrase and lengthens the last\n"
"  syllable, and against a written melody all of that is interference.\n"
"\n"
"  vibrato and vibrato_rate are voice sliders, not commands. A held note\n"
"  with no vibrato at all is a test tone; a quarter of a semitone near\n"
"  5.5 Hz is where singers actually sit.\n"
"\n"
"================================================================\n"
"  LENGTH AND DELAY\n"
"================================================================\n"
"\n"
"  [hold MS]      How long the vowels after it last, in milliseconds.\n"
"                 Vowels only: a sung note's duration lives in its vowel,\n"
"                 and stretching the consonants with it turns a word into\n"
"                 a groan.\n"
"\n"
"  [pause MS]     Inserts silence here. Up to 10000 ms. This is the rest\n"
"                 between phrases, and unlike everything else it happens\n"
"                 once where it is written rather than applying onward.\n"
"\n"
"  [speed X]      Rate multiplier for everything else - the consonants,\n"
"                 mostly, once [hold] has taken the vowels. 0.1 to 10.\n"
"\n"
"  [reset]        Back to the voice's own settings: no held length, no\n"
"                 pitch override, nominal speed.\n"
"\n"
"  Tempo lives in the file, not in the engine. At T beats per minute a\n"
"  quarter note is 60000/T milliseconds - 500 ms at 120 - and the panel\n"
"  shows the arithmetic so you can write the [hold] without doing it.\n"
"\n"
"================================================================\n"
"  PHONEMES\n"
"================================================================\n"
"\n"
"  Vowels      AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW\n"
"  Stops       B D G K P T\n"
"  Affricates  CH JH\n"
"  Fricatives  DH F HH S SH TH V Z ZH\n"
"  Nasals      M N NG\n"
"  Liquids     L R W Y\n"
"  Silence     SIL           Punctuation  . ? ,\n"
"\n"
"  A vowel may carry a stress digit: 1 primary, 2 secondary, 0 none.\n"
"  In a song the melody decides the pitch, so the digit mostly affects\n"
"  duration - IY1 is longer than IY0.\n"
"\n"
"  The WORD TO PHONEMES box spells any word for you, using the same\n"
"  dictionary and rules the engine would. INSERT drops the result into\n"
"  the score at the caret.\n"
"\n"
"================================================================\n"
"  THE FILE\n"
"================================================================\n"
"\n"
"  A .bmsong is text: a header of key = value lines, then a line reading\n"
"  exactly \"score =\", after which the rest of the file is the score.\n"
"  The header carries the title, the tempo, and every voice parameter, so\n"
"  a song reopens sounding the way it was left.\n"
"\n"
"  Comments are whole lines beginning with #. A # anywhere else is\n"
"  literal, which it has to be: [note A#4] is a sharp.\n"
"\n"
"  ./bm -S song.bmsong -a   sings one from the command line.\n";

void bm_song_reference(bm_ui *ui, bm_song_ui *s, float w, float h)
{
    Rectangle p;
    float     pw = w - 120.0f;
    float     ph = h - 80.0f;
    float     cy;

    if (ui == 0 || s == 0) return;

    if (pw > 760.0f) pw = 760.0f;
    if (ph > 620.0f) ph = 620.0f;
    p = (Rectangle){ (w - pw) * 0.5f, (h - ph) * 0.5f, pw, ph };

    DrawRectangle(0, 0, (int)w, (int)h, (Color){ 0, 0, 0, 200 });
    bm_panel(p);

    /* The dialog's own controls are live again - the block rectangle main.c
     * published is there to stop the layout underneath, not this. */
    ui->blocking = 0;

    bm_text_spaced(ui, BM_FONT_BODY, "SCORE FORMAT", p.x + 20.0f, p.y + 18.0f,
                   BM_TEXT);
    bm_divider(p.x + 20.0f, p.y + 46.0f, pw - 40.0f);

    cy = p.y + ph - 46.0f;
    bm_textview(ui, ID_REFERENCE,
                (Rectangle){ p.x + 20.0f, p.y + 56.0f, pw - 40.0f,
                             cy - (p.y + 56.0f) - 10.0f },
                REFERENCE, &s->ref_st, BM_DIM);

    if (bm_button(ui, (Rectangle){ p.x + pw - 116.0f, cy, 96.0f, 30.0f },
                  "CLOSE", 1) || IsKeyPressed(KEY_ESCAPE)) {
        s->ref_open = 0;
    }
}
