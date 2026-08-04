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

/* ------------------------------------------------------------------ */

int bm_button(const bm_ui *ui, Rectangle r, const char *label, int enabled)
{
    Vector2 m = GetMousePosition();
    int over = enabled && CheckCollisionPointRec(m, r);
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
    if (CheckCollisionPointRec(m, r) && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
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

int bm_dropdown(const bm_ui *ui, Rectangle r, const char **items, int count,
                int *index, int *open)
{
    Vector2 m = GetMousePosition();
    int changed = 0, i;

    bm_panel(r);
    bm_text(ui, BM_FONT_SMALL, items[*index], r.x + 8,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_TEXT);
    bm_text(ui, BM_FONT_SMALL, *open ? "^" : "v", r.x + r.width - 18,
            r.y + (r.height - BM_FONT_SMALL) * 0.5f, BM_DIM);

    if (CheckCollisionPointRec(m, r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        *open = !*open;
    }

    if (*open) {
        for (i = 0; i < count; i++) {
            Rectangle item = { r.x, r.y + r.height + 1 + (float)i * r.height,
                               r.width, r.height };
            int over = CheckCollisionPointRec(m, item);
            DrawRectangleRec(item, over ? BM_EDGE : BM_PANEL);
            DrawRectangleLinesEx(item, 1, BM_BORDER);
            bm_text(ui, BM_FONT_SMALL, items[i], item.x + 8,
                    item.y + (item.height - BM_FONT_SMALL) * 0.5f,
                    i == *index ? BM_ACCENT : BM_TEXT);
            if (over && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                *index = i;
                *open = 0;
                changed = 1;
            }
        }
    }
    return changed;
}

int bm_textfield(bm_ui *ui, Rectangle r, char *buf, int cap)
{
    Vector2 m = GetMousePosition();
    int len = (int)strlen(buf);
    int changed = 0, key;

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        ui->focus = CheckCollisionPointRec(m, r) ? (int)r.y + 1 : 0;
    }

    bm_panel(r);
    if (ui->focus == (int)r.y + 1) {
        DrawRectangleRoundedLines(r, BM_RADIUS / r.height, 4, BM_ACCENT);

        while ((key = GetCharPressed()) != 0) {
            if (key >= 32 && key < 127 && len < cap - 1) {
                buf[len++] = (char)key;
                buf[len] = '\0';
                changed = 1;
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (len > 0) { buf[--len] = '\0'; changed = 1; }
        }
        ui->caret += GetFrameTime();
        if (ui->caret > 1.0f) ui->caret -= 1.0f;
    }

    /* Show the tail when the text outruns the box, which is where the caret is
     * and therefore what the typist needs to see. */
    {
        const char *shown = buf;
        float w = bm_text_measure(ui, BM_FONT_BODY, shown, 0.0f);
        while (w > r.width - 20 && *shown != '\0') {
            shown++;
            w = bm_text_measure(ui, BM_FONT_BODY, shown, 0.0f);
        }
        bm_text(ui, BM_FONT_BODY, shown, r.x + 9,
                r.y + (r.height - BM_FONT_BODY) * 0.5f, BM_TEXT);
        if (ui->focus == (int)r.y + 1 && ui->caret < 0.5f) {
            DrawRectangle((int)(r.x + 10 + w), (int)(r.y + 6), 2,
                          (int)(r.height - 12), BM_ACCENT);
        }
    }
    return changed;
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
