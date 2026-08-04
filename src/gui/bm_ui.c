/*
 * BENCmouth GUI - theme and widget set
 * See bm_gui.h for why these are hand-drawn.
 */

#include "bm_gui.h"

#include <stdio.h>
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
 * built-in font keeps the application usable either way. */
static const char *FONT_PATHS[] = {
    "assets/fonts/TerminessNerdFont-Regular.ttf",
    "assets/fonts/TerminusTTF.ttf",
    "/usr/share/fonts/TTF/TerminessNerdFont-Regular.ttf",
    "/usr/share/fonts/truetype/terminus/TerminusTTF.ttf",
    "/Library/Fonts/TerminessNerdFont-Regular.ttf",
    "C:/Windows/Fonts/TerminessNerdFont-Regular.ttf"
};

static Font load_at(const char *path, int size, int *found)
{
    Font f = LoadFontEx(path, size, 0, 0);
    if (f.texture.id != 0 && f.glyphCount > 0) {
        /* Point filtering, not bilinear. Terminus is a bitmap design; smoothing
         * it is how you get the mush this font exists to avoid. */
        SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
        *found = 1;
        return f;
    }
    return GetFontDefault();
}

void bm_ui_init(bm_ui *ui)
{
    size_t i;
    const char *path = 0;

    memset(ui, 0, sizeof *ui);

    for (i = 0; i < sizeof FONT_PATHS / sizeof FONT_PATHS[0]; i++) {
        if (FileExists(FONT_PATHS[i])) { path = FONT_PATHS[i]; break; }
    }

    if (path != 0) {
        ui->small = load_at(path, BM_FONT_SMALL, &ui->loaded);
        ui->body  = load_at(path, BM_FONT_BODY,  &ui->loaded);
        ui->title = load_at(path, BM_FONT_TITLE, &ui->loaded);
    } else {
        ui->small = ui->body = ui->title = GetFontDefault();
    }

    /* Report the file that was actually found rather than a hardcoded name.
     * The status line said "Terminess" whichever font loaded, which is the
     * kind of small untruth that makes you distrust the rest of a display. */
    ui->font_name = ui->loaded ? GetFileNameWithoutExt(path) : "built-in font";
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

int bm_slider(const bm_ui *ui, Rectangle r, const char *label,
              float *value, float lo, float hi, const char *fmt)
{
    Vector2 m = GetMousePosition();
    Rectangle track = { r.x + 110, r.y + r.height * 0.5f - 3, r.width - 210, 6 };
    float t = (hi > lo) ? (*value - lo) / (hi - lo) : 0.0f;
    int changed = 0;
    char buf[48];

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    bm_label(ui, label, r.x, r.y + (r.height - BM_FONT_SMALL) * 0.5f);

    DrawRectangleRec(track, BM_PANEL);
    DrawRectangleLinesEx(track, 1, BM_BORDER);
    DrawRectangle((int)track.x, (int)track.y, (int)(track.width * t), (int)track.height,
                  BM_ACCENT);

    /* Grab anywhere on the row, not just the 6px track - a 6px hit target is
     * the kind of thing that makes an interface feel hostile. */
    if (mouse_free(ui) && CheckCollisionPointRec(m, r) &&
        IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float nt = (m.x - track.x) / track.width;
        if (nt < 0.0f) nt = 0.0f;
        if (nt > 1.0f) nt = 1.0f;
        *value = lo + (hi - lo) * nt;
        changed = 1;
    }

    snprintf(buf, sizeof buf, fmt, (double)*value);
    bm_text(ui, BM_FONT_SMALL, buf, track.x + track.width + 12,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);

    return changed;
}

int bm_dropdown(bm_ui *ui, Rectangle r, const char **items, int count,
                int *index, int *open)
{
    Vector2 m = GetMousePosition();
    int changed = 0, i;
    int free_ = mouse_free(ui);

    bm_panel(r);
    bm_text(ui, BM_FONT_SMALL, items[*index], r.x + 8,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);
    bm_text(ui, BM_FONT_SMALL, *open ? "^" : "v", r.x + r.width - 18,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_DIM);

    if (free_ && CheckCollisionPointRec(m, r) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        *open = !*open;
        if (*open) ui->menu_open = 0;   /* one popup at a time */
    }

    if (*open) {
        Rectangle list = { r.x, r.y + r.height + 1, r.width,
                           (float)count * r.height };

        /* Input here, drawing in bm_ui_overlay. The list is on top, so it takes
         * the mouse first - and publishing its rectangle is what stops the
         * sliders underneath from being dragged through it. */
        for (i = 0; i < count; i++) {
            Rectangle item = { list.x, list.y + (float)i * r.height,
                               list.width, r.height };
            if (CheckCollisionPointRec(m, item) &&
                IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                *index = i;
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

static void clamp_scroll(bm_edit *st, float content, float view)
{
    float maxs = content - view;
    if (maxs < 0.0f) maxs = 0.0f;
    if (st->scroll > maxs) st->scroll = maxs;
    if (st->scroll < 0.0f) st->scroll = 0.0f;
}

int bm_textbox(bm_ui *ui, int id, Rectangle r, char *buf, int cap, bm_edit *st)
{
    /* One text box is edited at a time, so these are scratch rather than
     * state - recomputed from the buffer every frame. */
    static int start[BM_MAXLINES], end[BM_MAXLINES];

    Vector2 m       = GetMousePosition();
    int     size    = BM_FONT_BODY;
    float   lh      = (float)size + 4.0f;
    float   pad     = 8.0f;
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
                char save = buf[b];
                buf[b] = '\0';
                SetClipboardText(buf + a);
                buf[b] = save;
                if (ui->menu_action == BM_MENU_CUT) {
                    del_range(buf, a, b);
                    st->caret = st->sel = a;
                    changed = 1;
                }
            }
            break;
        case BM_MENU_PASTE: {
            const char *clip = GetClipboardText();
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
            ui->menu_open  = 1;
            ui->menu_owner = id;
            menu = (Rectangle){ m.x, m.y, 148, 4 * 24 + 8 };
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
            if (IsKeyPressed(KEY_X)) {
                del_range(buf, a, b);
                st->caret = st->sel = a;
                changed = 1;
                len = (int)strlen(buf);
            }
        }
        if (ctrl && IsKeyPressed(KEY_V)) {
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

        if (!ctrl) {
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

        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            a = st->sel < st->caret ? st->sel : st->caret;
            b = st->sel < st->caret ? st->caret : st->sel;
            if (b > a)          { del_range(buf, a, b); st->caret = a; changed = 1; }
            else if (st->caret > 0) { del_range(buf, st->caret - 1, st->caret);
                                      st->caret--; changed = 1; }
            st->sel = st->caret;
            moved = 1;
        }
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
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
            if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
            memcpy(line, buf + start[i], (size_t)n);
            line[n] = '\0';

            if (b > a && b > start[i] && a < end[i]) {
                int sa = a > start[i] ? a : start[i];
                int sb = b < end[i] ? b : end[i];
                float x0 = span_w(ui, size, buf, start[i], sa);
                float x1 = span_w(ui, size, buf, start[i], sb);
                DrawRectangle((int)(inner.x + x0), (int)ly,
                              (int)(x1 - x0) + 1, (int)lh, BM_EDGE);
            }
            bm_text(ui, size, line, inner.x, ly, BM_TEXT);
        }

        if (focused && st->blink < 0.5f) {
            int   line = line_of(st->caret, start, nlines);
            float cx = inner.x + span_w(ui, size, buf, start[line], st->caret);
            float cy = inner.y + (float)line * lh - st->scroll;
            DrawRectangle((int)cx, (int)cy, 2, (int)lh - 2, BM_ACCENT);
        }
    }
    EndScissorMode();

    return changed;
}

void bm_textview(bm_ui *ui, Rectangle r, const char *s, bm_edit *st, Color c)
{
    static int start[BM_MAXLINES], end[BM_MAXLINES];

    Vector2 m       = GetMousePosition();
    int     size    = BM_FONT_SMALL;
    float   lh      = (float)size + 4.0f;
    float   pad     = 8.0f;
    Rectangle inner = { r.x + pad, r.y + pad, r.width - 2 * pad, r.height - 2 * pad };
    int     over    = mouse_free(ui) && CheckCollisionPointRec(m, r);
    int     nlines, i;
    float   textw, content;

    bm_panel(r);

    textw   = inner.width - 18.0f;
    nlines  = wrap_text(ui, size, s, textw, start, end, BM_MAXLINES);
    content = (float)nlines * lh;
    if (content <= inner.height) {
        textw   = inner.width;
        nlines  = wrap_text(ui, size, s, textw, start, end, BM_MAXLINES);
        content = (float)nlines * lh;
    }

    if (content > inner.height) {
        scrollbar(r, content, inner.height, st, over);
    } else {
        st->scroll = 0.0f;
    }
    clamp_scroll(st, content, inner.height);

    BeginScissorMode((int)inner.x, (int)inner.y, (int)inner.width,
                     (int)inner.height);
    for (i = 0; i < nlines; i++) {
        float ly = inner.y + (float)i * lh - st->scroll;
        char  line[1024];
        int   n = end[i] - start[i];

        if (ly + lh < inner.y || ly > inner.y + inner.height) continue;
        if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
        memcpy(line, s + start[i], (size_t)n);
        line[n] = '\0';
        bm_text(ui, size, line, inner.x, ly, c);
    }
    EndScissorMode();
}

/* ------------------------------------------------------------------ */

void bm_ui_overlay(bm_ui *ui)
{
    static const char *MENU[] = { "CUT", "COPY", "PASTE", "SELECT ALL" };
    Vector2 m = GetMousePosition();
    int i;

    if (ui->pop_kind == 1) {
        Rectangle list = ui->pop_rect;
        float ih = list.height / (float)ui->pop_count;

        /* Drawn as one card with a shadow, so it reads as being above the
         * layout rather than punched into it. */
        DrawRectangle((int)list.x + 3, (int)list.y + 3, (int)list.width,
                      (int)list.height, (Color){ 0, 0, 0, 110 });
        for (i = 0; i < ui->pop_count; i++) {
            Rectangle item = { list.x, list.y + (float)i * ih, list.width, ih };
            int over = CheckCollisionPointRec(m, item);
            DrawRectangleRec(item, over ? BM_EDGE : BM_PANEL);
            DrawRectangleLinesEx(item, 1, BM_BORDER);
            bm_text(ui, BM_FONT_SMALL, ui->pop_items[i], item.x + 8,
                    item.y + (item.height - BM_FONT_SMALL) * 0.5f,
                    i == ui->pop_sel ? BM_ACCENT : BM_TEXT);
        }
    }

    if (ui->menu_open) {
        Rectangle menu = ui->pop_rect;

        DrawRectangle((int)menu.x + 3, (int)menu.y + 3, (int)menu.width,
                      (int)menu.height, (Color){ 0, 0, 0, 110 });
        DrawRectangleRec(menu, BM_PANEL);
        DrawRectangleLinesEx(menu, 1, BM_BORDER);

        for (i = 0; i < 4; i++) {
            Rectangle item = { menu.x + 1, menu.y + 4 + (float)i * 24,
                               menu.width - 2, 24 };
            int over = CheckCollisionPointRec(m, item);
            if (over) DrawRectangleRec(item, BM_EDGE);
            bm_text(ui, BM_FONT_SMALL, MENU[i], item.x + 10,
                    item.y + (item.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);
            if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                ui->menu_action = BM_MENU_CUT + i;
                ui->menu_open = 0;
            }
        }
        if (ui->menu_open && !CheckCollisionPointRec(m, menu) &&
            (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) ||
             IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            ui->menu_open = 0;
        }
    }

    ui->pop_kind = 0;
    ui->blocking = 0;
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

void bm_meter(Rectangle r, float peak, int limited)
{
    float t = peak;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    bm_panel(r);
    DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)((r.width - 2) * t),
                  (int)r.height - 2, limited ? BM_ALERT : BM_ACCENT);
    /* Where the host limiter starts, so a hot voice is visible before it is
     * audible. */
    DrawRectangle((int)(r.x + r.width * 0.85f), (int)r.y, 1, (int)r.height, BM_AMBER);
}
