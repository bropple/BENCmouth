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

/* One semitone. Eleven pixels rather than a rounder twelve because the band the
 * panel gets is 148 px tall, and 12 would show twelve lanes - one short of the
 * octave from C4 to C5, so the roll opened unable to display the scale it opens
 * with. Thirteen lanes is the smallest interval that is musically a unit. */
#define LANE_H    11.0f
#define RULER_H   14.0f
#define KEYS_W    34.0f     /* the keyboard down the left */
#define FOOT_H    64.0f     /* the two rows of controls underneath */
#define GRAB_PX    5.0f     /* how near the right edge is a length drag */

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

static void draw_notes(bm_ui *ui, const bm_roll_ui *s, Rectangle lanes)
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
            if (s->px_per_sec < 20.0f)  s->px_per_sec = 20.0f;
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
            float end = time_x(s, lanes, (float)(n->start + n->length));

            s->selected = hit;
            s->drag_index = hit;
            s->drag = (m.x >= end - GRAB_PX) ? BM_ROLL_DRAG_LENGTH
                                             : BM_ROLL_DRAG_MOVE;
            s->drag_grab = x_time(s, lanes, m.x) - n->start;
            s->drag_from = m;
            s->drag_moved = 0;
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
            } else {
                int start = snap_to(s, x_time(s, lanes, m.x) - s->drag_grab);
                int midi = y_midi(s, lanes, m.y);

                if (start < 0) start = 0;
                if (midi >= 12 && midi <= 108) n->midi = midi;
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
        bm_roll_remove(&s->song.roll, s->selected);
        if (s->selected >= s->song.roll.count) s->selected = s->song.roll.count - 1;
        bm_roll_check_ties(&s->song.roll);
        s->dirty = 1;
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

    if (ui == 0 || s == 0) return BM_ROLL_ACT_NONE;

    if (s->selected >= s->song.roll.count) s->selected = s->song.roll.count - 1;

    keys  = (Rectangle){ area.x, area.y + 18.0f + RULER_H, KEYS_W,
                         area.height - 18.0f - RULER_H - FOOT_H };
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
    draw_notes(ui, s, lanes);

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

    /* The head, drawn into the ruler too, so the strip you drag says it is a
     * control rather than a row of numbers. */
    {
        float x = time_x(s, lanes, s->head_ms);
        if (x >= ruler.x && x <= ruler.x + ruler.width) {
            DrawRectangle((int)x - 3, (int)ruler.y + (int)RULER_H - 3, 7, 3,
                          BM_TEXT);
        }
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
                if (bm_textbox(ui, ID_LYRIC, r, n->lyric, (int)sizeof n->lyric,
                               &s->lyric_st)) {
                    respell(n, use_dict);
                    s->dirty = 1;
                }
                r.x += r.width + 8.0f;
                bm_text_spaced(ui, BM_FONT_SMALL, "PHONEMES", r.x, y + 6.0f,
                               BM_DIM);
                r.x += 78.0f;
                r.width = 190.0f;
                /* Typed over the spelling when the dictionary is wrong about a
                 * word, or when what is wanted is not a word at all. */
                if (bm_textbox(ui, ID_PHON, r, n->phon, (int)sizeof n->phon,
                               &s->phon_st)) {
                    s->dirty = 1;
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
             * has one before it to be tied to. Whether that note actually has a
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
            Rectangle box = { r.x, r.y, 130.0f, LINE_H };
            bm_panel(box);
            bm_text(ui, BM_FONT_SMALL, "no note selected", r.x + 190.0f,
                    y + 6.0f, BM_DIM);
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
"  Drag a note to move it, drag its right edge to lengthen it, and\n"
"  press Delete to remove the selected one. The wheel scrolls pitch,\n"
"  shift-wheel scrolls time, and control-wheel zooms.\n"
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
"  SING starts from wherever it is, LOOP repeats, and dragging it\n"
"  while the song is playing scrubs. The other two tabs cannot do any\n"
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
