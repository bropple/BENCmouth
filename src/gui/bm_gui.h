/*
 * BENCmouth GUI - theme and widget set
 *
 * The widgets are drawn here rather than taken from a toolkit. That is not
 * NIH: the BENCO look is flat fills, 1px dim borders and small radii, and a
 * flat dark rectangle is what an immediate-mode renderer gives you by default.
 * Bending a native widget set toward it would mean fighting scrollbars, focus
 * rings and animations at every step - more work, for a worse result.
 *
 * There are about a dozen controls in this application and each is a few dozen
 * lines.
 */

#ifndef BM_GUI_H
#define BM_GUI_H

#include "raylib.h"

/* ------------------------------------------------------------------ *
 * Palette - every value from the BENCO style guide, nowhere else.
 * ------------------------------------------------------------------ */

extern Color BM_BG;        /* window background, a green-tinted near-black  */
extern Color BM_PANEL;     /* one step up, for inputs and cards             */
extern Color BM_BORDER;    /* 1px, dim, never decorative                    */
extern Color BM_TEXT;      /* phosphor - the screen-glow green, not white   */
extern Color BM_DIM;       /* labels, captions, anything secondary          */
extern Color BM_ACCENT;    /* the brand green: fills, active states         */
extern Color BM_EDGE;      /* pressed states and outlines                   */
extern Color BM_ALERT;     /* errors                                        */
extern Color BM_AMBER;     /* warnings - a limiter that engaged             */

#define BM_RADIUS  3       /* small radii are the default; pills are rare   */
#define BM_PAD     10
#define BM_ROW     26

/* Terminus is a bitmap design, so it is crisp at its native sizes and mushy
 * between them. These are native sizes, and the font is loaded with point
 * filtering to keep it that way - unantialiased text at fixed sizes *is* the
 * terminal look, arrived at honestly rather than simulated. */
#define BM_FONT_SMALL  16
#define BM_FONT_BODY   20
#define BM_FONT_TITLE  32

/* What a right-click menu came back with. Read by the widget that owns the
 * menu, on the frame after the click - see bm_ui_overlay. */
enum { BM_MENU_NONE = 0, BM_MENU_CUT, BM_MENU_COPY, BM_MENU_PASTE, BM_MENU_ALL };

typedef struct bm_ui {
    Font  small;
    Font  body;
    Font  title;
    int   loaded;          /* nonzero if a real font file was found         */
    const char *font_name; /* which one, for the status line                */

    /* One active text field at a time, by caller-chosen id. Zero is nobody. */
    int   focus;

    /* Anything popped up over the layout - a dropdown list, a context menu -
     * has to be drawn after the widgets it covers and has to take the mouse
     * away from them. Immediate mode gives you neither for free: widgets draw
     * and hit-test in call order, so a list drawn where it is declared ends up
     * underneath everything declared later, and the controls beneath it stay
     * live. Both symptoms had the same cause.
     *
     * So popups are handled in two parts. The widget that owns one takes its
     * input where it is called - it is on top, so it gets first refusal - and
     * publishes the rectangle here. Widgets called later see the rectangle and
     * ignore a mouse inside it. Drawing is deferred to bm_ui_overlay, which
     * runs after the whole layout. */
    Rectangle block;
    int       blocking;

    /* Deferred draw: 1 = dropdown list, 2 = context menu. */
    int         pop_kind;
    Rectangle   pop_rect;
    const char **pop_items;
    int         pop_count;
    int         pop_sel;

    /* Context menu, which belongs to whichever text box opened it. */
    int   menu_open;
    int   menu_owner;
    int   menu_action;
} bm_ui;

/* Per-text-box state. Kept by the caller so a widget can stay a function:
 * a caret is a position in a buffer that has to survive between frames, and
 * there is nowhere else for it to live. */
typedef struct bm_edit {
    int   caret;       /* byte index of the insertion point                 */
    int   sel;         /* the other end of the selection; == caret if none  */
    float scroll;      /* pixels scrolled down                             */
    float blink;
    int   drag_text;   /* sweeping out a selection with the mouse           */
    int   drag_bar;    /* dragging the scrollbar thumb                      */
    float drag_grab;   /* where in the thumb it was grabbed                 */
} bm_edit;

void bm_ui_init(bm_ui *ui);
void bm_ui_free(bm_ui *ui);

/* Text is drawn by size, and the size picks the font.
 *
 * An earlier version passed a Font and inferred the size from its texture id.
 * That works only while the three fonts are distinct - and when no Terminess is
 * installed all three fall back to the same built-in font, so every comparison
 * matched the first branch and the whole interface drew at title size. Size is
 * the parameter that actually varies, so it is the one to pass. */
void bm_text(const bm_ui *ui, int size, const char *s, float x, float y, Color c);

/* Style-guide letter-spacing: 1px on labels and headings, never on body text. */
void bm_text_spaced(const bm_ui *ui, int size, const char *s, float x, float y, Color c);

float bm_text_measure(const bm_ui *ui, int size, const char *s, float spacing);

void  bm_panel(Rectangle r);
void  bm_divider(float x, float y, float w);
void  bm_label(const bm_ui *ui, const char *s, float x, float y);

int   bm_button(const bm_ui *ui, Rectangle r, const char *label, int enabled);

/* An information button: a lowercase i in a circle. */
int   bm_info_button(const bm_ui *ui, Rectangle r);

/* A button that stays pressed. Filled when on, outlined when off - the state
 * is the fill, so it reads at a glance rather than only from the label. */
int   bm_toggle(const bm_ui *ui, Rectangle r, const char *label, int *on,
                int enabled);
int   bm_slider(const bm_ui *ui, Rectangle r, const char *label,
                float *value, float lo, float hi, const char *fmt);
int   bm_dropdown(bm_ui *ui, Rectangle r, const char **items, int count,
                  int *index, int *open);

/* A wrapped, scrolling, editable text box: caret, selection, clipboard, and a
 * scrollbar when the text outgrows the box. `id` is any nonzero number unique
 * among the boxes on screen - it is what focus is tracked by. */
int   bm_textbox(bm_ui *ui, int id, Rectangle r, char *buf, int cap, bm_edit *st);

/* The same wrapping and scrolling, read-only. */
void  bm_textview(bm_ui *ui, Rectangle r, const char *s, bm_edit *st, Color c);

/* Draws whatever popped up, above everything else. Call once, last, before
 * EndDrawing. */
void  bm_ui_overlay(bm_ui *ui);
void  bm_waveform(Rectangle r, const float *samples, int count);
void  bm_meter(Rectangle r, float peak, int limited);

#endif /* BM_GUI_H */
