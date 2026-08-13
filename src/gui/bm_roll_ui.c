/*
 * BENCmouth GUI - the note roll
 * See bm_roll_ui.h for what this is and what it deliberately does not decide.
 */

#include "bm_roll_ui.h"

#include <stdio.h>
#include <string.h>

/* Focus ids. One numbering across the whole window - see the note in main.c -
 * so these sit above the song panel's 2..6 and 9..10. */
#define ID_TITLE 11
#define ID_LYRIC 12
#define ID_PHON  13
#define ID_TEMPO 14
#define ID_HELP  15
#define ID_MENU  16

/* One semitone. Eleven pixels rather than a rounder twelve because the band the
 * panel gets is 148 px tall, and 12 would show twelve lanes - one short of the
 * octave from C4 to C5, so the roll opened unable to display the scale it opens
 * with. Thirteen lanes is the smallest interval that is musically a unit. */
#define LANE_H    11.0f
/* The strip of bar numbers above the grid, which is also where the playhead is
 * dragged. Eighteen rather than fourteen because the head lives in it and a
 * handle needs room to be a handle - at fourteen there was space for a seven by
 * three pixel mark, which is a thing you find by trying everything. */
#define RULER_H   18.0f
#define KEYS_W    34.0f     /* the keyboard down the left */
#define FOOT_H    64.0f     /* the two rows of controls underneath */
/* The horizontal scrollbar. Sixteen rather than ten: at ten the thumb was six
 * pixels of flat colour, which is a thing you can see and not a thing that
 * looks like it can be picked up. The grip marks below are the other half of
 * that - a plain rectangle reads as a readout, and three notches read as a
 * handle. */
#define BAR_H     16.0f
/* How near an edge counts as taking hold of it. Five pixels was what the
 * gesture had, and five pixels is a target nobody finds by accident - the note
 * simply moved instead, which reads as the edge drag not existing. Eight, and
 * never more than a third of the note, so a short note still has a middle. */
#define GRAB_PX    8.0f

#define LINE_H (BM_FONT_BODY + 4.0f + 10.0f)

/* Snap divisions, as fractions of a beat. Off is last because it is the one you
 * choose deliberately; everything else is a note value. */
static const char *SNAP_NAMES[] = { "1/4", "1/8", "1/16", "1/32", "off" };
static const int   SNAP_DIV[]   = { 1, 2, 4, 8, 0 };
#define SNAP_COUNT ((int)(sizeof SNAP_DIV / sizeof SNAP_DIV[0]))

/* Scratch for measuring. Static because it is the size of an engine and this is
 * called from a draw loop; the GUI is single-threaded and it is dead between
 * calls - the same arrangement retime_score uses in bm_song_ui.c. */
static bm_engine_storage g_scratch;
static bm_span           g_spans[BM_MAX_PHONEMES];

void bm_roll_ui_init(bm_roll_ui *s)
{
    if (s == 0) return;

    memset(s, 0, sizeof *s);
    bm_song_init(&s->song);

    snprintf(s->title, sizeof s->title, "%s", "Untitled");

    /* Singing wants prosody out of the way and a little vibrato, for the
     * reasons song mode gives. */
    s->song.voice.prosody = 0.0f;
    s->song.voice.vibrato = 0.28f;
    s->song.voice.vibrato_rate = 5.5f;
    s->song.tempo = 120.0f;
    s->tempo_applied = 120.0f;

    s->selected = -1;
    s->top_midi = 72;              /* C5 at the top, so C4 is in the middle */
    s->px_per_sec = 120.0f;
    s->snap = 1;                   /* eighths */
    s->dirty = 1;

    /* A scale, for the same reason song mode opens with one: every note in it
     * is audibly right or audibly wrong, which an unfamiliar tune is not.
     *
     * Quarter notes, which at the 120 BPM above is 500 ms - not the 400 the
     * score tab's scale uses. A roll is drawn against a grid and its own
     * opening bars have to sit on it, or the first thing anybody does moves
     * something. */
    {
        static const int PITCH[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
        int i;
        for (i = 0; i < 8; i++) {
            bm_roll_add(&s->song.roll, i * 500, 500, PITCH[i], "M IY1", "me");
        }
    }
}

/* ------------------------------------------------------------------ */

static float beat_ms(const bm_roll_ui *s)
{
    float bpm = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;
    return 60000.0f / bpm;
}

static int snap_ms(const bm_roll_ui *s)
{
    int div = SNAP_DIV[s->snap];
    if (div <= 0) return 0;
    return (int)(beat_ms(s) / (float)div + 0.5f);
}

static int snap_to(const bm_roll_ui *s, int ms)
{
    int step = snap_ms(s);
    if (step <= 0) return ms;
    return ((ms + step / 2) / step) * step;
}

/* ------------------------------------------------------------------ *
 * Compiling and measuring
 * ------------------------------------------------------------------ */

void bm_roll_ui_refresh(bm_roll_ui *s, const bm_voice *voice)
{
    bm_config cfg;
    size_t    n = 0, i;
    int       c = 0;

    if (s == 0) return;

    s->dirty = 0;
    s->unsingable = 0;
    memset(s->measured, 0, sizeof s->measured);

    s->skipped = bm_roll_compile(&s->song.roll, s->score, sizeof s->score);
    if (s->skipped < 0) {
        /* More notes than the score buffer holds. Nothing to measure, and the
         * status line says so. */
        s->score[0] = '\0';
        s->skipped = 0;
        s->unsingable = 1;
        return;
    }
    if (s->score[0] == '\0') return;

    bm_config_default(&cfg);
    if (voice != 0) cfg.voice = *voice;

    if (bm_measure(&g_scratch, &cfg, s->score, 0, g_spans,
                   sizeof g_spans / sizeof g_spans[0], &n, 0) != BM_OK) {
        /* A phoneme typed into a note that is not a phoneme. The note keeps its
         * drawn length on screen and SING will report why. */
        s->unsingable = 1;
        return;
    }
    if (n > sizeof g_spans / sizeof g_spans[0]) n = sizeof g_spans / sizeof g_spans[0];

    /* Groups are numbered in compile order, and the compiler skips notes with
     * nothing to sing - so walking the roll the same way is what maps a group
     * back to the note that produced it. */
    for (i = 0; i < (size_t)s->song.roll.count; i++) {
        const bm_note *note = &s->song.roll.note[i];
        const char    *p = note->phon;
        size_t         k;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        c++;
        for (k = 0; k < n; k++) {
            if (g_spans[k].group == (uint16_t)c) s->measured[i] += (int)g_spans[k].length_ms;
        }
    }
}

/* The word in a note, spelled. Empty in, empty out - clearing the lyric clears
 * the phonemes, because a note showing phonemes for a word that is no longer
 * there is worse than an empty one. */
static void respell(bm_note *note, int use_dict)
{
    char   out[BM_NOTE_PHON_MAX * 4];
    size_t n = 0;
    unsigned flags = use_dict ? 0u : BM_TEXT_NO_DICT;

    if (note->lyric[0] == '\0') { note->phon[0] = '\0'; return; }

    if (bm_text_to_phonemes_ex(note->lyric, 0, out, sizeof out, &n, flags)
            != BM_OK || n == 0) {
        note->phon[0] = '\0';
        return;
    }
    snprintf(note->phon, sizeof note->phon, "%s", out);
}

/* ------------------------------------------------------------------ *
 * The grid
 * ------------------------------------------------------------------ */

static float time_x(const bm_roll_ui *s, Rectangle lanes, float ms)
{
    return lanes.x + (ms - s->scroll_ms) * s->px_per_sec / 1000.0f;
}

static int x_time(const bm_roll_ui *s, Rectangle lanes, float x)
{
    return (int)(s->scroll_ms + (x - lanes.x) * 1000.0f / s->px_per_sec + 0.5f);
}

static float midi_y(const bm_roll_ui *s, Rectangle lanes, int midi)
{
    return lanes.y + (float)(s->top_midi - midi) * LANE_H;
}

static int y_midi(const bm_roll_ui *s, Rectangle lanes, float y)
{
    return s->top_midi - (int)((y - lanes.y) / LANE_H);
}

static int is_black_key(int midi)
{
    int pc = ((midi % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

/* How near an edge the pointer is, for the note at `index`: 0 for the left
 * edge, 1 for the right, -1 for neither. Never more than a third of the note
 * from either end, so a note two grabs wide is not all edge. */
static int edge_at(const bm_roll_ui *s, Rectangle lanes, int index, float x)
{
    const bm_note *n;
    float nx, nw, grab;

    if (index < 0 || index >= s->song.roll.count) return -1;
    n = &s->song.roll.note[index];
    nx = time_x(s, lanes, (float)n->start);
    nw = (float)n->length * s->px_per_sec / 1000.0f;

    grab = GRAB_PX;
    if (grab > nw / 3.0f) grab = nw / 3.0f;
    if (grab < 2.0f) return -1;

    if (x >= nx + nw - grab) return 1;
    if (x <= nx + grab)      return 0;
    return -1;
}

/* Zooms about the middle of what is on screen, so the bar you are looking at
 * stays where it is. Shared by the buttons and control-wheel, which zooms about
 * the pointer instead - the difference is deliberate: a button has no pointer
 * to be about. */
static void zoom_by(bm_roll_ui *s, Rectangle lanes, float by)
{
    float middle = s->scroll_ms + lanes.width * 500.0f / s->px_per_sec;

    s->px_per_sec *= by;
    if (s->px_per_sec < 8.0f)   s->px_per_sec = 8.0f;
    if (s->px_per_sec > 600.0f) s->px_per_sec = 600.0f;

    s->scroll_ms = middle - lanes.width * 500.0f / s->px_per_sec;
    if (s->scroll_ms < 0.0f) s->scroll_ms = 0.0f;
}

/* Which note is under the point, or -1. Searched backwards so that when two
 * notes touch, the later one wins its own left edge. */
static int note_at(const bm_roll_ui *s, Rectangle lanes, float x, float y)
{
    int i;

    for (i = s->song.roll.count - 1; i >= 0; i--) {
        const bm_note *n = &s->song.roll.note[i];
        float nx = time_x(s, lanes, (float)n->start);
        float nw = (float)n->length * s->px_per_sec / 1000.0f;
        float ny = midi_y(s, lanes, n->midi);

        if (nw < 3.0f) nw = 3.0f;
        if (x >= nx && x < nx + nw && y >= ny && y < ny + LANE_H) return i;
    }
    return -1;
}

static void scroll_into_view(bm_roll_ui *s, Rectangle lanes, int ms)
{
    float span = lanes.width * 1000.0f / s->px_per_sec;

    if ((float)ms < s->scroll_ms) s->scroll_ms = (float)ms;
    else if ((float)ms > s->scroll_ms + span * 0.9f) {
        s->scroll_ms = (float)ms - span * 0.9f;
    }
    if (s->scroll_ms < 0.0f) s->scroll_ms = 0.0f;
}

/* ------------------------------------------------------------------ */

static void draw_grid(bm_ui *ui, const bm_roll_ui *s, Rectangle ruler,
                      Rectangle lanes)
{
    float beat = beat_ms(s);
    int   lanes_n = (int)(lanes.height / LANE_H);
    int   i;
    float t;

    /* Lanes, striped by black key and white, which is what makes a pitch
     * readable without counting rows from the keyboard. */
    for (i = 0; i < lanes_n; i++) {
        int   midi = s->top_midi - i;
        float y = lanes.y + (float)i * LANE_H;
        DrawRectangle((int)lanes.x, (int)y, (int)lanes.width, (int)LANE_H,
                      is_black_key(midi) ? BM_BG : BM_PANEL);
        /* An octave line, so C is findable at a glance. */
        if (midi % 12 == 0) {
            DrawRectangle((int)lanes.x, (int)y + (int)LANE_H - 1,
                          (int)lanes.width, 1, BM_BORDER);
        }
    }

    /* Beats, and a brighter line every four of them. Nothing here knows about
     * time signatures; four is what the bar line means until something does. */
    if (beat > 1.0f) {
        long b = (long)(s->scroll_ms / beat);
        for (;; b++) {
            float x;
            t = (float)b * beat;
            x = time_x(s, lanes, t);
            if (x > lanes.x + lanes.width) break;
            if (x >= lanes.x) {
                int bar = (b % 4) == 0;
                DrawRectangle((int)x, (int)lanes.y, 1, (int)lanes.height,
                              bar ? BM_BORDER : BM_EDGE);
                if (bar) {
                    char lab[16];
                    snprintf(lab, sizeof lab, "%ld", b / 4 + 1);
                    bm_text(ui, BM_FONT_SMALL, lab, x + 3.0f,
                            ruler.y + 1.0f, BM_DIM);
                }
            }
        }
    }
}

static void draw_keys(bm_ui *ui, const bm_roll_ui *s, Rectangle keys)
{
    int lanes_n = (int)(keys.height / LANE_H);
    int i;

    for (i = 0; i < lanes_n; i++) {
        int   midi = s->top_midi - i;
        float y = keys.y + (float)i * LANE_H;
        int   black = is_black_key(midi);

        DrawRectangle((int)keys.x, (int)y, (int)keys.width, (int)LANE_H - 1,
                      black ? BM_BG : BM_DIM);
        /* Only the Cs are labelled. Every lane named turns the strip into a
         * wall of text at this size, and C is the one you count from. */
        if (midi % 12 == 0) {
            char name[8];
            bm_roll_note_name(midi, name, sizeof name);
            bm_text(ui, BM_FONT_SMALL, name, keys.x + 2.0f, y - 2.0f, BM_TEXT);
        }
    }
}

static void draw_notes(bm_ui *ui, const bm_roll_ui *s, Rectangle lanes,
                       int hover)
{
    int i;

    for (i = 0; i < s->song.roll.count; i++) {
        const bm_note *n = &s->song.roll.note[i];
        float x = time_x(s, lanes, (float)n->start);
        float w = (float)n->length * s->px_per_sec / 1000.0f;
        float y = midi_y(s, lanes, n->midi);
        int   selected = (i == s->selected);
        /* A tied note has no phonemes of its own and is not silent for it - it
         * sings the vowel it inherits. Reading emptiness as "not spelled yet"
         * would draw every slur as an unfinished note. */
        int   silent = n->tie ? (bm_roll_tied_vowel(&s->song.roll, i) == 0)
                              : (n->phon[0] == '\0');
        Rectangle r;

        if (w < 3.0f) w = 3.0f;
        if (x + w < lanes.x || x > lanes.x + lanes.width) continue;
        if (y + LANE_H < lanes.y || y > lanes.y + lanes.height) continue;

        r = (Rectangle){ x, y + 1.0f, w, LANE_H - 2.0f };

        /* What the note really sounds for, when that is longer than the box it
         * was drawn as. A consonant cluster that will not fit inside its note
         * pushes past the end of it, and this is that overhang - drawn in the
         * warning colour, past the right edge, so it reads as the note running
         * over rather than as part of it. */
        if (s->measured[i] > n->length + 15) {
            float mw = (float)(s->measured[i] - n->length) * s->px_per_sec / 1000.0f;
            DrawRectangle((int)(x + w), (int)(y + 3.0f), (int)mw,
                          (int)LANE_H - 6, BM_AMBER);
        }

        if (silent) {
            /* Nothing to sing yet. An outline rather than a fill, because it is
             * a note that has been drawn and not yet spelled. */
            DrawRectangleLinesEx(r, 1.0f, selected ? BM_TEXT : BM_BORDER);
        } else {
            DrawRectangleRec(r, selected ? BM_TEXT : BM_ACCENT);
            if (selected) DrawRectangleLinesEx(r, 1.0f, BM_TEXT);
        }

        /* The edges, on the note being pointed at and on the selected one.
         * Two pixels of a brighter colour at each end, which is the only thing
         * on screen that says the ends of a note are controls - the cursor
         * says so too, but only once you are already there. */
        if ((selected || i == hover) && !silent && w > 8.0f) {
            DrawRectangle((int)x, (int)r.y, 2, (int)r.height, BM_BG);
            DrawRectangle((int)(x + w) - 2, (int)r.y, 2, (int)r.height, BM_BG);
        }

        /* A tie and two separate notes are the difference between singing and
         * reciting, and adjacent notes touch exactly - so the join has to say
         * which it is, both ways round.
         *
         * In the same lane the two would otherwise read as one long note, so an
         * untied note begins with a line cut into it. Across lanes the opposite
         * problem: nothing joins them, so a tie draws the line that does. */
        if (i > 0) {
            const bm_note *p = &s->song.roll.note[i - 1];
            int touching = (p->start + p->length == n->start);

            if (touching && !n->tie && p->midi == n->midi) {
                DrawRectangle((int)x, (int)y + 1, 1, (int)LANE_H - 2, BM_BG);
            } else if (touching && n->tie && p->midi != n->midi) {
                float py = midi_y(s, lanes, p->midi);
                float top = (py < y) ? py : y;
                float bot = (py < y) ? y : py;
                DrawRectangle((int)x, (int)(top + LANE_H * 0.5f), 1,
                              (int)(bot - top), selected ? BM_TEXT : BM_ACCENT);
            }
        }

        /* The word, inside the note, when there is room for any of it. A tied
         * note shows the vowel it is carrying rather than a word of its own,
         * because a word of its own is the one thing it has not got.
         *
         * Clipped to the note and then to the grid again by hand, because
         * BeginScissorMode replaces the clip rather than nesting inside it and
         * EndScissorMode turns clipping off altogether - so ending this one
         * would leave every note after it, and the playhead, free to draw over
         * the keyboard and out of the panel. Restoring is what closing means
         * here. */
        if (w > 18.0f) {
            const char *label = n->tie ? bm_roll_tied_vowel(&s->song.roll, i)
                              : n->lyric[0] != '\0' ? n->lyric : n->phon;
            if (label != 0 && label[0] != '\0') {
                float cx = (x + 2.0f > lanes.x) ? x + 2.0f : lanes.x;
                float cr = x + w - 1.0f;
                if (cr > lanes.x + lanes.width) cr = lanes.x + lanes.width;

                if (cr > cx) {
                    BeginScissorMode((int)cx, (int)y, (int)(cr - cx),
                                     (int)LANE_H);
                    bm_text(ui, BM_FONT_SMALL, label, x + 3.0f, y - 2.0f,
                            silent ? BM_DIM : BM_BG);
                    BeginScissorMode((int)lanes.x, (int)lanes.y,
                                     (int)lanes.width, (int)lanes.height);
                }
            }
        }
    }
}

/* What the right-click menu offers. Static because bm_menu keeps the pointer -
 * a menu is drawn a frame after it is opened, so a local would be gone. */
static const char *MENU_ITEMS[] = {
    "Octave up",
    "Octave down",
    "Semitone up",
    "Semitone down",
    "-",
    "Tie / untie",
    "-",
    "Delete"
};
#define MENU_COUNT ((int)(sizeof MENU_ITEMS / sizeof MENU_ITEMS[0]))

enum {
    MENU_OCT_UP = 1, MENU_OCT_DOWN, MENU_SEMI_UP, MENU_SEMI_DOWN,
    MENU_SEP1, MENU_TIE, MENU_SEP2, MENU_DELETE
};

/* ------------------------------------------------------------------ *
 * Undo
 * ------------------------------------------------------------------ */

/* Tokens. A gesture is one undo step, so everything that reports a change
 * repeatedly while it is happening carries a token that says which gesture it
 * is: marks with the same one are the same run. Zero never coalesces, which is
 * what a discrete action wants. */
#define TOK_DRAG(i)  (0x10000u + (unsigned)(i))
#define TOK_WORD(i)  (0x20000u + (unsigned)(i))
#define TOK_PHON(i)  (0x30000u + (unsigned)(i))
#define TOK_TEMPO    (0x40000u)

/* Records the roll as it is, before whatever is about to change it. */
static void mark(bm_roll_ui *s, unsigned token)
{
    bm_roll_state now;

    now.roll = s->song.roll;
    now.tempo = s->song.tempo;
    now.selected = s->selected;
    bm_roll_mark(&s->hist, token, &now);
}

static void step(bm_roll_ui *s, int forward)
{
    bm_roll_state now;
    int moved;

    now.roll = s->song.roll;
    now.tempo = s->song.tempo;
    now.selected = s->selected;

    moved = forward ? bm_roll_redo(&s->hist, &now) : bm_roll_undo(&s->hist, &now);
    if (!moved) {
        snprintf(s->said, sizeof s->said, "nothing to %s",
                 forward ? "redo" : "undo");
        return;
    }

    s->song.roll = now.roll;
    s->song.tempo = now.tempo;
    s->selected = now.selected;
    if (s->selected >= s->song.roll.count) s->selected = s->song.roll.count - 1;

    /* The tempo came back with it, so what the roll's times are written
     * against has to come back too - otherwise the next tempo change would
     * retime from a number that is no longer true. */
    s->tempo_applied = s->song.tempo;

    memset(&s->lyric_st, 0, sizeof s->lyric_st);
    memset(&s->phon_st, 0, sizeof s->phon_st);
    s->dirty = 1;
    snprintf(s->said, sizeof s->said, "%s  -  %d notes",
             forward ? "redone" : "undone", s->song.roll.count);
}

/* ------------------------------------------------------------------ */

/* A tie needs something to be tied to. Not whether it would *sound* - that is
 * bm_roll_check_ties's job and it runs after every edit - only whether offering
 * the control makes sense at all. */
static int tie_possible(const bm_roll_ui *s, int index)
{
    return index > 0 && index < s->song.roll.count;
}

/* Tying pulls the note back onto the end of the one before it.
 *
 * A tie means "carry on the sound that is happening", so a gap in front of one
 * is a contradiction rather than a variation - and closing the gap is what
 * somebody asking for a tie meant. Untying leaves the note where it is: it has
 * become an ordinary note that happens to start there, which is true. */
static void set_tie(bm_roll_ui *s, int index, int on)
{
    bm_note *n;

    if (index <= 0 || index >= s->song.roll.count) return;
    mark(s, 0u);
    n = &s->song.roll.note[index];

    if (on) {
        const bm_note *p = &s->song.roll.note[index - 1];
        n->start = p->start + p->length;
        n->tie = 1u;
        /* Its own word goes, because it no longer has one - and leaving it
         * there would mean a note that shows one word and sings another. */
        n->phon[0] = '\0';
        n->lyric[0] = '\0';
        bm_roll_deoverlap(&s->song.roll, index);
    } else {
        n->tie = 0u;
    }

    bm_roll_check_ties(&s->song.roll);
    memset(&s->lyric_st, 0, sizeof s->lyric_st);
    memset(&s->phon_st, 0, sizeof s->phon_st);
    s->dirty = 1;
}

static void handle_mouse(bm_ui *ui, bm_roll_ui *s, Rectangle ruler,
                         Rectangle lanes, int use_dict)
{
    Vector2 m = GetMousePosition();
    int     inside = CheckCollisionPointRec(m, lanes);
    float   wheel;

    if (ui->blocking && CheckCollisionPointRec(m, ui->block)) return;

    /* The bar under the grid. Taken hold of anywhere along it: pressing beside
     * the thumb jumps there, which is what a scrollbar does everywhere. */
    if (CheckCollisionPointRec(m, s->bar) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        float w = s->bar.width * s->bar_span / s->bar_total;
        float t = (s->bar_total > s->bar_span)
                      ? s->scroll_ms / (s->bar_total - s->bar_span) : 0.0f;
        float tx;

        if (w < 24.0f) w = 24.0f;
        tx = s->bar.x + (s->bar.width - w) * t;

        /* Grabbed on the thumb, the thumb stays under the pointer; grabbed
         * beside it, it jumps there and then does. Anything else makes the
         * thumb leap out from under the finger that took hold of it. */
        if (m.x >= tx && m.x <= tx + w) s->scroll_grab = m.x - tx;
        else                            s->scroll_grab = w * 0.5f;

        s->drag = BM_ROLL_DRAG_SCROLL;
    }
    if (s->drag == BM_ROLL_DRAG_SCROLL) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s->drag = BM_ROLL_DRAG_NONE;
        } else if (s->bar.width > 1.0f && s->bar_total > s->bar_span) {
            float w = s->bar.width * s->bar_span / s->bar_total;
            float room;

            if (w < 24.0f) w = 24.0f;
            room = s->bar.width - w;
            if (room > 1.0f) {
                float t = (m.x - s->scroll_grab - s->bar.x) / room;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                s->scroll_ms = t * (s->bar_total - s->bar_span);
            }
        }
        return;
    }

    /* The ruler is the playhead's. Pressing in it moves the head there and
     * dragging scrubs it, which is the gesture everything with a timeline in it
     * uses and the one thing the score tab could never offer: the engine plays
     * an utterance from the beginning or not at all. */
    if (CheckCollisionPointRec(m, ruler) &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s->drag = BM_ROLL_DRAG_HEAD;
        s->drag_from = m;
        s->drag_moved = 1;
    }
    if (s->drag == BM_ROLL_DRAG_HEAD) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s->drag = BM_ROLL_DRAG_NONE;
        } else {
            int at = x_time(s, lanes, m.x);
            if (at < 0) at = 0;
            s->head_ms = (float)at;
            s->head_moved = 1;
        }
        return;
    }

    wheel = GetMouseWheelMove();
    if (inside && wheel != 0.0f) {
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
            /* Zoom about the pointer, so the note you are looking at stays
             * under it - zooming about the left edge walks the thing you were
             * working on off the screen. */
            int   at = x_time(s, lanes, m.x);
            float was = s->px_per_sec;
            s->px_per_sec *= (wheel > 0.0f) ? 1.15f : 1.0f / 1.15f;
            if (s->px_per_sec < 8.0f)   s->px_per_sec = 8.0f;
            if (s->px_per_sec > 600.0f) s->px_per_sec = 600.0f;
            if (s->px_per_sec != was) {
                s->scroll_ms = (float)at - (m.x - lanes.x) * 1000.0f / s->px_per_sec;
            }
        } else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            s->scroll_ms -= wheel * 200.0f * 1000.0f / s->px_per_sec;
        } else {
            s->top_midi += (int)wheel;
            if (s->top_midi > 108) s->top_midi = 108;
            if (s->top_midi < 24)  s->top_midi = 24;
        }
        if (s->scroll_ms < 0.0f) s->scroll_ms = 0.0f;
    }

    if (inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int hit = note_at(s, lanes, m.x, m.y);

        if (hit >= 0) {
            const bm_note *n = &s->song.roll.note[hit];
            int edge = edge_at(s, lanes, hit, m.x);

            s->selected = hit;
            s->drag_index = hit;
            /* Either edge. The right one is how long the note is and the left
             * one is where it starts, which is the same gesture from the other
             * end and was simply missing. */
            s->drag = (edge == 1) ? BM_ROLL_DRAG_LENGTH
                    : (edge == 0) ? BM_ROLL_DRAG_START
                                  : BM_ROLL_DRAG_MOVE;
            s->drag_grab = x_time(s, lanes, m.x) - n->start;
            s->drag_from = m;
            s->drag_moved = 0;
            /* Before the drag rather than during it: what is worth going back
             * to is where the note was when it was taken hold of. */
            mark(s, TOK_DRAG(hit));
            memset(&s->lyric_st, 0, sizeof s->lyric_st);
            memset(&s->phon_st, 0, sizeof s->phon_st);
            ui->focus = 0;

            /* Selecting a note puts the caret in its word, exactly as drawing
             * one does. Typing a syllable into the note you just clicked is the
             * whole gesture this tab exists for, and having to find the box
             * first - with no cue that you had to - is not it. A tied note has
             * no word, so there is nothing to put the caret in. */
            if (!n->tie) s->focus_word = 1;
        } else {
            /* Draw a new note. Press and drag right sets its length; a plain
             * click leaves it one step long, which is what the grid is set to
             * and so is the length you were most likely reaching for. */
            int start = snap_to(s, x_time(s, lanes, m.x));
            int len = snap_ms(s);
            int midi = y_midi(s, lanes, m.y);
            int at;

            if (start < 0) start = 0;
            if (len <= 0) len = 250;
            if (midi < 12 || midi > 108) return;

            mark(s, 0u);
            at = bm_roll_add(&s->song.roll, start, len, midi, "", "");
            if (at < 0) return;                    /* the roll is full */

            s->selected = at;
            s->drag_index = at;
            s->drag = BM_ROLL_DRAG_LENGTH;
            s->drag_grab = 0;
            s->drag_from = m;
            /* A note that has just been drawn *is* the drag: it was created at
             * the length the grid is set to, and dragging out from here is how
             * you say you wanted a different one. */
            s->drag_moved = 1;
            s->dirty = 1;
            bm_roll_deoverlap(&s->song.roll, at);

            /* Straight into the lyric box: the point of the tab is typing a
             * syllable into a note, so drawing one and then having to find
             * where to type it would be the wrong number of gestures. */
            memset(&s->lyric_st, 0, sizeof s->lyric_st);
            memset(&s->phon_st, 0, sizeof s->phon_st);

            /* Asked for rather than taken, and granted at the end of the panel.
             * Setting ui->focus here would be undone before it was ever seen:
             * the text box runs later in the same frame, sees a mouse press
             * that is not over it, and drops the focus it has just been given.
             * A press is one event, and the widget that did not receive it must
             * not be able to act on it either. */
            s->focus_word = 1;
        }
    }

    if (s->drag != BM_ROLL_DRAG_NONE) {
        /* Two pixels, so that the shake in a click does not count as a drag. */
        if (!s->drag_moved &&
            ((m.x - s->drag_from.x) * (m.x - s->drag_from.x) +
             (m.y - s->drag_from.y) * (m.y - s->drag_from.y) > 4.0f)) {
            s->drag_moved = 1;
        }

        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s->drag = BM_ROLL_DRAG_NONE;
            s->dirty = 1;
            /* The gesture is over, so the next one is a step of its own even
             * if it takes hold of the same note. */
            bm_roll_mark_end(&s->hist);
        } else if (!s->drag_moved) {
            /* Still a click, not yet a drag. Nothing has been asked for. */
        } else if (s->drag_index >= 0 && s->drag_index < s->song.roll.count) {
            bm_note *n = &s->song.roll.note[s->drag_index];

            if (s->drag == BM_ROLL_DRAG_LENGTH) {
                int len = snap_to(s, x_time(s, lanes, m.x) - n->start);
                int least = snap_ms(s);
                if (least <= 0) least = 30;
                if (len < least) len = least;
                if (len > 10000) len = 10000;
                n->length = len;
            } else if (s->drag == BM_ROLL_DRAG_START) {
                /* The left edge moves the beginning and leaves the end where
                 * it is, which is what dragging that edge means everywhere
                 * else. The note is never shortened past the minimum, and
                 * never dragged through its own end. */
                int end = n->start + n->length;
                int start = snap_to(s, x_time(s, lanes, m.x));
                int least = snap_ms(s);

                if (least <= 0) least = 30;
                if (start < 0) start = 0;
                if (start > end - least) start = end - least;
                if (start < 0) start = 0;
                n->start = start;
                n->length = end - start;
                s->drag_index = bm_roll_sort(&s->song.roll, s->drag_index);
                s->selected = s->drag_index;
            } else {
                int start = snap_to(s, x_time(s, lanes, m.x) - s->drag_grab);
                int midi = y_midi(s, lanes, m.y);

                if (start < 0) start = 0;
                if (midi >= 12 && midi <= 108 && midi != n->midi) {
                    n->midi = midi;
                    /* Say it, at the pitch it has just arrived at. Choosing a
                     * note by ear is the point of dragging it up and down, and
                     * doing that in silence means drag, let go, listen, and go
                     * back for another try. */
                    s->audition = s->drag_index + 1;
                }
                n->start = start;
                s->drag_index = bm_roll_sort(&s->song.roll, s->drag_index);
                s->selected = s->drag_index;
            }
            bm_roll_deoverlap(&s->song.roll, s->drag_index);
            /* A note dragged away from the one it was tied to is not tied to
             * it any more. The tie is a claim about two notes touching, and it
             * stops being true the moment they do not. */
            bm_roll_check_ties(&s->song.roll);
            s->dirty = 1;
            /* Read the start back out of the array rather than through `n`,
             * which the re-sort above may have left pointing at a different
             * note - dragging one past its neighbour is exactly when the note
             * at an index stops being the note that was there. */
            scroll_into_view(s, lanes,
                             s->song.roll.note[s->drag_index].start);
        }
    }

    /* Delete, but only when nothing is being typed into - Backspace belongs to
     * whichever field has the caret, and taking it away from there would delete
     * a note every time somebody mistyped a syllable. */
    if (ui->focus == 0 && s->selected >= 0 &&
        (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE))) {
        mark(s, 0u);
        bm_roll_remove(&s->song.roll, s->selected);
        if (s->selected >= s->song.roll.count) s->selected = s->song.roll.count - 1;
        bm_roll_check_ties(&s->song.roll);
        s->dirty = 1;
    }

    /* ---- undo ----
     *
     * Taken here rather than in the text boxes, which have no undo of their own:
     * a word typed into a note is an edit to the roll, and going back through
     * them one field at a time would be a second history that disagreed with
     * this one. Super as well as control, because that is the modifier the rest
     * of the widget set already accepts on macOS.
     */
    {
        int ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                   IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER);
        int shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        if (ctrl && IsKeyPressed(KEY_Z))      step(s, shift);
        else if (ctrl && IsKeyPressed(KEY_Y)) step(s, 1);
    }

    /* ---- the right-click menu ----
     *
     * Opened on the note under the pointer, which it also selects: a menu that
     * acted on something other than what was clicked would be a menu nobody
     * could trust.
     */
    if (inside && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        int hit = note_at(s, lanes, m.x, m.y);
        if (hit >= 0) {
            s->selected = hit;
            memset(&s->lyric_st, 0, sizeof s->lyric_st);
            memset(&s->phon_st, 0, sizeof s->phon_st);
            ui->focus = 0;
            bm_menu(ui, ID_MENU, m.x, m.y, MENU_ITEMS, MENU_COUNT);
        }
    }

    /* The pointer says what the edge under it will do. Without this the edge
     * drag is invisible: there is nothing to see and nothing to feel, so a
     * note that could be resized reads exactly like one that could not. */
    if (!ui->blocking && s->drag == BM_ROLL_DRAG_NONE && inside) {
        int over = note_at(s, lanes, m.x, m.y);
        SetMouseCursor(edge_at(s, lanes, over, m.x) >= 0
                           ? MOUSE_CURSOR_RESIZE_EW
                           : MOUSE_CURSOR_DEFAULT);
    } else if (s->drag == BM_ROLL_DRAG_LENGTH || s->drag == BM_ROLL_DRAG_START) {
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    } else if (!inside && s->drag == BM_ROLL_DRAG_NONE) {
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    /* T, for the same reason and under the same rule: it is a letter, so it
     * belongs to whichever field has the caret whenever one does. */
    if (ui->focus == 0 && s->selected > 0 && IsKeyPressed(KEY_T)) {
        set_tie(s, s->selected, !s->song.roll.note[s->selected].tie);
    }

    (void)use_dict;
}

/* ------------------------------------------------------------------ */

int bm_roll_panel(bm_ui *ui, bm_roll_ui *s, Rectangle area, int use_dict,
                  const bm_voice *voice, int playhead_ms)
{
    int       action = BM_ROLL_ACT_NONE;
    Rectangle keys, ruler, lanes;
    float     y;
    int       hover;

    if (ui == 0 || s == 0) return BM_ROLL_ACT_NONE;

    if (s->selected >= s->song.roll.count) s->selected = s->song.roll.count - 1;

    /* Something is selected as soon as there is anything to select, so the two
     * fields below have a note to be about the moment the tab is opened. The
     * first one, because that is where a song starts and where the playhead
     * is. */
    if (s->selected < 0 && s->song.roll.count > 0) s->selected = 0;

    keys  = (Rectangle){ area.x, area.y + 18.0f + RULER_H, KEYS_W,
                         area.height - 18.0f - RULER_H - FOOT_H - BAR_H };
    ruler = (Rectangle){ area.x + KEYS_W, area.y + 18.0f,
                         area.width - KEYS_W, RULER_H };
    lanes = (Rectangle){ ruler.x, ruler.y + RULER_H, ruler.width, keys.height };

    /* ---- the label row: what the roll is, and anything wrong with it ---- */
    bm_label(ui, "ROLL  -  DRAW A NOTE, TYPE THE SYLLABLE, DRAG THE LENGTH",
             area.x, area.y);
    {
        char note[96];
        float w;
        int total = bm_roll_length(&s->song.roll);

        if (s->unsingable) {
            snprintf(note, sizeof note, "this roll will not sing");
        } else if (s->skipped > 0) {
            snprintf(note, sizeof note, "%d note%s not spelled  -  %d.%02d s",
                     s->skipped, s->skipped == 1 ? "" : "s",
                     total / 1000, (total % 1000) / 10);
        } else {
            snprintf(note, sizeof note, "%d notes  -  %d.%02d s",
                     s->song.roll.count, total / 1000, (total % 1000) / 10);
        }
        w = bm_text_measure(ui, BM_FONT_SMALL, note, 1.0f);
        bm_text_spaced(ui, BM_FONT_SMALL, note, area.x + area.width - w, area.y,
                       s->unsingable ? BM_ALERT : BM_DIM);
    }

    /* Which note the pointer is over, worked out before anything is drawn so
     * that the note can show its edges as the mouse reaches it rather than a
     * frame later. */
    {
        Vector2 m = GetMousePosition();
        hover = (!ui->blocking && CheckCollisionPointRec(m, lanes))
                    ? note_at(s, lanes, m.x, m.y) : -1;
    }

    /* ---- the grid ---- */
    DrawRectangleRec(ruler, BM_BG);
    bm_panel(lanes);
    /* The scissor takes in the ruler as well as the lanes, because the bar
     * numbers are drawn by the same pass that draws the bar lines - they are
     * the same loop over the same beats, and splitting them in two so that each
     * could have its own clip would be two places to get the spacing wrong. */
    BeginScissorMode((int)ruler.x, (int)ruler.y, (int)ruler.width,
                     (int)(RULER_H + lanes.height));
    draw_grid(ui, s, ruler, lanes);
    draw_notes(ui, s, lanes, hover);

    /* The playhead: where the transport has got to while it is running, and
     * where it would start from when it is not. One mark either way, because
     * they are one thing - the head you dragged is the head that plays. */
    {
        float at = (playhead_ms >= 0) ? (float)playhead_ms : s->head_ms;
        float x = time_x(s, lanes, at);

        if (playhead_ms >= 0) s->head_ms = (float)playhead_ms;
        if (x >= lanes.x && x <= lanes.x + lanes.width) {
            DrawRectangle((int)x, (int)lanes.y, 1, (int)lanes.height,
                          playhead_ms >= 0 ? BM_TEXT : BM_DIM);
        }
    }
    EndScissorMode();
    DrawRectangleLinesEx(lanes, 1.0f, BM_BORDER);
    draw_keys(ui, s, keys);

    /* ---- how far along, and how much there is ----
     *
     * Scrolling sideways was shift-wheel and nothing else: a gesture with no
     * sign that it exists, on the one axis a song is long in. A bar says both
     * things a scrollbar says - where you are, and how much of the whole you
     * are looking at - and can be dragged.
     */
    {
        Rectangle bar = { lanes.x, lanes.y + lanes.height + 2.0f,
                          lanes.width, BAR_H - 2.0f };
        float span = lanes.width * 1000.0f / s->px_per_sec;   /* ms on screen */
        float total = (float)bm_roll_length(&s->song.roll) + span * 0.25f;
        float t, w;

        if (total < span) total = span;
        if (s->scroll_ms > total - span) s->scroll_ms = total - span;
        if (s->scroll_ms < 0.0f) s->scroll_ms = 0.0f;

        DrawRectangleRec(bar, BM_BG);
        DrawRectangleLinesEx(bar, 1.0f, BM_EDGE);

        w = bar.width * span / total;
        if (w < 24.0f) w = 24.0f;
        t = (total > span) ? s->scroll_ms / (total - span) : 0.0f;

        {
            /* Dim when the whole song is already on screen. A full-width bar in
             * the colour of a control says "drag me" about something that has
             * nowhere to go. */
            int   live = (total > span + 1.0f);
            Rectangle thumb = { bar.x + (bar.width - w) * t, bar.y + 2.0f,
                                w, bar.height - 4.0f };
            Vector2 m = GetMousePosition();
            int over = live && !ui->blocking &&
                       CheckCollisionPointRec(m, bar);

            DrawRectangleRec(thumb, live ? (over ? BM_TEXT : BM_ACCENT)
                                         : BM_EDGE);

            /* Three notches down the middle. The one mark every toolkit uses
             * for "this is a handle", and the only thing here that says so
             * before you have already tried it. */
            if (live && w > 26.0f) {
                float cx = thumb.x + w * 0.5f;
                int   k;
                for (k = -1; k <= 1; k++) {
                    DrawRectangle((int)(cx + (float)k * 4.0f),
                                  (int)thumb.y + 2, 1,
                                  (int)thumb.height - 4, BM_BG);
                }
            }
        }

        s->bar = bar;
        s->bar_span = span;
        s->bar_total = total;
    }

    /* The playhead's handle: a wedge pointing down at the line it controls.
     *
     * It was a seven by three pixel dash, which is why the strip above the grid
     * read as a row of numbers rather than as something to take hold of. A
     * shape that points at what it moves is the whole of the cue - and the
     * whole ruler is the target, not just the wedge, so it is easier to hit
     * than it looks. */
    {
        float x = time_x(s, lanes, s->head_ms);

        if (x >= ruler.x && x <= ruler.x + ruler.width) {
            Vector2 m = GetMousePosition();
            int     warm = (!ui->blocking &&
                            (s->drag == BM_ROLL_DRAG_HEAD ||
                             CheckCollisionPointRec(m, ruler)));
            Color   c = warm ? BM_TEXT : BM_ACCENT;
            float   top = ruler.y + 3.0f;
            float   bot = ruler.y + RULER_H - 1.0f;
            float   half = 6.0f;

            /* Kept inside the ruler. At the very start of the song the head
             * sits on the left edge, and half a wedge would otherwise be drawn
             * across the keyboard strip beside it. */
            BeginScissorMode((int)ruler.x, (int)ruler.y, (int)ruler.width,
                             (int)RULER_H);

            /* Clockwise in raylib's screen space, which is what its triangle
             * winding wants; the other order draws nothing at all. */
            DrawTriangle((Vector2){ x, bot },
                         (Vector2){ x + half, top },
                         (Vector2){ x - half, top }, c);

            /* A stem, so the wedge and the line through the grid read as one
             * object rather than two things that happen to line up. */
            DrawRectangle((int)x, (int)top, 1, (int)(bot - top), c);
            EndScissorMode();
        }
    }

    /* What the menu came back with, acted on before anything else looks at the
     * roll. A pitch move goes through the same clamp a drag does, so a note
     * cannot be sent somewhere the engine will not sing. */
    {
        int pick = bm_menu_chosen(ui, ID_MENU);

        if (pick != 0 && s->selected >= 0 && s->selected < s->song.roll.count) {
            bm_note *n = &s->song.roll.note[s->selected];
            int shift = 0;

            switch (pick) {
            case MENU_OCT_UP:    shift =  12; break;
            case MENU_OCT_DOWN:  shift = -12; break;
            case MENU_SEMI_UP:   shift =   1; break;
            case MENU_SEMI_DOWN: shift =  -1; break;
            case MENU_TIE:
                set_tie(s, s->selected, !n->tie);
                break;
            case MENU_DELETE:
                mark(s, 0u);
                bm_roll_remove(&s->song.roll, s->selected);
                if (s->selected >= s->song.roll.count) {
                    s->selected = s->song.roll.count - 1;
                }
                bm_roll_check_ties(&s->song.roll);
                s->dirty = 1;
                break;
            default: break;
            }

            if (shift != 0 && n->midi + shift >= 12 && n->midi + shift <= 108) {
                mark(s, 0u);
                n->midi += shift;
                s->audition = s->selected + 1;
                s->dirty = 1;
                /* Follow it, so a note moved out of view does not simply
                 * vanish. */
                if (n->midi > s->top_midi) s->top_midi = n->midi;
                if (n->midi <= s->top_midi - (int)(lanes.height / LANE_H)) {
                    s->top_midi = n->midi + (int)(lanes.height / LANE_H) - 1;
                }
            }
        }
    }

    /* The menu is over everything, so nothing underneath may act on a click
     * meant for it - the same rule main.c applies to its modals. */
    if (bm_menu_is_open(ui, ID_MENU)) {
        ui->blocking = 1;
        ui->block = (Rectangle){ 0, 0, (float)GetScreenWidth(),
                                 (float)GetScreenHeight() };
    }

    handle_mouse(ui, s, ruler, lanes, use_dict);

    /* ---- the note being edited ---- */
    y = area.y + area.height - FOOT_H + 4.0f;
    {
        Rectangle r = { area.x, y, 96.0f, LINE_H };

        bm_text_spaced(ui, BM_FONT_SMALL, "WORD", area.x, y + 6.0f, BM_DIM);
        r.x = area.x + 46.0f;
        r.width = 130.0f;

        if (s->selected >= 0 && s->selected < s->song.roll.count) {
            bm_note *n = &s->song.roll.note[s->selected];

            if (n->tie) {
                /* No boxes at all rather than boxes that do nothing: a tied
                 * note has no word of its own, and a field that accepted one
                 * and then ignored it would be worse than no field. */
                char what[64];
                const char *v = bm_roll_tied_vowel(&s->song.roll, s->selected);
                Rectangle box = { r.x, r.y, 276.0f, LINE_H };

                bm_panel(box);
                snprintf(what, sizeof what, "tied  -  sings %s",
                         (v != 0) ? v : "?");
                bm_text(ui, BM_FONT_SMALL, what, r.x + 8.0f, y + 6.0f, BM_DIM);
                r.x += 284.0f;
            } else {
                {
                    /* A text box changes the buffer and then says so, so by
                     * the time this can mark anything the first letter is
                     * already typed. The old text goes back for the instant
                     * the snapshot is taken - which only matters for the first
                     * change of a run, and costs nothing on the rest. */
                    char was[BM_NOTE_LYRIC_MAX];
                    memcpy(was, n->lyric, sizeof was);
                    if (bm_textbox(ui, ID_LYRIC, r, n->lyric,
                                   (int)sizeof n->lyric, &s->lyric_st)) {
                        char now[BM_NOTE_LYRIC_MAX];
                        memcpy(now, n->lyric, sizeof now);
                        memcpy(n->lyric, was, sizeof was);
                        mark(s, TOK_WORD(s->selected));
                        memcpy(n->lyric, now, sizeof now);

                        respell(n, use_dict);
                        s->dirty = 1;
                    }
                }
                r.x += r.width + 8.0f;
                bm_text_spaced(ui, BM_FONT_SMALL, "PHONEMES", r.x, y + 6.0f,
                               BM_DIM);
                r.x += 78.0f;
                r.width = 190.0f;
                /* Typed over the spelling when the dictionary is wrong about a
                 * word, or when what is wanted is not a word at all. */
                {
                    char was[BM_NOTE_PHON_MAX];
                    memcpy(was, n->phon, sizeof was);
                    if (bm_textbox(ui, ID_PHON, r, n->phon,
                                   (int)sizeof n->phon, &s->phon_st)) {
                        char now[BM_NOTE_PHON_MAX];
                        memcpy(now, n->phon, sizeof now);
                        memcpy(n->phon, was, sizeof was);
                        mark(s, TOK_PHON(s->selected));
                        memcpy(n->phon, now, sizeof now);
                        s->dirty = 1;
                    }
                }
                r.x += r.width + 8.0f;
            }
            {
                char what[64];
                char name[8];
                bm_roll_note_name(n->midi, name, sizeof name);
                if (s->measured[s->selected] > n->length + 15) {
                    snprintf(what, sizeof what, "%s  %d ms  (sounds %d)",
                             name, n->length, s->measured[s->selected]);
                } else {
                    snprintf(what, sizeof what, "%s  %d ms", name, n->length);
                }
                bm_text(ui, BM_FONT_SMALL, what, r.x, y + 6.0f,
                        s->measured[s->selected] > n->length + 15 ? BM_AMBER
                                                                  : BM_TEXT);
            }

            /* TIE. Only offered where it could mean something - on a note that
             * has one before it to be tied to.
             *
             * (UNDO and REDO are drawn after this block, so that they are there
             * whether or not a note is selected.) Whether that note actually has a
             * vowel to carry is left to bm_roll_check_ties, which is the one
             * place that decides it. */
            {
                int on = (int)n->tie;
                int can = tie_possible(s, s->selected);

                if (bm_toggle(ui, (Rectangle){ area.x + area.width - 90.0f,
                                               y, 84.0f, LINE_H },
                              "TIE", &on, can)) {
                    set_tie(s, s->selected, on);
                }
            }
        } else {
            /* Both boxes, drawn and labelled, with nothing in them. They used
             * to be one blank panel and the words "no note selected", which
             * answers a question nobody asked and hides the two fields the tab
             * is mostly about - so the first thing the tab showed was the one
             * arrangement in which you cannot see what typing would do.
             *
             * Empty and not editable rather than absent: there is genuinely no
             * note to type into, and a live box that discarded what it was
             * given would be worse than one that is plainly waiting. */
            Rectangle box = { r.x, r.y, 130.0f, LINE_H };
            bm_panel(box);
            box.x += box.width + 8.0f + 78.0f;
            box.width = 190.0f;
            bm_text_spaced(ui, BM_FONT_SMALL, "PHONEMES",
                           r.x + 130.0f + 8.0f, y + 6.0f, BM_DIM);
            bm_panel(box);
            bm_text(ui, BM_FONT_SMALL, "click a lane to draw a note",
                    box.x + box.width + 12.0f, y + 6.0f, BM_DIM);
        }
        /* Undo and redo, whatever is selected. Greyed when there is nothing
         * to go back to, which is the only thing on screen that says the
         * history exists at all - a keystroke nobody is told about is a
         * feature only its author has. */
        {
            Rectangle u = { area.x + area.width - 90.0f - 2.0f * 62.0f, y,
                            58.0f, LINE_H };

            if (bm_button(ui, u, "UNDO", bm_roll_can_undo(&s->hist))) {
                step(s, 0);
            }
            u.x += 62.0f;
            if (bm_button(ui, u, "REDO", bm_roll_can_redo(&s->hist))) {
                step(s, 1);
            }
        }
    }
    y += LINE_H + 6.0f;

    /* ---- tempo, snap, and the buttons ---- */
    {
        Rectangle r = { area.x, y, 244.0f, 22.0f };
        float v = (s->song.tempo > 0.0f) ? s->song.tempo : 120.0f;

        if (bm_slider(ui, ID_TEMPO, r, "tempo", &v, 40.0f, 240.0f, "%.0f BPM") &&
            v > 0.0f) {
            s->song.tempo = v;
        }
        /* Retimed once the gesture is over rather than every frame, for the
         * reason bm_song_ui.c gives: rounding is only harmless once. */
        if (s->song.tempo > 0.0f && s->tempo_applied > 0.0f &&
            s->song.tempo != s->tempo_applied &&
            !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ui->focus != ID_TEMPO) {
            /* The slider moved the tempo some frames ago; the notes are still
             * written against the old one. Recording the pair as they actually
             * are means undo puts both back together. */
            {
                float moved_to = s->song.tempo;
                s->song.tempo = s->tempo_applied;
                mark(s, TOK_TEMPO);
                s->song.tempo = moved_to;
            }
            bm_roll_retime(&s->song.roll, s->tempo_applied, s->song.tempo);
            s->tempo_applied = s->song.tempo;
            s->dirty = 1;
        }

        /* Clear of the slider's readout and its stepper arrows, which is 90 px
         * of the row that the track's own width does not account for. */
        r.x += 282.0f;
        r.width = 46.0f;
        r.height = 26.0f;
        r.y = y - 2.0f;
        bm_text_spaced(ui, BM_FONT_SMALL, "SNAP", r.x, y + 4.0f, BM_DIM);
        r.x += 42.0f;
        {
            int i;
            for (i = 0; i < SNAP_COUNT; i++) {
                int on = (s->snap == i);
                if (bm_toggle(ui, r, SNAP_NAMES[i], &on, 1) && on) s->snap = i;
                r.x += r.width + 4.0f;
            }
        }

        /* Zoom, as buttons as well as control-wheel. A gesture nobody is told
         * about is a feature only its author has. */
        r.x += 24.0f;
        r.width = 30.0f;
        bm_text_spaced(ui, BM_FONT_SMALL, "ZOOM", r.x, y + 4.0f, BM_DIM);
        r.x += 44.0f;
        if (bm_button(ui, r, "-", 1)) zoom_by(s, lanes, 1.0f / 1.4f);
        r.x += 34.0f;
        if (bm_button(ui, r, "+", 1)) zoom_by(s, lanes, 1.4f);

        r.x = area.x + area.width - 3.0f * 88.0f - 16.0f;
        r.width = 80.0f;
        if (bm_button(ui, r, "HELP", 1)) s->help_open = 1;
        r.x += 88.0f;
        if (bm_button(ui, r, "LOAD", 1)) action = BM_ROLL_ACT_LOAD;
        r.x += 88.0f;
        if (bm_button(ui, r, "SAVE", 1)) action = BM_ROLL_ACT_SAVE;
    }

    if (s->focus_word) {
        ui->focus = ID_LYRIC;
        s->focus_word = 0;
    }

    if (s->dirty) bm_roll_ui_refresh(s, voice);

    return action;
}

/* ------------------------------------------------------------------ */

static char HELP[] =
"Click an empty lane to draw a note, and drag right to set its length.\n"
"Whatever you type goes into the note that is selected.\n"
"\n"
"  WORD        A syllable, spelled for you by the same dictionary and\n"
"              rules the engine uses. \"me\" becomes M IY1.\n"
"\n"
"  PHONEMES    What will actually be sung. Type over it when the\n"
"              dictionary is wrong about a word, or when what you want\n"
"              is not a word - the score format reference in the SONG\n"
"              tab lists every phoneme there is.\n"
"\n"
"  Drag a note to move it. Its two ends are handles: the right one\n"
"  changes how long it is and the left one where it starts, and the\n"
"  pointer turns into a double arrow when you are on one.\n"
"\n"
"  Dragging a note up and down plays it at each pitch it passes, so a\n"
"  melody can be found by ear rather than by counting lanes.\n"
"\n"
"  Right-click a note for a menu: an octave or a semitone either way,\n"
"  tie, and delete. Delete also works from the keyboard, and T ties.\n"
"\n"
"  Control-Z takes back the last thing you did, and control-shift-Z\n"
"  or control-Y puts it back. Thirty-two of each, and a gesture counts\n"
"  as one: a drag is one undo however far it went, and a typed word is\n"
"  one undo rather than one per letter. The UNDO and REDO buttons do\n"
"  the same and grey out when there is nothing left either way.\n"
"\n"
"  It covers the notes - drawn, moved, resized, tied, deleted, spelled\n"
"  - and the tempo. Loading a song starts a new history: undo does not\n"
"  walk back into the song before it.\n"
"\n"
"  The bar under the grid scrolls sideways and says how much of the\n"
"  song you are looking at: drag it anywhere along its length, or\n"
"  press beside the thumb to jump. ZOOM - and + change how much, and\n"
"  so does control-wheel, which zooms about the pointer. The wheel on\n"
"  its own scrolls pitch and shift-wheel scrolls time.\n"
"\n"
"  The grid takes whatever height the window has spare, so a taller\n"
"  window is more song rather than more background.\n"
"\n"
"================================================================\n"
"  TIE  -  ONE SYLLABLE OVER TWO NOTES\n"
"================================================================\n"
"\n"
"  TIE, or the T key, makes the selected note carry on the vowel that\n"
"  is already sounding instead of starting a syllable of its own. It\n"
"  has no word of its own; it sings what the note before it was\n"
"  singing, and it glides onto its pitch instead of stepping.\n"
"\n"
"  Both halves of that are what legato is. Nothing re-articulates,\n"
"  because there is no consonant in front of the held vowel - and the\n"
"  pitch bends into place over about 60 ms rather than jumping, which\n"
"  is the difference between a sung phrase and an arpeggio.\n"
"\n"
"  A word's closing consonants move to the end of the slur, which is\n"
"  what a singer does: \"straight\" over two notes is S T R EY and then\n"
"  EY T, not the whole word followed by a vowel. Tie as many notes in\n"
"  a row as you like - a run of them is one held vowel however long.\n"
"\n"
"  Tied notes are drawn joined: no line cutting them apart in the same\n"
"  lane, a stroke connecting them across lanes. A tie only means\n"
"  anything while the two notes touch, so a tied note pulls back onto\n"
"  the end of the one before it, and dragging it away unties it. A tie\n"
"  across a rest would be a lie - the tone stops in the rest.\n"
"\n"
"  SNAP is in beats, so it follows the tempo. Changing the tempo moves\n"
"  every note with it, the same way it rewrites a score's [hold]\n"
"  values in the SONG tab - the engine has no idea what a tempo is.\n"
"\n"
"  Drag in the numbered strip above the grid to move the playhead.\n"
"  SING starts from wherever it is and the head comes back there when\n"
"  the song runs out, so pressing SING again plays the same passage.\n"
"  LOOP repeats, and dragging the head while it plays scrubs. The other two tabs cannot do any\n"
"  of that: they speak an utterance from its beginning, because that\n"
"  is what the engine offers. A roll is rendered first and then played\n"
"  from, which is what makes a position in it something you can pick.\n"
"\n"
"================================================================\n"
"  WHEN A NOTE RUNS OVER\n"
"================================================================\n"
"\n"
"  A note is drawn the length it asks for, and a syllable does not\n"
"  always fit. \"straight\" has 600 ms of consonants in it: the S, the\n"
"  T, the R and the T at the end all take time that no amount of\n"
"  shortening the vowel can give back.\n"
"\n"
"  Up to a point the consonants are compressed to fit, which is what a\n"
"  singer does with a word too long for its beat. Past that the note\n"
"  runs over, and an amber bar past its right edge shows by how much.\n"
"  Nothing is hidden: what you see is measured from the same code that\n"
"  makes the sound, not estimated beside it.\n"
"\n"
"  The fix is to lengthen the note, move the note after it, or use a\n"
"  shorter syllable. A roll with no amber in it plays exactly as drawn.\n"
"\n"
"================================================================\n"
"  ONE VOICE\n"
"================================================================\n"
"\n"
"  BENCmouth sings one note at a time, so notes cannot overlap: drag\n"
"  one onto another and the other gives way. A chord is not something\n"
"  this can be asked for.\n"
"\n"
"  An outlined note is one that has been drawn but not yet spelled. It\n"
"  is skipped, and the count above the grid says how many there are.\n"
"\n"
"  The roll is saved inside the .bmsong as note = lines, next to the\n"
"  score it compiles to - so a song you draw still opens in the SONG\n"
"  tab, and `bm -S` still sings it. It does not work the other way\n"
"  round: a score can say things a grid cannot draw.\n";

void bm_roll_help(bm_ui *ui, bm_roll_ui *s, float w, float h)
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
    ui->blocking = 0;

    bm_text_spaced(ui, BM_FONT_BODY, "THE ROLL", p.x + 20.0f, p.y + 18.0f,
                   BM_TEXT);
    bm_divider(p.x + 20.0f, p.y + 46.0f, pw - 40.0f);

    cy = p.y + ph - 46.0f;
    bm_textview(ui, ID_HELP,
                (Rectangle){ p.x + 20.0f, p.y + 56.0f, pw - 40.0f,
                             cy - (p.y + 56.0f) - 10.0f },
                HELP, &s->help_st, BM_DIM);

    if (bm_button(ui, (Rectangle){ p.x + pw - 116.0f, cy, 96.0f, 30.0f },
                  "CLOSE", 1) || IsKeyPressed(KEY_ESCAPE)) {
        s->help_open = 0;
    }
}
