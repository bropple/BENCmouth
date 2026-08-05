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
#define ID_TEMPO     9
#define ID_QUARTER   10

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
    s->tempo_applied = 120.0f;

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

/* Rewrites every [hold N] in the score for a new tempo.
 *
 * This is what makes the tempo control a control rather than a caption. The
 * engine has no notion of tempo - `[hold]` is milliseconds and always was - so
 * the only thing "changing the tempo" can honestly mean here is changing the
 * numbers that were written against it. Doing it to the text rather than at
 * playback also keeps the file self-describing: the tempo in the header and the
 * holds in the score always agree, so reloading gives back exactly what was
 * heard, which a hidden playback multiplier would not.
 *
 * Clamped to the 1..10000 ms the markup parser accepts, so dragging the tempo
 * to an extreme cannot produce a score that will no longer sing. Values that
 * clamp do not come back on the way up - that is the price of holding the text
 * as the single source of truth, and it is why the range is 40..240 rather than
 * something wider.
 */
static void retime_score(bm_song_ui *s, float from, float to)
{
    /* Static because it is 16 KB and this is called from a draw loop; the GUI
     * is single-threaded and this is dead between calls. */
    static char out[BM_SONG_SCORE_MAX];
    const char *in = s->score;
    size_t o = 0;
    float  ratio;

    if (from <= 0.0f || to <= 0.0f) return;
    ratio = from / to;

    while (*in != '\0' && o + 1 < sizeof out) {
        if (in[0] == '[' && strncmp(in + 1, "hold", 4) == 0) {
            const char *p = in + 5;
            long        ms = 0;
            int         digits = 0;

            while (*p == ' ' || *p == '\t') p++;
            while (*p >= '0' && *p <= '9') {
                ms = ms * 10 + (*p - '0');
                p++;
                digits++;
            }
            if (digits > 0 && *p == ']') {
                long v = (long)((float)ms * ratio + 0.5f);
                int  n;

                if (v < 1)     v = 1;
                if (v > 10000) v = 10000;

                n = snprintf(out + o, sizeof out - o, "[hold %ld]", v);
                /* Refuse rather than truncate: half a command written into the
                 * middle of a score is worse than a score that did not change. */
                if (n < 0 || (size_t)n >= sizeof out - o) return;
                o += (size_t)n;
                in = p + 1;
                continue;
            }
        }
        out[o++] = *in++;
    }

    if (*in != '\0') return;          /* would not have fitted; leave it alone */
    out[o] = '\0';
    memcpy(s->score, out, o + 1);
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

    /* ---- right: title, translator, tempo, reference ----
     *
     * The running total, against a panel 244 tall: 18 label + 34 box + 8,
     * 18 + 34 + 4, 34 view + 8, 24 + 24 tempo, 4, 28 buttons = 238. Six pixels
     * to spare, so change one of these numbers and check the FORMAT row still
     * lands inside `area` - there is no layout engine to catch it. */
    y = area.y;
    bm_label(ui, "TITLE", rx, y);
    /* The eighth note, right-aligned on the label row, which is otherwise
     * empty. It is the one figure here with no control of its own: it is half
     * the quarter by definition, and a slider for it would be a third view of
     * a number that already has two. */
    {
        char note[48];
        float bpm = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;
        float w;
        snprintf(note, sizeof note, "eighth %.0f ms", (double)(30000.0f / bpm));
        w = bm_text_measure(ui, BM_FONT_SMALL, note, 1.0f);
        bm_text_spaced(ui, BM_FONT_SMALL, note, rx + rw - w, y, BM_DIM);
    }
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

    /* Tempo, twice.
     *
     * The engine knows nothing about tempo - note lengths are absolute, and
     * `[hold]` is in milliseconds - so this is arithmetic the editor does on
     * the writer's behalf rather than a setting the synthesizer reads. It is
     * still a song parameter: it is saved in the file, because a score whose
     * holds were written at 96 BPM is a different piece read at 140.
     *
     * Both units get a control because both are things people arrive with. A
     * tempo is what a song *is*; a quarter in milliseconds is what actually
     * gets typed into a `[hold]`, and working it out from BPM in your head is
     * the sort of arithmetic that stops you writing.
     *
     * They are one number. `tempo` is what is stored, and setting the quarter
     * stores 60000/quarter - which is often not a round BPM, and that is
     * deliberate: rounding it would make the quarter you just asked for snap
     * to something else the moment you looked away. The displays round; the
     * stored value does not. */
    {
        Rectangle row = { rx, y, rw, 22.0f };
        float bpm = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;
        float v = bpm;

        if (bm_slider(ui, ID_TEMPO, row, "tempo", &v, 40.0f, 240.0f, "%.0f BPM") &&
            v > 0.0f) {
            s->song.tempo = v;
        }
        y += 24.0f;

        /* Recomputed rather than carried down from above: the slider on the
         * row before this one may have just changed it. */
        bpm = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;
        row.y = y;
        v = 60000.0f / bpm;
        if (bm_slider(ui, ID_QUARTER, row, "quarter", &v, 250.0f, 1500.0f,
                      "%.0f ms") && v > 1.0f) {
            s->song.tempo = 60000.0f / v;
        }
        y += 24.0f;

        /* Retime once the gesture is over, not while it is happening. A slider
         * reports a change every frame it is dragged, and rescaling the text
         * sixty times a second would walk 260 down a millisecond at a time -
         * the rounding is only harmless when it happens once. The focus test
         * covers the arrow keys, which move the value with the mouse up. */
        if (s->song.tempo > 0.0f && s->tempo_applied > 0.0f &&
            s->song.tempo != s->tempo_applied &&
            !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
            ui->focus != ID_TEMPO && ui->focus != ID_QUARTER) {
            retime_score(s, s->tempo_applied, s->song.tempo);
            s->tempo_applied = s->song.tempo;
        }
    }
    y += 4.0f;

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
"  TEMPO is the tempo the score's [hold] values are written at. At T beats\n"
"  per minute a quarter note is 60000/T milliseconds - 500 ms at 120 - and\n"
"  the panel has a control for each, so you can set whichever one you\n"
"  arrived with and read the other off. They are one number; moving either\n"
"  moves the other, and the eighth beside the title is half the quarter.\n"
"\n"
"  Changing it rewrites every [hold] in the score by the same ratio, so\n"
"  the song really does speed up or slow down and the file stays honest -\n"
"  its header and its holds always agree. The engine has no idea what a\n"
"  tempo is; [hold] is milliseconds and always was.\n"
"\n"
"  Consonants keep their own length, so a song does not scale exactly\n"
"  with the number: Daisy Bell runs 11.9 s at 116 and 9.9 s at 160, not\n"
"  8.6 s. That is what singing does too - it is the vowels that carry a\n"
"  note's duration, which is why [hold] only touches them.\n"
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
