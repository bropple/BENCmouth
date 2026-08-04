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

typedef struct bm_ui {
    Font  small;
    Font  body;
    Font  title;
    int   loaded;          /* nonzero if a real font file was found         */

    /* One active text field at a time, identified by its rectangle's y. */
    int   focus;
    float caret;           /* blink phase                                   */
} bm_ui;

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
int   bm_slider(const bm_ui *ui, Rectangle r, const char *label,
                float *value, float lo, float hi, const char *fmt);
int   bm_dropdown(const bm_ui *ui, Rectangle r, const char **items, int count,
                  int *index, int *open);
int   bm_textfield(bm_ui *ui, Rectangle r, char *buf, int cap);
void  bm_waveform(Rectangle r, const float *samples, int count);
void  bm_meter(Rectangle r, float peak, int limited);

#endif /* BM_GUI_H */
