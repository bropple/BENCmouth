/*
 * BENCmouth GUI - theme and widget set
 * See bm_gui.h for why these are hand-drawn.
 */

#include "bm_gui.h"
#include "bm_embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Color BM_BG     = { 0x0c, 0x14, 0x08, 255 };
Color BM_PANEL  = { 0x18, 0x20, 0x10, 255 };
Color BM_BORDER = { 0x2a, 0x3a, 0x1e, 255 };
Color BM_TEXT   = { 0xcd, 0xea, 0xb0, 255 };
Color BM_DIM    = { 0x8a, 0xa8, 0x78, 255 };
Color BM_ACCENT = { 0x78, 0xb9, 0x46, 255 };
Color BM_EDGE   = { 0x3f, 0x5c, 0x28, 255 };
Color BM_ALERT  = { 0xd8, 0x4a, 0x3a, 255 };
Color BM_AMBER  = { 0xe8, 0xb2, 0x3d, 255 };

/* Terminus, if it can be found. Looked for rather than embedded: the OFL
 * requires the licence to ship with the font, and a build that silently
 * bundles one is a licence problem waiting to happen. Falling back to raylib's
 * built-in font keeps the application usable either way.
 *
 * These are relative, and where they are relative *to* is the whole problem.
 * Resolving them against the working directory only works when the working
 * directory happens to be the one holding the binary, which is true when you
 * double-click it and false for a shortcut, a terminal open somewhere else, or
 * Run. The symptom is a program that silently loses its font depending on how
 * it was started. So each of these is tried beside the executable first and
 * against the working directory second - the same order the window icon
 * uses, and for the same reason. */
static const char *FONT_RELATIVE[] = {
    "assets/fonts/TerminessNerdFont-Regular.ttf",
    "assets/fonts/TerminusTTF.ttf",
    "TerminusTTF.ttf",            /* an archive unpacked flat */
    "../assets/fonts/TerminusTTF.ttf"
};

/* Where a system package would have put it. Absolute, so they are used as-is. */
static const char *FONT_SYSTEM[] = {
    "/usr/share/fonts/TTF/TerminessNerdFont-Regular.ttf",
    "/usr/share/fonts/TTF/TerminusTTF.ttf",
    "/usr/share/fonts/truetype/terminus/TerminusTTF.ttf",
    "/usr/local/share/fonts/TerminusTTF.ttf",
    "/Library/Fonts/TerminessNerdFont-Regular.ttf",
    "/Library/Fonts/TerminusTTF.ttf",
    "C:/Windows/Fonts/TerminessNerdFont-Regular.ttf",
    "C:/Windows/Fonts/TerminusTTF.ttf"
};

/* Returns a path that exists, or 0. `probe` holds the answer when the hit came
 * from beside the executable, so it has to outlive this call. */
static const char *find_font(char *probe, size_t cap)
{
    const char *dir = GetApplicationDirectory();
    size_t i;

    for (i = 0; i < sizeof FONT_RELATIVE / sizeof FONT_RELATIVE[0]; i++) {
        snprintf(probe, cap, "%s%s", dir, FONT_RELATIVE[i]);
        if (FileExists(probe)) return probe;
    }
    for (i = 0; i < sizeof FONT_RELATIVE / sizeof FONT_RELATIVE[0]; i++) {
        if (FileExists(FONT_RELATIVE[i])) return FONT_RELATIVE[i];
    }
    for (i = 0; i < sizeof FONT_SYSTEM / sizeof FONT_SYSTEM[0]; i++) {
        if (FileExists(FONT_SYSTEM[i])) return FONT_SYSTEM[i];
    }
    return 0;
}

/* Point filtering, not bilinear, in both loaders. Terminus is a bitmap design;
 * smoothing it is how you get the mush this font exists to avoid. */
static Font load_at(const char *path, int size, int *found)
{
    Font f = LoadFontEx(path, size, 0, 0);
    if (f.texture.id != 0 && f.glyphCount > 0) {
        SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
        *found = 1;
        return f;
    }
    return GetFontDefault();
}

static Font load_embedded(int size, int *found)
{
    Font f = LoadFontFromMemory(".ttf", BM_FONT_TTF, (int)BM_FONT_TTF_LEN,
                                size, 0, 0);
    if (f.texture.id != 0 && f.glyphCount > 0) {
        SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
        *found = 1;
        return f;
    }
    return GetFontDefault();
}

void bm_ui_init(bm_ui *ui)
{
    static char probe[1024];
    const char *path;

    memset(ui, 0, sizeof *ui);

    /* Disk first, so a different build of the face - Terminess Nerd Font, say -
     * can be dropped in beside the binary without recompiling. The embedded
     * copy is the floor, not the ceiling, and it is why there is no longer a
     * path where the window comes up in raylib's fallback font. */
    path = find_font(probe, sizeof probe);

    if (path != 0) {
        ui->small = load_at(path, BM_FONT_SMALL, &ui->loaded);
        ui->body  = load_at(path, BM_FONT_BODY,  &ui->loaded);
        ui->title = load_at(path, BM_FONT_TITLE, &ui->loaded);
    }
    if (!ui->loaded) {
        ui->small = load_embedded(BM_FONT_SMALL, &ui->loaded);
        ui->body  = load_embedded(BM_FONT_BODY,  &ui->loaded);
        ui->title = load_embedded(BM_FONT_TITLE, &ui->loaded);
        path = 0;
    }
    if (!ui->loaded) ui->small = ui->body = ui->title = GetFontDefault();

    /* Report the file that was actually used rather than a hardcoded name. The
     * status line said "Terminess" whichever font loaded, which is the kind of
     * small untruth that makes you distrust the rest of a display. */
    ui->font_name = !ui->loaded ? "built-in font"
                  : (path != 0 ? GetFileNameWithoutExt(path) : "Terminus (embedded)");
}

void bm_ui_defocus(bm_ui *ui)
{
    ui->focus    = 0;
    ui->num_id   = 0;
    ui->drag_id  = 0;
    ui->step_id  = 0;
    ui->col_drag = 0;
}

void bm_ui_free(bm_ui *ui)
{
    if (!ui->loaded) return;
    UnloadFont(ui->small);
    UnloadFont(ui->body);
    UnloadFont(ui->title);
}

/* ------------------------------------------------------------------ */

static Font font_for(const bm_ui *ui, int size)
{
    if (size >= BM_FONT_TITLE) return ui->title;
    if (size <= BM_FONT_SMALL) return ui->small;
    return ui->body;
}

float bm_text_measure(const bm_ui *ui, int size, const char *s, float spacing)
{
    return MeasureTextEx(font_for(ui, size), s, (float)size, spacing).x;
}

void bm_text(const bm_ui *ui, int size, const char *s, float x, float y, Color c)
{
    DrawTextEx(font_for(ui, size), s, (Vector2){ x, y }, (float)size, 0.0f, c);
}

void bm_text_spaced(const bm_ui *ui, int size, const char *s, float x, float y, Color c)
{
    DrawTextEx(font_for(ui, size), s, (Vector2){ x, y }, (float)size, 1.0f, c);
}

void bm_panel(Rectangle r)
{
    DrawRectangleRounded(r, BM_RADIUS / (r.height > 1 ? r.height : 1), 4, BM_PANEL);
    DrawRectangleRoundedLines(r, BM_RADIUS / (r.height > 1 ? r.height : 1), 4,
                              BM_BORDER);
}

/* The screen equivalent of the receipt format's dash line: a plain 1px rule,
 * no boxes and no double lines. */
void bm_divider(float x, float y, float w)
{
    DrawRectangle((int)x, (int)y, (int)w, 1, BM_BORDER);
}

void bm_label(const bm_ui *ui, const char *s, float x, float y)
{
    bm_text_spaced(ui, BM_FONT_SMALL, s, x, y, BM_DIM);
}

/* ------------------------------------------------------------------ *
 * Popup arbitration
 *
 * A widget asks whether the mouse is its to use. It is not, if a popup that
 * was declared earlier in the frame is covering it. See the comment on
 * bm_ui.block for why this is explicit rather than automatic.
 * ------------------------------------------------------------------ */

static int mouse_free(const bm_ui *ui)
{
    return !ui->blocking ||
           !CheckCollisionPointRec(GetMousePosition(), ui->block);
}

/* ------------------------------------------------------------------ */

int bm_button(const bm_ui *ui, Rectangle r, const char *label, int enabled)
{
    Vector2 m = GetMousePosition();
    int over = enabled && mouse_free(ui) && CheckCollisionPointRec(m, r);
    int down = over && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color fill = enabled ? (down ? BM_EDGE : (over ? BM_ACCENT : BM_PANEL)) : BM_PANEL;
    Color text = enabled ? (over && !down ? BM_BG : BM_TEXT) : BM_BORDER;
    float w;

    DrawRectangleRounded(r, BM_RADIUS / r.height, 4, fill);
    DrawRectangleRoundedLines(r, BM_RADIUS / r.height, 4,
                              enabled ? BM_ACCENT : BM_BORDER);

    w = bm_text_measure(ui, BM_FONT_SMALL, label, 1.0f);
    bm_text_spaced(ui, BM_FONT_SMALL, label,
                   r.x + (r.width - w) * 0.5f,
                   r.y + (r.height - BM_FONT_SMALL) * 0.5f, text);

    return enabled && over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

int bm_tabs(const bm_ui *ui, Rectangle r, const char **labels, int count,
            int *index)
{
    Vector2 m = GetMousePosition();
    int     changed = 0, i;
    float   x = r.x;
    float   base = r.y + r.height - 1.0f;

    if (count <= 0) return 0;

    /* The rule under the whole row first; each selected tab erases its own
     * stretch of it below. */
    DrawRectangle((int)r.x, (int)base, (int)r.width, 1, BM_BORDER);

    for (i = 0; i < count; i++) {
        float     w = bm_text_measure(ui, BM_FONT_SMALL, labels[i], 1.0f) + 34.0f;
        Rectangle t = { x, r.y, w, r.height };
        int       on = (i == *index);
        int       over = mouse_free(ui) && CheckCollisionPointRec(m, t);
        Color     text = on ? BM_TEXT : (over ? BM_ACCENT : BM_DIM);
        float     tw;

        if (on) {
            /* Three sides and a gap: the missing bottom edge is what joins the
             * tab to the panel below it. */
            DrawRectangleRec(t, BM_PANEL);
            DrawRectangle((int)t.x, (int)t.y, (int)t.width, 1, BM_ACCENT);
            DrawRectangle((int)t.x, (int)t.y, 1, (int)t.height, BM_ACCENT);
            DrawRectangle((int)(t.x + t.width) - 1, (int)t.y, 1, (int)t.height,
                          BM_ACCENT);
            DrawRectangle((int)t.x + 1, (int)base, (int)t.width - 2, 1, BM_PANEL);
        } else if (over) {
            DrawRectangle((int)t.x, (int)t.y + 2, (int)t.width,
                          (int)t.height - 3, BM_PANEL);
        }

        tw = bm_text_measure(ui, BM_FONT_SMALL, labels[i], 1.0f);
        bm_text_spaced(ui, BM_FONT_SMALL, labels[i],
                       t.x + (t.width - tw) * 0.5f,
                       t.y + (t.height - BM_FONT_SMALL) * 0.5f, text);

        if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !on) {
            *index = i;
            changed = 1;
        }
        x += w;
    }

    return changed;
}

/* A lowercase i in a ring. Drawn rather than typeset: the mark is two
 * rectangles and a circle, and asking the font where to put a glyph inside a
 * ring never quite centres it at 16 px. */
int bm_info_button(const bm_ui *ui, Rectangle r)
{
    Vector2 m   = GetMousePosition();
    int     over = mouse_free(ui) && CheckCollisionPointRec(m, r);
    float   cx  = r.x + r.width * 0.5f;
    float   cy  = r.y + r.height * 0.5f;
    float   rad = ((r.width < r.height) ? r.width : r.height) * 0.5f - 1.0f;
    Color   c   = over ? BM_ACCENT : BM_DIM;

    DrawCircleLines((int)cx, (int)cy, rad, c);
    DrawRectangle((int)cx - 1, (int)(cy - rad * 0.50f), 2, 2, c);
    DrawRectangle((int)cx - 1, (int)(cy - rad * 0.20f), 2, (int)(rad * 0.85f), c);

    return over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

int bm_toggle(const bm_ui *ui, Rectangle r, const char *label, int *on,
              int enabled)
{
    Vector2 m = GetMousePosition();
    int over = enabled && mouse_free(ui) && CheckCollisionPointRec(m, r);
    int hit  = over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    Color fill, text;
    float w;

    if (!enabled)     { fill = BM_PANEL; text = BM_BORDER; }
    else if (*on)     { fill = BM_ACCENT; text = BM_BG; }
    else if (over)    { fill = BM_EDGE;  text = BM_TEXT; }
    else              { fill = BM_PANEL; text = BM_DIM; }

    DrawRectangleRounded(r, BM_RADIUS / r.height, 4, fill);
    DrawRectangleRoundedLines(r, BM_RADIUS / r.height, 4,
                              enabled ? BM_ACCENT : BM_BORDER);

    w = bm_text_measure(ui, BM_FONT_SMALL, label, 1.0f);
    bm_text_spaced(ui, BM_FONT_SMALL, label, r.x + (r.width - w) * 0.5f,
                   r.y + (r.height - BM_FONT_SMALL) * 0.5f, text);

    if (hit) *on = !*on;
    return hit;
}

/* How many decimals a printf format shows, which is the precision the readout
 * is quoting and therefore the smallest step worth offering. Anything else
 * would print a number the arrow did not appear to change.
 *
 * Scans for the first "%.N" - the formats here are one conversion each, with
 * the unit as trailing literal text. A format without a precision means 6, the
 * C default, which is not a useful step, so it falls back to 2. */
static int fmt_decimals(const char *fmt)
{
    const char *p;

    for (p = fmt; *p != '\0'; p++) {
        if (p[0] == '%' && p[1] == '.' && p[2] >= '0' && p[2] <= '9') {
            return p[2] - '0';
        }
    }
    return 2;
}

static float decimal_step(int decimals)
{
    float s = 1.0f;
    int   i;
    for (i = 0; i < decimals; i++) s *= 0.1f;
    return s;
}

/* Snap to the displayed grid before stepping, so repeated clicks give round
 * numbers rather than carrying the arbitrary fraction a drag left behind.
 * Without this, one click on a 0.01 stepper takes 0.5273 to 0.5373, and the
 * readout reads 0.54 both before and after. */
static float step_value(float v, float step, int dir, float lo, float hi)
{
    double snapped = (double)v / (double)step;
    double whole   = (snapped < 0.0) ? -(double)(long)(0.5 - snapped)
                                     :  (double)(long)(snapped + 0.5);
    float  out     = (float)((whole + dir) * (double)step);

    /* A step landing within a thousandth of an endpoint is meant to be the
     * endpoint: a slider that stops at 0.9999999 shows 1.00 and then refuses to
     * behave like 1. */
    if (out < lo + step * 0.001f) out = lo;
    if (out > hi - step * 0.001f) out = hi;
    return out;
}

/* Draws a small solid triangle pointing up or down, inside `r`. */
static void step_arrow(Rectangle r, int up, Color c)
{
    float cx = r.x + r.width * 0.5f;
    float w  = 3.5f;
    float y0 = r.y + 2.0f, y1 = r.y + r.height - 2.0f;

    if (up) {
        DrawTriangle((Vector2){ cx, y0 }, (Vector2){ cx - w, y1 },
                     (Vector2){ cx + w, y1 }, c);
    } else {
        DrawTriangle((Vector2){ cx - w, y0 }, (Vector2){ cx, y1 },
                     (Vector2){ cx + w, y0 }, c);
    }
}

/* Parses what has been typed and, if it is a number, writes it clamped. Returns
 * nonzero if the value actually moved - a commit that lands on the value
 * already there is not a change, and reporting it as one would make the engine
 * reload the voice for nothing. */
static int commit_number(bm_ui *ui, float *value, float lo, float hi)
{
    float v;

    if (ui->num_bad) return 0;
    v = (float)atof(ui->num_buf);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (v == *value) return 0;
    *value = v;
    return 1;
}

static void seed_number(bm_ui *ui, float value, int decimals)
{
    /* Without the unit: "118 Hz" is not something strtod will take back, and
     * asking somebody to delete the unit before editing the number is a small
     * insult. */
    snprintf(ui->num_buf, sizeof ui->num_buf, "%.*f", decimals, (double)value);
    ui->num_len = (int)strlen(ui->num_buf);
    ui->num_bad = 0;
}

int bm_slider(bm_ui *ui, int id, Rectangle r, const char *label,
              float *value, float lo, float hi, const char *fmt)
{
    Vector2 m = GetMousePosition();
    /* Label and readout take a share of the row rather than a fixed 110 and
     * 100 pixels, so the same widget works in a third-width column. The caps
     * are the old fixed values, which means a wide slider is laid out exactly
     * as it always was and only a narrow one gives ground. */
    float labelw = r.width * 0.37f;
    float valuew = r.width * 0.32f;
    Rectangle track, field, up, down;
    float t = (hi > lo) ? (*value - lo) / (hi - lo) : 0.0f;
    int   decimals = fmt_decimals(fmt);
    float step     = decimal_step(decimals);
    int   editing, over_field, free_ = mouse_free(ui);
    int   changed = 0;
    char  buf[48];

    if (labelw > 110.0f) labelw = 110.0f;
    if (valuew > 100.0f) valuew = 100.0f;

    /* The readout is a control now, so it gets a rectangle. The steppers take a
     * fixed 13 px column off its right rather than a share: they are two
     * triangles and a hit target, and both want to be the same size in a wide
     * column and a narrow one. */
    field = (Rectangle){ r.x + r.width - valuew, r.y, valuew - 13.0f, r.height };
    up    = (Rectangle){ r.x + r.width - 13.0f, r.y, 13.0f, r.height * 0.5f };
    down  = (Rectangle){ up.x, r.y + r.height * 0.5f, 13.0f, r.height * 0.5f };
    /* Six pixels of air after the label. Without it a label that fills its
     * share sits directly against the track and reads as one object. */
    track = (Rectangle){ r.x + labelw, r.y + r.height * 0.5f - 3,
                         field.x - (r.x + labelw) - 8.0f, 6 };

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    editing    = (ui->num_id == id && ui->focus == id);
    over_field = free_ && CheckCollisionPointRec(m, field);

    /* Somebody else took the caret before this slider was reached this frame -
     * a text box, which is laid out above and so runs first. Commit rather than
     * discard: typing a number and then clicking the thing you want to hear it
     * on is the normal way to use this, and throwing the number away at that
     * moment would be the single most annoying behaviour available. */
    if (ui->num_id == id && !editing) {
        changed |= commit_number(ui, value, lo, hi);
        ui->num_id = 0;
    }

    /* ---- taking and giving up the caret ---- */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && free_) {
        if (over_field && !editing) {
            seed_number(ui, *value, decimals);
            ui->num_blink = 0.0f;
            ui->num_id    = id;
            ui->focus     = id;
            editing       = 1;
        } else if (editing && !over_field) {
            changed   |= commit_number(ui, value, lo, hi);
            ui->num_id = 0;
            ui->focus  = 0;
            editing    = 0;
        }
    }

    if (editing) {
        int key;

        while ((key = GetCharPressed()) != 0) {
            /* Digits, one sign, one point. Filtering here rather than at commit
             * means the field cannot be got into a state it will not come out
             * of, and the reject is silent because a beep for a letter typed
             * into a number field is noise about a thing you already know. */
            int ok = (key >= '0' && key <= '9') ||
                     (key == '.' && strchr(ui->num_buf, '.') == 0) ||
                     (key == '-' && ui->num_len == 0);
            if (ok && ui->num_len < (int)sizeof ui->num_buf - 1) {
                ui->num_buf[ui->num_len++] = (char)key;
                ui->num_buf[ui->num_len]   = '\0';
            }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) &&
            ui->num_len > 0) {
            ui->num_buf[--ui->num_len] = '\0';
        }

        /* "-" and "." and "" are all things a half-typed number passes
         * through. Shown in the alert colour and simply not committed, rather
         * than corrected under the caret while somebody is still typing. */
        {
            char *end;
            double v = strtod(ui->num_buf, &end);
            (void)v;
            ui->num_bad = (ui->num_len == 0 || *end != '\0');
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
            IsKeyPressed(KEY_TAB)) {
            changed   |= commit_number(ui, value, lo, hi);
            ui->num_id = 0;
            ui->focus  = 0;
            editing    = 0;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            ui->num_id = 0;
            ui->focus  = 0;
            editing    = 0;
        }

        /* The arrow keys step, which is the same control as the arrow buttons
         * and is how anyone who has just typed a number expects to nudge it. */
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP) ||
            IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
            int dir = (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) ? 1 : -1;
            *value  = step_value(*value, step, dir, lo, hi);
            changed = 1;
            seed_number(ui, *value, decimals);
        }
    }

    /* ---- the steppers ---- */
    {
        int over_up   = free_ && CheckCollisionPointRec(m, up);
        int over_down = free_ && CheckCollisionPointRec(m, down);
        int dir       = over_up ? 1 : (over_down ? -1 : 0);

        if (dir != 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            ui->step_id   = id;
            ui->step_dir  = dir;
            ui->step_held = 0.0f;
            /* Hold before repeating, then ten a second. A stepper that repeats
             * immediately cannot be used for a single increment. */
            ui->step_next = 0.45f;
            *value  = step_value(*value, step, dir, lo, hi);
            changed = 1;
        } else if (ui->step_id == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            ui->step_held += GetFrameTime();
            if (ui->step_held >= ui->step_next) {
                ui->step_next += 0.05f;
                *value  = step_value(*value, step, ui->step_dir, lo, hi);
                changed = 1;
            }
        } else if (ui->step_id == id) {
            ui->step_id = 0;
        }

        if (changed && editing) seed_number(ui, *value, decimals);

        step_arrow(up,   1, over_up   ? BM_ACCENT : BM_DIM);
        step_arrow(down, 0, over_down ? BM_ACCENT : BM_DIM);
    }

    /* ---- the track ---- *
     *
     * Recomputed after the steppers and the keyboard, so the fill drawn this
     * frame is the value this frame ended with rather than the one it started
     * with. A one-frame lag here is invisible while dragging and obvious when
     * clicking a stepper, which moves the number and not the bar. */
    t = (hi > lo) ? (*value - lo) / (hi - lo) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    bm_label(ui, label, r.x, r.y + (r.height - BM_FONT_SMALL) * 0.5f);

    DrawRectangleRec(track, BM_PANEL);
    DrawRectangleLinesEx(track, 1, BM_BORDER);
    DrawRectangle((int)track.x, (int)track.y, (int)(track.width * t), (int)track.height,
                  BM_ACCENT);

    /* Grab anywhere left of the readout, not just the 6px track - a 6px hit
     * target is the kind of thing that makes an interface feel hostile. The
     * readout and its arrows are excluded, because they are controls of their
     * own now and a drag that started on the number used to jump the value. */
    if (free_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        m.x < field.x && CheckCollisionPointRec(m, r)) {
        ui->drag_id = id;
    }
    if (ui->drag_id == id) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            ui->drag_id = 0;
        } else {
            float nt = (m.x - track.x) / track.width;
            if (nt < 0.0f) nt = 0.0f;
            if (nt > 1.0f) nt = 1.0f;
            if (*value != lo + (hi - lo) * nt) {
                *value  = lo + (hi - lo) * nt;
                changed = 1;
            }
            t = nt;
            DrawRectangle((int)track.x, (int)track.y, (int)(track.width * t),
                          (int)track.height, BM_ACCENT);
        }
    }

    /* ---- the readout ---- */
    if (editing) {
        float cx;
        DrawRectangleRec(field, BM_PANEL);
        DrawRectangleLinesEx(field, 1, BM_ACCENT);
        bm_text(ui, BM_FONT_SMALL, ui->num_buf, field.x + 4,
                r.y + (r.height - BM_FONT_SMALL) * 0.5f,
                ui->num_bad ? BM_ALERT : BM_TEXT);

        ui->num_blink += GetFrameTime();
        if (ui->num_blink > 1.0f) ui->num_blink -= 1.0f;
        cx = field.x + 4 + bm_text_measure(ui, BM_FONT_SMALL, ui->num_buf, 0.0f);
        if (ui->num_blink < 0.5f) {
            DrawRectangle((int)cx + 1, (int)r.y + 4, 1,
                          (int)r.height - 8, BM_TEXT);
        }
    } else {
        /* An outline on hover, so the number reads as something you can click
         * into. Without it the only cue is the colour change, and a readout
         * that merely brightens looks like a hover effect rather than like a
         * field - nobody tries to type into it. */
        if (over_field) DrawRectangleLinesEx(field, 1, BM_BORDER);
        snprintf(buf, sizeof buf, fmt, (double)*value);
        bm_text(ui, BM_FONT_SMALL, buf, field.x + 4,
                r.y + (r.height - BM_FONT_SMALL) * 0.5f,
                over_field ? BM_ACCENT : BM_TEXT);
    }

    return changed;
}

int bm_dropdown(bm_ui *ui, Rectangle r, const char **items, int count,
                int *index, int *open, const char *shown)
{
    Vector2 m = GetMousePosition();
    int changed = 0, i;
    int free_ = mouse_free(ui);
    int was_open = *open;

    bm_panel(r);
    /* What is actually selected, which is not always an entry in the list. A
     * voice loaded from a file, or one whose sliders have been moved since,
     * has a name the list does not contain - and a control that went on
     * displaying the preset it started from would be describing something that
     * is no longer there. */
    bm_text(ui, BM_FONT_SMALL, shown != 0 ? shown : items[*index], r.x + 8,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);
    bm_text(ui, BM_FONT_SMALL, *open ? "^" : "v", r.x + r.width - 18,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_DIM);

    if (free_ && CheckCollisionPointRec(m, r) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        *open = !*open;
        if (*open) ui->menu_open = 0;   /* one popup at a time */
    }

    if (*open) {
        float top = r.y + r.height + 1;
        /* However many rows fit between the control and the bottom edge, with
         * a margin so the last one is not flush against it. Computed rather
         * than fixed: the window is resizable, and a constant that is right at
         * 780 px tall is wrong at 1400. */
        int   rows = (int)((float)GetScreenHeight() - top - 12.0f) /
                     (int)r.height;
        Rectangle list;

        if (rows > count) rows = count;
        if (rows < 3) rows = 3;

        if (!was_open) {
            /* Opening: put the selection in view. Centred rather than at the
             * top, so there is context either side of it. */
            ui->pop_first = *index - rows / 2;
        }
        if (count > rows) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) ui->pop_first -= (int)wheel;
        } else {
            ui->pop_first = 0;
        }
        if (ui->pop_first > count - rows) ui->pop_first = count - rows;
        if (ui->pop_first < 0) ui->pop_first = 0;

        list = (Rectangle){ r.x, top, r.width, (float)rows * r.height };

        /* Input here, drawing in bm_ui_overlay. The list is on top, so it takes
         * the mouse first - and publishing its rectangle is what stops the
         * sliders underneath from being dragged through it. */
        for (i = 0; i < rows; i++) {
            Rectangle item = { list.x, list.y + (float)i * r.height,
                               list.width, r.height };
            if (CheckCollisionPointRec(m, item) &&
                IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                *index = ui->pop_first + i;
                *open = 0;
                changed = 1;
            }
        }
        /* A click anywhere else dismisses it, as every other menu does. */
        if (*open && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) &&
            !CheckCollisionPointRec(m, list) && !CheckCollisionPointRec(m, r)) {
            *open = 0;
        }

        if (*open) {
            ui->pop_kind  = 1;
            ui->pop_rect  = list;
            ui->pop_items = items;
            ui->pop_count = count;
            ui->pop_rows  = rows;
            ui->pop_sel   = *index;
            ui->block     = list;
            ui->blocking  = 1;
        }
    }
    return changed;
}

/* ------------------------------------------------------------------ *
 * Text
 *
 * Wrapping, a caret, a selection and a scrollbar. This is more code than the
 * append-and-backspace field it replaces, but that field could not do the
 * things a text box is for: put the caret back in the middle of a word, select
 * a phrase, paste one in. Those are not embellishments on typing - for anyone
 * correcting a sentence rather than composing one, they are the interaction.
 * ------------------------------------------------------------------ */

#define BM_MAXLINES 512

/* Advance width of one glyph, from the font's own metrics.
 *
 * The alternative - MeasureTextEx on a one-character string - is what the
 * single-line field did, and it is a full text-shaping call per character per
 * frame. Wrapping needs a width for every character in the buffer, so this is
 * on the hot path in a way it was not before. */
static float char_w(const bm_ui *ui, int size, int c)
{
    Font  f = font_for(ui, size);

    /* Carriage returns take no space and are never drawn. Text that arrives
     * from a file rather than from the keyboard can be CRLF - the embedded
     * OFL is - and a font with no glyph for 0x0D renders one box per line. */
    if (c == '\r') return 0.0f;

    int   i = GetGlyphIndex(f, c);
    float scale = (float)size / (float)f.baseSize;
    float adv;

    if (f.glyphs == 0 || f.recs == 0) return (float)size * 0.5f;
    adv = (f.glyphs[i].advanceX != 0)
              ? (float)f.glyphs[i].advanceX
              : f.recs[i].width + (float)f.glyphs[i].offsetX;
    return adv * scale;
}

/* Break `s` into visual lines no wider than maxw, at spaces where there is
 * one and mid-word where there is not. Returns the line count, always >= 1. */
static int wrap_text(const bm_ui *ui, int size, const char *s, float maxw,
                     int *start, int *end, int maxlines)
{
    int   n = 0, i = 0, ls = 0, lastsp = -1;
    float w = 0.0f;

    if (maxw < 1.0f) maxw = 1.0f;

    for (;;) {
        int c = (unsigned char)s[i];

        if (c == '\0') { start[n] = ls; end[n] = i; n++; break; }

        if (c == '\n') {
            start[n] = ls; end[n] = i; n++;
            if (n >= maxlines) break;
            i++; ls = i; lastsp = -1; w = 0.0f;
            continue;
        }

        {
            float cw = char_w(ui, size, c);

            /* i > ls guarantees at least one character per line, which is what
             * keeps this from looping forever on a box narrower than a glyph. */
            if (w + cw > maxw && i > ls) {
                int at_space = (lastsp > ls);
                int brk = at_space ? lastsp : i;

                start[n] = ls; end[n] = brk; n++;
                if (n >= maxlines) break;
                ls = at_space ? brk + 1 : brk;   /* the space itself is eaten */
                i = ls; lastsp = -1; w = 0.0f;
                continue;
            }
            if (c == ' ') lastsp = i;
            w += cw;
            i++;
        }
    }
    if (n == 0) { start[0] = 0; end[0] = 0; n = 1; }
    return n;
}

/* Copies one wrapped line out of the buffer, leaving carriage returns behind. */
static void copy_line(char *dst, size_t cap, const char *src, int from, int to)
{
    size_t n = 0;
    int    i;

    for (i = from; i < to && n + 1 < cap; i++) {
        if (src[i] != '\r') dst[n++] = src[i];
    }
    dst[n] = '\0';
}

static int line_of(int caret, const int *start, int n)
{
    int i;
    for (i = n - 1; i > 0; i--) {
        if (caret >= start[i]) return i;
    }
    return 0;
}

static float span_w(const bm_ui *ui, int size, const char *s, int from, int to)
{
    float w = 0.0f;
    int   i;
    for (i = from; i < to; i++) w += char_w(ui, size, (unsigned char)s[i]);
    return w;
}

/* Which character position on a line the point x lands on, rounded to the
 * nearer gap between glyphs - clicking the left half of a letter puts the
 * caret before it, the right half after. */
static int index_at(const bm_ui *ui, int size, const char *s, int ls, int le,
                    float x)
{
    float w = 0.0f;
    int   i;
    for (i = ls; i < le; i++) {
        float cw = char_w(ui, size, (unsigned char)s[i]);
        if (x < w + cw * 0.5f) return i;
        w += cw;
    }
    return le;
}

static void del_range(char *buf, int a, int b)
{
    int len = (int)strlen(buf);
    if (a < 0) a = 0;
    if (b > len) b = len;
    if (b <= a) return;
    memmove(buf + a, buf + b, (size_t)(len - b + 1));
}

/* Insert, keeping to the printable ASCII the synthesiser's front end accepts
 * plus newlines. Pasting from a browser otherwise brings smart quotes and
 * non-breaking spaces in with it, which reach the letter-to-sound rules as
 * unknown bytes. */
static int ins_text(char *buf, int cap, int at, const char *s)
{
    int len = (int)strlen(buf), room = cap - 1 - len, n = 0, i;
    char clean[1024];

    for (i = 0; s[i] != '\0' && n < (int)sizeof clean - 1 && n < room; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r') clean[n++] = '\n';
        else if (c == '\t') clean[n++] = ' ';
        else if (c >= 32 && c < 127) clean[n++] = (char)c;
    }
    clean[n] = '\0';
    if (n <= 0) return 0;

    memmove(buf + at + n, buf + at, (size_t)(len - at + 1));
    memcpy(buf + at, clean, (size_t)n);
    return n;
}

/* ------------------------------------------------------------------ */

/* Shared by the editable box and the read-only view. Returns the width the
 * text may use, which is narrower when a bar is showing. */
static float scrollbar(Rectangle r, float content, float view, bm_edit *st,
                       int hovered)
{
    Rectangle bar, thumb;
    float     maxs = content - view;
    float     t;

    if (maxs <= 0.0f) { st->scroll = 0.0f; return r.width - 20.0f; }

    if (hovered) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) st->scroll -= wheel * 3.0f * (BM_FONT_BODY + 4);
    }

    bar = (Rectangle){ r.x + r.width - 12, r.y + 4, 7, r.height - 8 };
    {
        float th = bar.height * (view / content);
        Vector2 m = GetMousePosition();

        if (th < 18.0f) th = 18.0f;
        if (st->scroll < 0.0f) st->scroll = 0.0f;
        if (st->scroll > maxs) st->scroll = maxs;
        t = st->scroll / maxs;

        thumb = (Rectangle){ bar.x, bar.y + (bar.height - th) * t, bar.width, th };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(m, bar)) {
            if (CheckCollisionPointRec(m, thumb)) {
                st->drag_grab = m.y - thumb.y;
            } else {
                st->drag_grab = th * 0.5f;      /* jump the thumb to the click */
            }
            st->drag_bar = 1;
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) st->drag_bar = 0;

        if (st->drag_bar && bar.height > th) {
            float nt = (m.y - st->drag_grab - bar.y) / (bar.height - th);
            if (nt < 0.0f) nt = 0.0f;
            if (nt > 1.0f) nt = 1.0f;
            st->scroll = nt * maxs;
            t = nt;
            thumb.y = bar.y + (bar.height - th) * t;
        }

        DrawRectangleRounded(bar, 0.5f, 4, BM_PANEL);
        DrawRectangleRounded(thumb, 0.5f, 4,
                             st->drag_bar ? BM_ACCENT : BM_EDGE);
    }
    return r.width - 30.0f;
}

/* Inset from the box edge to the text.
 *
 * Eight pixels top and bottom is right for a paragraph and wrong for a
 * one-line field: it is 16 px of chrome around a 24 px line, so a box built to
 * hold exactly one line has less room inside it than the line needs, which
 * clips the descenders and raises a scrollbar for text that fits. Below the
 * height of two lines the padding shrinks to what a single line actually
 * wants. */
static float bm_textpad(float height, float line_h)
{
    return (height < 2.0f * line_h + 16.0f) ? 4.0f : 8.0f;
}

static void clamp_scroll(bm_edit *st, float content, float view)
{
    float maxs = content - view;
    if (maxs < 0.0f) maxs = 0.0f;
    if (st->scroll > maxs) st->scroll = maxs;
    if (st->scroll < 0.0f) st->scroll = 0.0f;
}

/* The text box and the selectable read-only view are one function.
 *
 * They differ in about twenty lines - insert, delete, cut, paste - and agree on
 * everything else: wrapping, the caret, sweeping out a selection, the
 * scrollbar, Ctrl-A, Ctrl-C, the arrow keys, Home and End. Writing the
 * read-only one separately would have meant a second copy of all of that, and
 * the second copy is the one that quietly stops matching.
 *
 * `editable` is what the whole distinction reduces to. Where it is 0 the buffer
 * is never written, which is what makes it safe to hand this a string the
 * caller regards as output only. */
static int textbox(bm_ui *ui, int id, Rectangle r, char *buf, int cap,
                   bm_edit *st, int size, Color col, int editable)
{
    /* One text box is edited at a time, so these are scratch rather than
     * state - recomputed from the buffer every frame. */
    static int start[BM_MAXLINES], end[BM_MAXLINES];

    Vector2 m       = GetMousePosition();
    float   lh      = (float)size + 4.0f;
    float   pad     = bm_textpad(r.height, lh);
    Rectangle inner = { r.x + pad, r.y + pad, r.width - 2 * pad, r.height - 2 * pad };
    int     focused = (ui->focus == id);
    int     over    = mouse_free(ui) && CheckCollisionPointRec(m, r);
    int     changed = 0;
    int     nlines, i, moved = 0;
    float   textw, content;
    int     ctrl, shift;

    /* An action chosen from the context menu last frame. The menu is drawn
     * after this widget, so its result cannot arrive any sooner - and a frame
     * is 16 ms, which is not a delay anyone can perceive. */
    if (ui->menu_action != BM_MENU_NONE && ui->menu_owner == id) {
        int a = st->sel < st->caret ? st->sel : st->caret;
        int b = st->sel < st->caret ? st->caret : st->sel;

        switch (ui->menu_action) {
        case BM_MENU_COPY:
        case BM_MENU_CUT:
            if (b > a) {
                /* Terminating in place and putting the byte back, rather than
                 * copying the span out to a buffer that would have to be as
                 * large as the largest thing anybody might select. In a
                 * read-only view this is the only write that ever happens to
                 * the caller's string, and it is undone on the next line. */
                char save = buf[b];
                buf[b] = '\0';
                SetClipboardText(buf + a);
                buf[b] = save;
                if (editable && ui->menu_action == BM_MENU_CUT) {
                    del_range(buf, a, b);
                    st->caret = st->sel = a;
                    changed = 1;
                }
            }
            break;
        case BM_MENU_PASTE: {
            const char *clip = editable ? GetClipboardText() : 0;
            if (clip != 0) {
                int n;
                if (b > a) { del_range(buf, a, b); st->caret = a; }
                n = ins_text(buf, cap, st->caret, clip);
                st->caret += n;
                st->sel = st->caret;
                changed = 1;
            }
            break;
        }
        case BM_MENU_ALL:
            st->sel = 0;
            st->caret = (int)strlen(buf);
            break;
        default: break;
        }
        ui->menu_action = BM_MENU_NONE;
    }

    /* The menu belongs to this box, so this box publishes its rectangle. */
    if (ui->menu_open && ui->menu_owner == id) {
        ui->blocking = 1;
        ui->block    = ui->pop_rect;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_free(ui)) {
        ui->focus = over ? id : (ui->focus == id ? 0 : ui->focus);
        focused = (ui->focus == id);
    }

    bm_panel(r);
    if (focused) DrawRectangleRoundedLines(r, BM_RADIUS / r.height, 4, BM_ACCENT);

    /* Wrap to the text width, which depends on whether a bar is showing -
     * which depends on the wrap. One pass at the narrow width settles it
     * without the flicker of a bar that appears and disappears. */
    textw  = inner.width - 18.0f;
    nlines = wrap_text(ui, size, buf, textw, start, end, BM_MAXLINES);
    content = (float)nlines * lh;

    if (content <= inner.height) {
        textw  = inner.width;
        nlines = wrap_text(ui, size, buf, textw, start, end, BM_MAXLINES);
        content = (float)nlines * lh;
    }

    if (st->caret > (int)strlen(buf)) st->caret = (int)strlen(buf);
    if (st->sel   > (int)strlen(buf)) st->sel   = (int)strlen(buf);

    ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
            IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER);
    shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    /* ---- mouse: place the caret, sweep out a selection ---- */
    {
        int line = line_of(st->caret, start, nlines);

        if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int li = (int)((m.y - inner.y + st->scroll) / lh);
            if (li < 0) li = 0;
            if (li >= nlines) li = nlines - 1;
            st->caret = index_at(ui, size, buf, start[li], end[li], m.x - inner.x);
            if (!shift) st->sel = st->caret;
            st->drag_text = 1;
            st->blink = 0.0f;
        }
        if (st->drag_text && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            int li = (int)((m.y - inner.y + st->scroll) / lh);
            if (li < 0) li = 0;
            if (li >= nlines) li = nlines - 1;
            st->caret = index_at(ui, size, buf, start[li], end[li], m.x - inner.x);
        }
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) st->drag_text = 0;

        if (over && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            Rectangle menu;
            ui->focus      = id;
            focused        = 1;
            ui->menu_open     = 1;
            ui->menu_owner    = id;
            ui->menu_readonly = !editable;
            menu = (Rectangle){ m.x, m.y, 148,
                                (editable ? 4 : 2) * 24 + 8 };
            ui->pop_rect = menu;
            ui->blocking = 1;
            ui->block    = menu;
        }
        (void)line;
    }

    /* ---- keyboard ---- */
    if (focused) {
        int len = (int)strlen(buf);
        int a   = st->sel < st->caret ? st->sel : st->caret;
        int b   = st->sel < st->caret ? st->caret : st->sel;
        int key;

        if (ctrl && IsKeyPressed(KEY_A)) { st->sel = 0; st->caret = len; }

        if (ctrl && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X)) && b > a) {
            char save = buf[b];
            buf[b] = '\0';
            SetClipboardText(buf + a);
            buf[b] = save;
            if (editable && IsKeyPressed(KEY_X)) {
                del_range(buf, a, b);
                st->caret = st->sel = a;
                changed = 1;
                len = (int)strlen(buf);
            }
        }
        if (editable && ctrl && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip != 0) {
                int n;
                if (b > a) { del_range(buf, a, b); st->caret = a; }
                n = ins_text(buf, cap, st->caret, clip);
                st->caret += n;
                st->sel = st->caret;
                changed = 1;
                moved = 1;
            }
        }

        if (editable && !ctrl) {
            while ((key = GetCharPressed()) != 0) {
                if (key >= 32 && key < 127) {
                    a = st->sel < st->caret ? st->sel : st->caret;
                    b = st->sel < st->caret ? st->caret : st->sel;
                    if (b > a) { del_range(buf, a, b); st->caret = a; }
                    if ((int)strlen(buf) < cap - 1) {
                        char one[2];
                        one[0] = (char)key;
                        one[1] = '\0';
                        st->caret += ins_text(buf, cap, st->caret, one);
                        st->sel = st->caret;
                        changed = 1;
                        moved = 1;
                    }
                }
            }
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) {
                a = st->sel < st->caret ? st->sel : st->caret;
                b = st->sel < st->caret ? st->caret : st->sel;
                if (b > a) { del_range(buf, a, b); st->caret = a; }
                st->caret += ins_text(buf, cap, st->caret, "\n");
                st->sel = st->caret;
                changed = 1;
                moved = 1;
            }
        }

        if (editable &&
            (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))) {
            a = st->sel < st->caret ? st->sel : st->caret;
            b = st->sel < st->caret ? st->caret : st->sel;
            if (b > a)          { del_range(buf, a, b); st->caret = a; changed = 1; }
            else if (st->caret > 0) { del_range(buf, st->caret - 1, st->caret);
                                      st->caret--; changed = 1; }
            st->sel = st->caret;
            moved = 1;
        }
        if (editable &&
            (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE))) {
            a = st->sel < st->caret ? st->sel : st->caret;
            b = st->sel < st->caret ? st->caret : st->sel;
            len = (int)strlen(buf);
            if (b > a)              { del_range(buf, a, b); st->caret = a; changed = 1; }
            else if (st->caret < len) { del_range(buf, st->caret, st->caret + 1);
                                        changed = 1; }
            st->sel = st->caret;
            moved = 1;
        }

        /* Editing changes the wrap, so redo it before the arrows use it. */
        if (changed) {
            nlines = wrap_text(ui, size, buf, textw, start, end, BM_MAXLINES);
            content = (float)nlines * lh;
        }

        {
            int line = line_of(st->caret, start, nlines);
            int step = 0;

            if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
                if (!shift && st->sel != st->caret) {
                    st->caret = st->sel < st->caret ? st->sel : st->caret;
                } else if (st->caret > 0) {
                    st->caret--;
                    /* Ctrl-left: to the start of the word, as everywhere else. */
                    if (ctrl) {
                        while (st->caret > 0 && buf[st->caret - 1] != ' ' &&
                               buf[st->caret - 1] != '\n') st->caret--;
                    }
                }
                step = 1;
            }
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
                len = (int)strlen(buf);
                if (!shift && st->sel != st->caret) {
                    st->caret = st->sel > st->caret ? st->sel : st->caret;
                } else if (st->caret < len) {
                    st->caret++;
                    if (ctrl) {
                        while (st->caret < len && buf[st->caret] != ' ' &&
                               buf[st->caret] != '\n') st->caret++;
                    }
                }
                step = 1;
            }
            if ((IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) && line > 0) {
                float x = span_w(ui, size, buf, start[line], st->caret);
                st->caret = index_at(ui, size, buf, start[line - 1],
                                     end[line - 1], x);
                step = 1;
            }
            if ((IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) &&
                line < nlines - 1) {
                float x = span_w(ui, size, buf, start[line], st->caret);
                st->caret = index_at(ui, size, buf, start[line + 1],
                                     end[line + 1], x);
                step = 1;
            }
            if (IsKeyPressed(KEY_HOME)) { st->caret = ctrl ? 0 : start[line]; step = 1; }
            if (IsKeyPressed(KEY_END)) {
                st->caret = ctrl ? (int)strlen(buf) : end[line];
                step = 1;
            }

            if (step) {
                if (!shift) st->sel = st->caret;
                st->blink = 0.0f;
                moved = 1;
            }
        }

        st->blink += GetFrameTime();
        if (st->blink > 1.0f) st->blink -= 1.0f;
    }

    /* Keep the caret on screen after anything that moved it. */
    if (moved || changed) {
        int   line = line_of(st->caret, start, nlines);
        float top  = (float)line * lh;
        if (top < st->scroll) st->scroll = top;
        if (top + lh > st->scroll + inner.height) {
            st->scroll = top + lh - inner.height;
        }
    }
    clamp_scroll(st, content, inner.height);

    /* ---- draw ---- */
    if (content > inner.height) {
        scrollbar(r, content, inner.height, st, over);
        clamp_scroll(st, content, inner.height);
    }

    BeginScissorMode((int)inner.x, (int)inner.y, (int)inner.width,
                     (int)inner.height);
    {
        int a = st->sel < st->caret ? st->sel : st->caret;
        int b = st->sel < st->caret ? st->caret : st->sel;

        for (i = 0; i < nlines; i++) {
            float ly = inner.y + (float)i * lh - st->scroll;
            char  line[1024];
            int   n = end[i] - start[i];

            if (ly + lh < inner.y || ly > inner.y + inner.height) continue;
            (void)n;
            copy_line(line, sizeof line, buf, start[i], end[i]);

            if (b > a && b > start[i] && a < end[i]) {
                int sa = a > start[i] ? a : start[i];
                int sb = b < end[i] ? b : end[i];
                float x0 = span_w(ui, size, buf, start[i], sa);
                float x1 = span_w(ui, size, buf, start[i], sb);
                DrawRectangle((int)(inner.x + x0), (int)ly,
                              (int)(x1 - x0) + 1, (int)lh, BM_EDGE);
            }
            bm_text(ui, size, line, inner.x, ly, col);
        }

        /* No caret in a read-only view. A blinking bar is a promise that
         * typing will land there, and here it will not - the selection
         * highlight is the whole of the feedback this needs. */
        if (editable && focused && st->blink < 0.5f) {
            int   line = line_of(st->caret, start, nlines);
            float cx = inner.x + span_w(ui, size, buf, start[line], st->caret);
            float cy = inner.y + (float)line * lh - st->scroll;
            DrawRectangle((int)cx, (int)cy, 2, (int)lh - 2, BM_ACCENT);
        }
    }
    EndScissorMode();

    return changed;
}

int bm_textbox(bm_ui *ui, int id, Rectangle r, char *buf, int cap, bm_edit *st)
{
    return textbox(ui, id, r, buf, cap, st, BM_FONT_BODY, BM_TEXT, 1);
}

int bm_textview(bm_ui *ui, int id, Rectangle r, char *s, bm_edit *st, Color c)
{
    /* `cap` is the length: nothing here can grow the buffer, so the only thing
     * a capacity would be used for is bounds on an insert that cannot happen.
     * Passing the length keeps the caret and the selection clamped correctly
     * without asking every caller to hand over a size it may not have. */
    return textbox(ui, id, r, s, (int)strlen(s) + 1, st, BM_FONT_SMALL, c, 0);
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ *
 * A right-click menu of caller-supplied items
 * ------------------------------------------------------------------ */

#define BM_MENU_ROW 24.0f

void bm_menu(bm_ui *ui, int id, float x, float y,
             const char **items, int count)
{
    float w = 0.0f;
    int   i;

    if (ui == 0 || items == 0 || count <= 0 || id == 0) return;

    for (i = 0; i < count; i++) {
        float tw = bm_text_measure(ui, BM_FONT_SMALL, items[i], 0.0f);
        if (tw > w) w = tw;
    }

    ui->list_owner  = id;
    ui->list_open   = 1;
    ui->list_items  = items;
    ui->list_count  = count;
    ui->list_choice = 0;
    ui->list_rect   = (Rectangle){ x, y, w + 28.0f,
                                   (float)count * BM_MENU_ROW + 8.0f };

    /* The other menu and any dropdown go away: two popups would fight over
     * which of them owns the mouse. */
    ui->menu_open = 0;
}

int bm_menu_chosen(bm_ui *ui, int id)
{
    int c;

    if (ui == 0 || ui->list_owner != id) return 0;
    c = ui->list_choice;
    ui->list_choice = 0;
    return c;
}

int bm_menu_is_open(const bm_ui *ui, int id)
{
    return ui != 0 && ui->list_open && ui->list_owner == id;
}

static void draw_list_menu(bm_ui *ui, Vector2 m)
{
    Rectangle menu = ui->list_rect;
    int i;

    DrawRectangle((int)menu.x + 3, (int)menu.y + 3, (int)menu.width,
                  (int)menu.height, (Color){ 0, 0, 0, 110 });
    DrawRectangleRec(menu, BM_PANEL);
    DrawRectangleLinesEx(menu, 1, BM_BORDER);

    for (i = 0; i < ui->list_count; i++) {
        Rectangle item = { menu.x + 1, menu.y + 4 + (float)i * BM_MENU_ROW,
                           menu.width - 2, BM_MENU_ROW };
        int over = CheckCollisionPointRec(m, item);

        /* A separator rather than an item: a dash on its own is how a menu
         * says "these belong together" without a second widget. */
        if (ui->list_items[i][0] == '-' && ui->list_items[i][1] == '\0') {
            bm_divider(item.x + 8, item.y + BM_MENU_ROW * 0.5f,
                       item.width - 16);
            continue;
        }

        if (over) DrawRectangleRec(item, BM_EDGE);
        bm_text(ui, BM_FONT_SMALL, ui->list_items[i], item.x + 10,
                item.y + (item.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);

        /* The choice outlives the menu by a frame: the overlay runs last, so
         * the owner does not get to look until the next one. Closing here and
         * clearing the choice there is what keeps those two apart. */
        if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            ui->list_choice = i + 1;
            ui->list_open = 0;
        }
    }

    /* Anywhere else dismisses it, on the press rather than the release - a
     * release would also arrive at whatever is underneath. */
    if (!CheckCollisionPointRec(m, menu) &&
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
         IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        ui->list_open = 0;
    }
}

void bm_ui_overlay(bm_ui *ui)
{
    static const char *MENU[]  = { "CUT", "COPY", "PASTE", "SELECT ALL" };
    /* A read-only view offers the two that do not write. Greying CUT and PASTE
     * instead would be four rows of which two are always dead, which says
     * "this is broken" rather than "this cannot be edited". */
    static const int   READONLY[] = { BM_MENU_COPY, BM_MENU_ALL };
    Vector2 m = GetMousePosition();
    int i;

    if (ui->pop_kind == 1) {
        Rectangle list = ui->pop_rect;
        int   rows = (ui->pop_rows > 0) ? ui->pop_rows : ui->pop_count;
        float ih = list.height / (float)rows;
        int   scrolls = ui->pop_count > rows;

        /* Drawn as one card with a shadow, so it reads as being above the
         * layout rather than punched into it. */
        DrawRectangle((int)list.x + 3, (int)list.y + 3, (int)list.width,
                      (int)list.height, (Color){ 0, 0, 0, 110 });
        for (i = 0; i < rows; i++) {
            int       k = ui->pop_first + i;
            Rectangle item = { list.x, list.y + (float)i * ih, list.width, ih };
            int over = CheckCollisionPointRec(m, item);

            if (k < 0 || k >= ui->pop_count) continue;
            DrawRectangleRec(item, over ? BM_EDGE : BM_PANEL);
            DrawRectangleLinesEx(item, 1, BM_BORDER);
            bm_text(ui, BM_FONT_SMALL, ui->pop_items[k], item.x + 8,
                    item.y + (item.height - BM_FONT_SMALL) * 0.5f,
                    k == ui->pop_sel ? BM_ACCENT : BM_TEXT);
        }

        /* Where you are in a list you cannot see all of. Drawn inside the
         * right edge rather than beside it, so the card stays one rectangle. */
        if (scrolls) {
            float track_h = list.height - 8.0f;
            float th = track_h * (float)rows / (float)ui->pop_count;
            float t  = (ui->pop_count > rows)
                     ? (float)ui->pop_first / (float)(ui->pop_count - rows)
                     : 0.0f;

            if (th < 16.0f) th = 16.0f;
            DrawRectangleRounded(
                (Rectangle){ list.x + list.width - 9.0f, list.y + 4.0f,
                             5.0f, track_h }, 0.5f, 4, BM_BG);
            DrawRectangleRounded(
                (Rectangle){ list.x + list.width - 9.0f,
                             list.y + 4.0f + (track_h - th) * t,
                             5.0f, th }, 0.5f, 4, BM_ACCENT);
        }
    }

    if (ui->menu_open) {
        Rectangle menu = ui->pop_rect;

        DrawRectangle((int)menu.x + 3, (int)menu.y + 3, (int)menu.width,
                      (int)menu.height, (Color){ 0, 0, 0, 110 });
        DrawRectangleRec(menu, BM_PANEL);
        DrawRectangleLinesEx(menu, 1, BM_BORDER);

        for (i = 0; i < (ui->menu_readonly ? 2 : 4); i++) {
            int action = ui->menu_readonly ? READONLY[i] : BM_MENU_CUT + i;
            Rectangle item = { menu.x + 1, menu.y + 4 + (float)i * 24,
                               menu.width - 2, 24 };
            int over = CheckCollisionPointRec(m, item);
            if (over) DrawRectangleRec(item, BM_EDGE);
            bm_text(ui, BM_FONT_SMALL, MENU[action - BM_MENU_CUT], item.x + 10,
                    item.y + (item.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);
            if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                ui->menu_action = action;
                ui->menu_open = 0;
            }
        }
        if (ui->menu_open && !CheckCollisionPointRec(m, menu) &&
            (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) ||
             IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            ui->menu_open = 0;
        }
    }

    if (ui->list_open) draw_list_menu(ui, m);

    ui->pop_kind = 0;
    ui->blocking = 0;
}

void bm_scroll_rows(bm_ui *ui, int id, Rectangle area, int total, int visible,
                    int *first)
{
    Vector2 m = GetMousePosition();
    Rectangle bar, thumb;
    int   maxfirst = total - visible;
    float th, travel;

    if (maxfirst <= 0) { *first = 0; return; }

    if (*first < 0) *first = 0;
    if (*first > maxfirst) *first = maxfirst;

    /* The wheel works anywhere over the column, not only over the 7px bar.
     * Scrolling that requires aiming is scrolling nobody uses. */
    if (mouse_free(ui) && CheckCollisionPointRec(m, area)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            *first -= (int)wheel;
            if (*first < 0) *first = 0;
            if (*first > maxfirst) *first = maxfirst;
        }
    }

    bar = (Rectangle){ area.x + area.width - 8.0f, area.y, 7.0f, area.height };
    DrawRectangleRounded(bar, 0.5f, 4, BM_PANEL);

    th = bar.height * ((float)visible / (float)total);
    if (th < 18.0f) th = 18.0f;
    travel = bar.height - th;

    thumb = (Rectangle){ bar.x, bar.y + travel * ((float)*first / (float)maxfirst),
                         bar.width, th };

    if (mouse_free(ui) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(m, bar)) {
        if (CheckCollisionPointRec(m, thumb)) {
            ui->col_drag = id;
            ui->col_grab = m.y - thumb.y;
        } else {
            /* Clicking the track jumps the thumb to the pointer and then keeps
             * following it, which is what every scrollbar does and what makes a
             * long list reachable in one gesture. */
            ui->col_drag = id;
            ui->col_grab = th * 0.5f;
        }
    }
    if (ui->col_drag == id) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            ui->col_drag = 0;
        } else if (travel > 0.0f) {
            float t = (m.y - ui->col_grab - bar.y) / travel;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            *first = (int)(t * (float)maxfirst + 0.5f);
            thumb.y = bar.y + travel * ((float)*first / (float)maxfirst);
        }
    }

    DrawRectangleRounded(thumb, 0.5f, 4, BM_ACCENT);
}

void bm_waveform(Rectangle r, const float *samples, int count)
{
    int x;

    bm_panel(r);
    DrawRectangle((int)r.x, (int)(r.y + r.height * 0.5f), (int)r.width, 1, BM_BORDER);

    if (count <= 0) return;

    /* One vertical bar per column, spanning that column's min and max. Drawing
     * every sample as a point would alias into a mess at this width. */
    for (x = 0; x < (int)r.width; x++) {
        int lo = count * x / (int)r.width;
        int hi = count * (x + 1) / (int)r.width;
        float mn = 0.0f, mx = 0.0f;
        int i;

        if (hi <= lo) hi = lo + 1;
        if (hi > count) hi = count;
        for (i = lo; i < hi; i++) {
            if (samples[i] < mn) mn = samples[i];
            if (samples[i] > mx) mx = samples[i];
        }
        {
            float mid = r.y + r.height * 0.5f;
            float h = r.height * 0.5f - 2;
            DrawRectangle((int)r.x + x, (int)(mid - mx * h), 1,
                          (int)((mx - mn) * h) + 1, BM_ACCENT);
        }
    }
}

void bm_meter(Rectangle r, float peak, float rms, int limited)
{
    float p = peak, m = rms;

    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    if (m < 0.0f) m = 0.0f;
    if (m > 1.0f) m = 1.0f;

    bm_panel(r);

    /* Two readings on one bar, which is how a meter that has to be believed is
     * usually built: the fill is loudness and the marker is headroom. They are
     * far apart here more often than they would be on a mixing desk, because a
     * driven voice can sit two thirds of the way up in level while peaking
     * barely above its own average - and a meter showing only the peak would
     * call that voice quiet. The gap between the fill and the marker is the
     * crest factor, drawn. */
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)((r.width - 2) * m),
                  (int)r.height - 2, limited ? BM_ALERT : BM_ACCENT);
    DrawRectangle((int)(r.x + 1 + (r.width - 3) * p), (int)r.y + 1, 2,
                  (int)r.height - 2, limited ? BM_ALERT : BM_TEXT);

    /* Where the host limiter starts, so a hot voice is visible before it is
     * audible. */
    DrawRectangle((int)(r.x + r.width * 0.85f), (int)r.y, 1, (int)r.height, BM_AMBER);
}
