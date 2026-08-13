/*
 * BENCmouth GUI
 *
 * Links against the public API only - bencmouth.h and libbencmouth.a, exactly
 * as the CLI does. It never reaches into src/core. That is not tidiness: if
 * the GUI needs something the public API cannot express, that is a finding
 * about the API, and an internal include would hide it.
 *
 * Audio streams from bm_read() into raylib's AudioStream callback with no
 * buffer of the whole utterance in between, which is what the pull interface
 * was shaped for - and it means a slider moved mid-sentence is audible
 * immediately rather than at the next render.
 */

#include "bencmouth.h"
#include "bm_gui.h"
#include "bm_embed.h"
#include "bm_filedlg.h"
#include "bm_player.h"
#include "bm_render.h"
#include "bm_shm.h"
#include "bm_roll_ui.h"
#include "bm_song_ui.h"
#include "bm_songfile.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Taller than it was. Song mode needs a panel with two columns in it, and the
 * voice grew three sliders, which between them added about 80 px of layout
 * that has to come from somewhere, and 34 more when song mode got controls for
 * its tempo.
 *
 * Width has stopped growing, though, and that is the rule worth keeping: when
 * the effects outgrew their column the answer was to scroll the column rather
 * than add a fourth one. A list that will keep growing gets a scrollbar; a
 * fixed handful of controls that will not grow again can have the pixels.
 *
 * WIN_MIN_H is measured rather than guessed, and it had been guessed. The
 * layout is computed top-down, so its total height does not depend on the
 * window - at anything shorter than the total, the status line is simply off
 * the bottom. The old 740 was about twelve pixels short of what the layout
 * already needed, so the minimum size had been quietly clipping the one line
 * that reports what the program is doing. Rendered at a range of heights: the
 * last row is whole at 786. */
#define WIN_W       900
#define WIN_H       814
#define WIN_MIN_W   800
#define WIN_MIN_H   790

/* The band the active tab's panel gets. Fixed rather than proportional: the
 * controls below it are a fixed height each, so a proportional panel would
 * push the status line off the bottom of a short window. */
#define PANEL_H     244

/* Everything below the tab panel: the transport, the divider, the voice and
 * effects columns, the scope and meters, and the status line. Measured from the
 * window this layout was designed at - WIN_H less the panel and everything
 * above it - and used only to work out how much spare height there is for the
 * roll, which is the one panel that can use it. */
#define BELOW_PANEL 474
#define TEXT_CAP    2048
#define SCOPE_LEN   2048
#define SAMPLE_RATE 22050

/* Widget ids, which are what focus is tracked by.
 *
 * One numbering for every widget that can hold the caret, text boxes and slider
 * readouts alike, because there is one caret. Two schemes would let a slider
 * and a text box both be lit at once, and both take the same keystroke.
 *
 * 1 is this file's text box; 2 to 6 and 9 to 10 are the song panel's, in
 * bm_song_ui.c. The sliders start well clear of those and take a block each, so
 * adding a parameter costs nothing here. */
#define ID_TEXT          1
#define ID_PHONEMES      7
#define ID_ABOUT         8
#define ID_VOICE_SLIDER  100
#define ID_FX_SLIDER     200

/* ------------------------------------------------------------------ *
 * Shared with the audio callback.
 *
 * The callback runs on raylib's audio thread, so it touches exactly two
 * things: the engine (which never allocates or blocks, which is why this is
 * safe) and the scope ring. Everything else is main-thread only.
 * ------------------------------------------------------------------ */

static bm_engine_storage g_storage;
static bm_engine        *g_engine;
static volatile int      g_speaking;

/* Whether there is an audio device at all. Zero in editor mode, where the
 * plugin owns the sound - and checked rather than assumed, because raylib's
 * stream calls on an uninitialized device are undefined rather than ignored. */
static int               g_audio;

/* The roll plays a rendered score rather than a live engine, because a timeline
 * needs to start in the middle and the engine cannot. See bm_render.h.
 *
 * Two renders, used alternately. The audio thread may be inside a memcpy from
 * the buffer when a new render begins, and bm_render_score reallocates - so the
 * one being played is never the one being written, and the buffer a callback
 * could still be reading stays alive until the render after next. It costs one
 * spare copy of the audio and removes the only way this could crash. */
static bm_player         g_player;
static bm_render         g_render[2];
static int               g_render_slot;
/* Latched by the audio thread when an utterance runs out, cleared by the UI
 * once it has been noticed. A flag rather than an edge: see audio_cb. */
static volatile int      g_finished;
static float             g_scope[SCOPE_LEN];
static volatile int      g_scope_at;
static volatile float    g_peak;
static volatile int      g_limited;

/* Loudness, which is not what the peak meter measures.
 *
 * Peak and loudness come apart exactly where this synthesizer is most
 * interesting. Across the voices that carry an effects chain, peak spans four
 * to one - Aggressor 0.124 against Gravel 0.527 - while their RMS spans three
 * decibels. Nothing is quieter; drive and crush collapse the crest factor,
 * which is what distortion does. The ear follows RMS far more closely than
 * peak, so a peak-only meter reads a driven voice as much quieter than it
 * sounds, and anyone trimming gain by eye off that meter is being told the
 * opposite of the truth.
 *
 * Accumulated over the whole utterance and reset by SPEAK, to match the peak
 * hold beside it: a short-time meter would jitter and would not let two voices
 * be compared once they had finished speaking, which is the thing this is for.
 *
 * The main thread zeroes these to reset, which races with the audio thread by
 * at most one block - the same non-atomic arrangement g_peak has always used,
 * and for a meter the cost of losing 1024 samples out of an utterance is
 * nothing. */
static volatile float    g_rms;      /* published, linear */
static double            g_rms_sum;  /* audio thread only, except on reset */
static volatile double   g_rms_n;

static void audio_cb(void *buffer, unsigned int frames)
{
    short *out = (short *)buffer;
    float  chunk[1024];
    unsigned int done = 0;

    while (done < frames) {
        unsigned int want = frames - done;
        size_t got = 0;
        unsigned int i;

        if (want > 1024) want = 1024;

        if (g_player.playing) {
            /* A rendered score. It stops itself at the end, and the same latch
             * the engine path uses tells the UI about it. */
            got = bm_player_read(&g_player, chunk, want);
            if (!g_player.playing) { g_speaking = 0; g_finished = 1; }
        } else if (g_speaking) {
            got = bm_read(g_engine, chunk, want);
            /* Latched, not left for the UI to spot as a change between frames.
             * A short utterance can begin and end inside a single 16 ms video
             * frame - more easily still with a large audio buffer - and a
             * "was it speaking last time I looked" test then never sees it
             * speaking at all, leaving the status line reading "speaking" over
             * a silent engine. Setting it only on this path is also what keeps
             * STOP's own message: STOP clears g_speaking itself, so the next
             * callback does not come through here. */
            if (got == 0) { g_speaking = 0; g_finished = 1; }
        }

        for (i = 0; i < want; i++) {
            float s = (i < got) ? chunk[i] : 0.0f;
            float a = s < 0.0f ? -s : s;

            if (a > g_peak) g_peak = a;
            if (a > 0.85f) g_limited = 1;

            /* Only what the engine actually produced. Past `got` these are the
             * zeros that pad the last block and then every block after it, and
             * averaging those in would walk the reading down toward silence for
             * as long as the window stayed open. Peak does not care, being a
             * maximum; a mean cares a great deal. */
            if (i < got) {
                g_rms_sum += (double)s * (double)s;
                g_rms_n   += 1.0;
            }

            g_scope[g_scope_at] = s;
            g_scope_at = (g_scope_at + 1) % SCOPE_LEN;

            out[done + i] = (short)bm_pcm_sample(s, 0);
        }
        /* Published once per block rather than per sample: the square root is
         * cheap but this is the audio thread, and the meter is redrawn sixty
         * times a second against a block rate of about twenty. */
        if (g_rms_n > 0.0) g_rms = (float)sqrt(g_rms_sum / g_rms_n);
        done += want;
    }
}

/* ------------------------------------------------------------------ */

typedef struct {
    const char *key;
    const char *label;
    float       lo, hi;
    const char *fmt;
} param_row;

/* The keys are the .bmvoice file keys, so what the sliders write and what a
 * saved file contains cannot drift apart. */
static const param_row PARAMS[] = {
    { "f0_base",         "pitch",        60.0f, 260.0f, "%.0f Hz" },
    { "f0_range",        "range",         0.0f,  10.0f, "%.1f st" },
    { "f0_flutter",      "flutter",       0.0f,   1.0f, "%.2f"    },
    { "vibrato",         "vibrato",       0.0f,   3.0f, "%.2f st" },
    { "vibrato_rate",    "vib. hz",       0.0f,  12.0f, "%.1f Hz" },
    /* 0 folds, 1 pipe, 2 bell, and it crossfades - see bm_voice.source. */
    { "source",          "source",        0.0f,   2.0f, "%.2f"    },
    { "speed",           "speed",         0.5f,   2.0f, "%.2f"    },
    /* The top of the range is 1.4 rather than 1.3 because Cadet needs it: a
     * child's vocal tract really is about a third shorter than an adult's, and
     * a slider that cannot reach the preset it ships with is a slider that
     * silently rewrites the voice the moment it is touched. */
    { "throat",          "throat",        0.7f,   1.4f, "%.2f"    },
    { "mouth",           "mouth",         0.7f,   1.4f, "%.2f"    },
    { "tilt",            "tilt",          0.0f,  14.0f, "%.1f dB" },
    { "breathiness",     "breath",        0.0f,  12.0f, "%.1f dB" },
    { "open_quotient",   "open q.",       0.3f,   0.7f, "%.2f"    },
    { "whisper",         "whisper",       0.0f,   1.0f, "%.2f"    },
    { "gain",            "gain",          0.0f,   1.5f, "%.2f"    },
    { "coarticulation",  "coart.",        0.0f,   1.0f, "%.2f"    },
    { "prosody",         "prosody",       0.0f,   1.0f, "%.2f"    },
    { "formant_glide",   "glide",         0.0f,   1.0f, "%.2f"    },
    { "bandwidth_track", "bandw.",        0.0f,   1.0f, "%.2f"    },
    { "flatten",         "flatten",       0.0f,   1.0f, "%.2f"    }
};
#define NPARAMS ((int)(sizeof PARAMS / sizeof PARAMS[0]))

/* The effects chain, in its own column. Same table shape, different struct -
 * and that separation is the point: a voice describes a speaker, an effect is
 * something done to the sound afterwards. See bm_effects in bencmouth.h. */
static const param_row FX_PARAMS[] = {
    { "ring",    "ring",       0.0f,   1.0f,  "%.2f"    },
    { "ring_hz", "ring hz",    0.0f, 400.0f,  "%.0f Hz" },
    /* Off by default everywhere, so this row reads 0.00 until someone asks
     * for it - see bm_effects.ring_drift for what it is and why. */
    { "ring_drift","drift",     0.0f,  1.0f,  "%.2f"    },
    { "comb",    "comb",       0.0f,   1.0f,  "%.2f"    },
    { "comb_hz", "comb hz",   40.0f, 900.0f,  "%.0f Hz" },
    { "chorus",  "chorus",     0.0f,   1.0f,  "%.2f"    },
    { "chorus_hz","chorus hz", 0.0f,   3.0f,  "%.2f Hz" },
    { "drive",   "drive",      0.0f,   1.0f,  "%.2f"    },
    { "vocoder", "vocoder",    0.0f,   1.0f,  "%.2f"    },
    /* The carrier's pitch. Top of the range is the top of a soprano; past that
     * the bands are further apart than the harmonics and the vocoder has more
     * channels than there is anything to put in them. 0 is the default, about
     * 110, the same way chorus_hz and echo_ms work. */
    { "vocoder_hz","voc. hz",  0.0f, 400.0f,  "%.0f Hz" },
    { "crush",   "crush",      0.0f,   1.0f,  "%.2f"    },
    { "echo",    "echo",       0.0f,   1.0f,  "%.2f"    },
    /* 0 is not "no delay" - it selects the default around 180 ms, the same way
     * chorus_hz does. The mix control above is what turns it off. */
    { "echo_ms", "echo ms",    0.0f, 360.0f,  "%.0f ms" },
    { "reverb",  "reverb",     0.0f,   1.0f,  "%.2f"    },
    { "reverb_size","room",    0.0f,   1.0f,  "%.2f"    },
    /* Bottom of the range is 0.1, not 0. In the file format a level of 0 means
     * unity - that is what keeps an all-zero bm_effects an exact bypass - so a
     * slider that could reach 0 would show "0.00" for unity gain and let you
     * set a value that means the opposite of what it reads. */
    { "level",   "level",      0.1f,   4.0f,  "%.2f"    }
};
#define NFXPARAMS ((int)(sizeof FX_PARAMS / sizeof FX_PARAMS[0]))

static float param_get(const bm_voice *v, const char *key)
{
    /* The voice is a flat block of floats after the name pointer, in the same
     * order as bm_voice_set_param accepts. Reading it back positionally keeps
     * this table the only place the order is written down. */
    static const char *ORDER[] = {
        "f0_base", "f0_range", "f0_flutter", "vibrato", "vibrato_rate",
        "source", "speed", "throat", "mouth",
        "breathiness", "tilt", "open_quotient", "whisper", "gain",
        "coarticulation", "prosody", "formant_glide", "bandwidth_track",
        "flatten"
    };
    const float *f = (const float *)(const void *)&v->f0_base;
    int i;
    for (i = 0; i < (int)(sizeof ORDER / sizeof ORDER[0]); i++) {
        if (strcmp(ORDER[i], key) == 0) return f[i];
    }
    return 0.0f;
}

/* Where `name` sits in a dropdown's list, or `fallback` if it is not in it -
 * which is the normal case for a voice that has been edited or loaded from a
 * file, and not an error. */
static int name_index(const char **names, int count, const char *name,
                      int fallback)
{
    int i;
    if (name == 0) return fallback;
    for (i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) return i;
    }
    return fallback;
}

/* Alphabetical, case-insensitive, for the voice dropdown.
 *
 * The presets are declared in the order they were written - default, Retro, the
 * classic set, then whatever arrived last - which is a useful order to read the
 * source in and a useless one to hunt a name in once there are more than a
 * dozen. The list is sorted for display only; nothing else depends on preset
 * order, because everything else looks them up by name. */
/* Which score sings, for the two tabs that have one. The song tab's is the text
 * in its editor; the roll tab's is compiled from its notes. Both go in as
 * phonemes - bm_speak_phonemes honours markup for free, which is the whole
 * reason commands survive into the phoneme string. */
static const char *sing_source(int tab, const bm_song_ui *song,
                               const bm_roll_ui *roll)
{
    return (tab == 2) ? roll->score : song->score;
}

static int by_name(const void *a, const void *b)
{
    const char *x = (*(const bm_voice *const *)a)->name;
    const char *y = (*(const bm_voice *const *)b)->name;
    for (; *x != '\0' && *y != '\0'; x++, y++) {
        int cx = (*x >= 'A' && *x <= 'Z') ? *x - 'A' + 'a' : *x;
        int cy = (*y >= 'A' && *y <= 'Z') ? *y - 'A' + 'a' : *y;
        if (cx != cy) return cx - cy;
    }
    return (int)(unsigned char)*x - (int)(unsigned char)*y;
}

/* The same trick for the effects chain, which is laid out the same way: a name
 * pointer and then the floats, in the order bm_effects_set_param accepts. */
static float fx_get(const bm_effects *e, const char *key)
{
    const float *f = (const float *)(const void *)&e->ring;
    int i;

    /* Shown as what it does rather than as what is stored. See the FX_PARAMS
     * entry for why the stored value for unity is 0. */
    if (strcmp(key, "level") == 0 && e->level <= 0.0f) return 1.0f;

    for (i = 0; i < NFXPARAMS; i++) {
        if (strcmp(FX_PARAMS[i].key, key) == 0) return f[i];
    }
    return 0.0f;
}

/* `--shot PREFIX` renders each tab for a few frames, writes PREFIX-TEXT.png and
 * PREFIX-SONG.png, and exits.
 *
 * It is here rather than in a script because there is no other way to see this
 * window without a display: under Xvfb the layout can be checked, and a layout
 * bug that pushes the status line off the bottom of a short window is exactly
 * the kind of thing that otherwise ships. It is also what produces the
 * screenshots in the README.
 *
 *   xvfb-run -a ./bencmouth-gui --shot layout
 *
 * The files land in the working directory whatever the prefix says - raylib's
 * TakeScreenshot takes the basename and writes beside the process, which is
 * worth knowing before hunting for a screenshot in the directory you named. */
static const char *parse_shot(int argc, char **argv)
{
    int i;
    for (i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0) return argv[i + 1];
    }
    return 0;
}

/* `--editor NAME` runs this program as a plugin's window rather than as a
 * program of its own: it attaches to the shared block NAME names, edits the
 * song the plugin is holding, and publishes it back.
 *
 * The plugin is the one making the sound, so this mode opens no audio device.
 * A second one would be the same song playing a few milliseconds out of step
 * with itself. */
static const char *parse_editor(int argc, char **argv)
{
    int i;
    for (i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--editor") == 0) return argv[i + 1];
    }
    return 0;
}

/* Optional "WxH" on the command line. Useful for a cramped desktop, and it is
 * what produced the screenshot in the README at a sensible resolution. */
static void parse_size(int argc, char **argv, int *w, int *h)
{
    int i, a, b;
    for (i = 1; i < argc; i++) {
        if (sscanf(argv[i], "%dx%d", &a, &b) == 2 &&
            a >= WIN_MIN_W && b >= WIN_MIN_H && a <= 8192 && b <= 8192) {
            *w = a;
            *h = b;
        }
    }
}

int main(int argc, char **argv)
{
    bm_ui     ui;
    bm_config config;
    bm_voice  voice;
    AudioStream stream;

    char  text[TEXT_CAP] = "Hello. I am BENCmouth.";
    char  phonemes[TEXT_CAP * 3] = "";
    char  status[192] = "";
    char  scope[SCOPE_LEN];
    float scope_copy[SCOPE_LEN];

    const char *voice_names[64];
    /* Sorted alongside the names, so the dropdown index still selects the
     * preset the label belongs to. Keeping the pointers rather than re-looking
     * up by name also means two presets could share a name without the wrong
     * one being chosen. */
    const bm_voice *voice_list[64];
    int   voice_count = 0, voice_index = 0, voice_open = 0;
    const char *fx_names[32];
    int   fx_count = 0, fx_index = 0, fx_open = 0, fx_scroll = 0;
    bm_effects effects;
    int   i, dirty = 1;
    int   have_dict = 0, use_dict = 1;
    int   info_open = 0;

    /* Three tabs, and each keeps its own voice. Song mode wants prosody off and
     * a little vibrato; speech wants the opposite, and one shared voice would
     * mean every trip through the song tab quietly retuned the text tab. The
     * sliders always edit whichever is in front.
     *
     * Held as an array rather than as one stashed copy, which is what it was
     * while there were two of them: a swap works for a pair and quietly loses
     * the third. */
    static const char *TABS[] = { "TEXT", "SONG", "ROLL" };
#define TAB_COUNT ((int)(sizeof TABS / sizeof TABS[0]))
    int        tab = 0, prev_tab = 0;
    bm_voice   tab_voice[TAB_COUNT];
    bm_effects tab_fx[TAB_COUNT];
    bm_song_ui song;
    /* Static, and it has to be: the roll carries its undo history, which is
     * thirty-two states each way at eighteen kilobytes apiece - about 1.2 MB.
     * That is nothing in .bss and far too much for a stack frame, and the
     * frame it would have gone in is main's on a thread whose stack is a
     * megabyte on Windows. It would have run here and crashed there. */
    static bm_roll_ui roll;
    static char song_path[1024] = "";

    int        roll_loop = 0;

    /* Where the roll's transport was started from, so the playhead can go back
     * there when the song runs out. Negative when the roll is not what is
     * sounding - the text and song tabs speak an utterance and have no head to
     * put back. */
    float      play_from = -1.0f;

    /* Editor mode: this window belongs to a plugin in another process. See
     * parse_editor and src/plugin/bm_shm.h. */
    const char *editor_name = 0;
    bm_shm      editor_shm;
    uint32_t    editor_seq = 0;       /* of the last song taken from the plugin */
    int         editor_dirty = 0;     /* something changed; publish it back */
    double      editor_published = 0.0;

    const char *shot = 0;
    int         shot_frames = 0;
    Texture2D logo = { 0, 0, 0, 0, 0 };
    bm_edit info_st;
    char  voice_name_buf[64] = "";
    static char about[24576];
    bm_edit text_st, phon_st;
    Color status_color;

    (void)scope;

    bm_config_default(&config);
    config.sample_rate = SAMPLE_RATE;
    have_dict = bm_dict_count() > 0;
    use_dict  = have_dict;
    config.markup = 1;          /* the GUI is a place to experiment */
    voice = config.voice;
    effects = config.effects;

    for (i = 0; i < bm_voice_preset_count() &&
                i < (int)(sizeof voice_list / sizeof voice_list[0]); i++) {
        voice_list[voice_count++] = bm_voice_preset_at(i);
    }
    qsort(voice_list, (size_t)voice_count, sizeof voice_list[0], by_name);
    for (i = 0; i < voice_count; i++) voice_names[i] = voice_list[i]->name;

    /* Start on whichever entry the default voice turned out to be, rather than
     * on whatever sorts first - the dropdown has to agree with the sliders. */
    for (i = 0; i < voice_count; i++) {
        if (voice_list[i]->name == voice.name) { voice_index = i; break; }
    }
    for (i = 0; i < bm_effects_preset_count() &&
                i < (int)(sizeof fx_names / sizeof fx_names[0]); i++) {
        fx_names[fx_count++] = bm_effects_preset_at(i)->name;
    }

    editor_name = parse_editor(argc, argv);
    if (editor_name != 0 && bm_shm_attach(&editor_shm, editor_name) != 0) {
        /* The block is gone, which means the plugin that made it is gone. There
         * is nothing to edit and nothing to publish to, so this is not a window
         * worth opening. */
        fprintf(stderr, "bencmouth-gui: no plugin at %s\n", editor_name);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    {
        int w = WIN_W, h = WIN_H;
        parse_size(argc, argv, &w, &h);
        InitWindow(w, h, editor_name ? "BENCmouth - plugin editor" : "BENCmouth");
    }
    shot = parse_shot(argc, argv);
    if (shot != 0) shot_frames = 8;   /* let the font and logo textures land */
    /* ESC closes the information window and nothing else. raylib exits on it
     * by default, which in a program built around a text field means one
     * stray keystroke throws away what you were typing. */
    SetExitKey(KEY_NULL);
    SetWindowMinSize(WIN_MIN_W, WIN_MIN_H);
    SetTargetFPS(60);
    /* No device in editor mode: the plugin is already making the sound, and a
     * second one would be the same song playing a few milliseconds out of step
     * with itself. Everything downstream checks g_audio before touching the
     * stream. */
    if (editor_name == 0) {
        InitAudioDevice();
        g_audio = 1;
    }

    bm_ui_init(&ui);
    bm_song_ui_init(&song);
    bm_roll_ui_init(&roll);
    for (i = 0; i < TAB_COUNT; i++) {
        tab_voice[i] = voice;
        tab_fx[i] = effects;
    }
    tab_voice[1] = song.song.voice;
    tab_fx[1]    = song.song.effects;
    tab_voice[2] = roll.song.voice;
    tab_fx[2]    = roll.song.effects;
    bm_roll_ui_refresh(&roll, &tab_voice[2]);
    memset(&text_st, 0, sizeof text_st);
    memset(&phon_st, 0, sizeof phon_st);
    memset(&info_st, 0, sizeof info_st);

    /* The wordmark, from the binary rather than from disk for the same reason
     * as the font. Bilinear here and nowhere else: it is a 1095 px image drawn
     * at about a third of that, and point sampling a photograph-sized bitmap
     * down by 3x is what aliasing looks like. The rule is "do not smooth the
     * bitmap font", not "never filter anything". */
    {
        Image li = LoadImageFromMemory(".png", BM_LOGO_PNG, (int)BM_LOGO_PNG_LEN);
        if (li.data != 0) {
            logo = LoadTextureFromImage(li);
            SetTextureFilter(logo, TEXTURE_FILTER_BILINEAR);
            UnloadImage(li);
        }
    }

    /* Assembled once, from the repository's own licence files as they were at
     * build time, so what this window shows and what the archive ships cannot
     * drift apart. */
    snprintf(about, sizeof about,
             "%s\n"
             "================================================================\n"
             "  THIRD-PARTY NOTICES\n"
             "================================================================\n"
             "%s\n"
             "================================================================\n"
             "  SIL OPEN FONT LICENSE 1.1  -  Terminus (TTF), embedded above\n"
             "================================================================\n"
             "%s\n",
             (const char *)BM_LICENSE_MIT,
             (const char *)BM_NOTICE,
             (const char *)BM_LICENSE_OFL);
    text_st.caret = text_st.sel = (int)strlen(text);

    if (bm_engine_init(&g_storage, &config, &g_engine) != BM_OK) {
        TraceLog(LOG_ERROR, "engine init failed");
        return 1;
    }

    SetAudioStreamBufferSizeDefault(1024);
    if (g_audio) {
        stream = LoadAudioStream(SAMPLE_RATE, 16, 1);
        SetAudioStreamCallback(stream, audio_cb);
        PlayAudioStream(stream);
    }

    /* Window icon: H. Hex, from the roster.
     *
     * Windows takes it from the GLFW_ICON resource compiled into the binary,
     * which carries every size Explorer and the taskbar want and needs no file
     * at runtime. Setting it again here from a 64x64 PNG would override that
     * with a single size, so on Windows this is left alone.
     *
     * Everywhere else there is no such thing as an executable resource, so the
     * icon comes out of the binary instead - it used to be searched for on
     * disk, which meant the program lost its icon whenever it was started from
     * somewhere other than its own directory. */
#if !defined(_WIN32)
    {
        Image icon = LoadImageFromMemory(".png", BM_ICON_PNG,
                                         (int)BM_ICON_PNG_LEN);
        if (icon.data != 0) { SetWindowIcon(icon); UnloadImage(icon); }
    }
#endif

    status_color = BM_DIM;
    {
        /* Say which pronunciation path this build has. Without the dictionary
         * every word goes through the letter-to-sound rules, which get "robot"
         * as R AA B AA T - correct for the rules, wrong for the word - and
         * nothing on screen said so. */
        int  words = bm_dict_count();
        char dict[64];

        if (words > 0) snprintf(dict, sizeof dict, "%d-word dictionary", words);
        else           snprintf(dict, sizeof dict, "no dictionary - rules only");

        snprintf(status, sizeof status, "ready  -  %s%s  -  %s", ui.font_name,
                 ui.loaded ? "" : "  (no Terminus TTF found)", dict);
    }

    while (!WindowShouldClose()) {
        float W = (float)GetScreenWidth();
        float y;

        /* ---- the plugin, if this window belongs to one ----
         *
         * Once a frame, and all of it is polling: no callbacks, no thread, and
         * nothing that can block the process on the other end. An editor that
         * stalls or dies stops publishing, and the plugin goes on playing the
         * last song it was given. */
        if (editor_name != 0) {
            static char handed[BM_SHM_TEXT];
            size_t      n = 0;

            if (editor_shm.block == 0 || editor_shm.block->quit) break;
            editor_shm.block->heartbeat++;

            /* A song from the plugin: at startup, and any time the host loads
             * a different project state underneath the window. */
            if (bm_shm_take(&editor_shm.block->to_editor, &editor_seq,
                            handed, sizeof handed, &n) && n > 0) {
                static bm_song loaded;
                static char    loaded_score[BM_SONG_SCORE_MAX];
                char err[192];

                if (bm_song_parse(handed, n, &loaded, loaded_score,
                                  sizeof loaded_score, err, sizeof err) == 0) {
                    roll.song = loaded;
                    roll.song.voice.name = roll.song.voice_name;
                    memcpy(roll.score, loaded_score, sizeof roll.score);
                    snprintf(roll.title, sizeof roll.title, "%s",
                             roll.song.title);
                    roll.tempo_applied = roll.song.tempo;
                    roll.selected = -1;
                    roll.dirty = 1;
                    /* Handed a different song, so the history starts here -
                     * undoing into the previous project's music would be a
                     * surprise nobody could account for. */
                    bm_roll_history_init(&roll.hist);
                    voice = roll.song.voice;
                    effects = roll.song.effects;
                    tab_voice[2] = voice;
                    tab_fx[2] = effects;
                    tab = 2;
                    prev_tab = 2;
                    bm_engine_set_voice(g_engine, &voice);
                    bm_roll_ui_refresh(&roll, &voice);
                    snprintf(status, sizeof status,
                             "editing the plugin's song  -  %d notes",
                             roll.song.roll.count);
                    status_color = BM_ACCENT;
                } else {
                    snprintf(status, sizeof status, "%.150s", err);
                    status_color = BM_ALERT;
                }
            }

            /* The transport, for the roll's playhead. */
            roll.head_ms = (float)editor_shm.block->pos_ms;
        }

        /* The utterance ends on the audio thread, which has no way to say so
         * beyond this flag. Without it the status line went on reading
         * "speaking" over a silent engine - and a readout that is wrong about
         * the one thing it reports is worse than no readout at all. */
        /* Modal. Published before any widget runs, so nothing underneath the
         * scrim reacts to a click meant for the dialog. */
        if (info_open || song.ref_open || roll.help_open) {
            ui.blocking = 1;
            ui.block = (Rectangle){ 0, 0, W, (float)GetScreenHeight() };
            voice_open = 0;
            ui.menu_open = 0;
        }
        /* Only one modal at a time, and the one just asked for wins. */
        if (info_open) { song.ref_open = 0; roll.help_open = 0; }
        if (song.ref_open) roll.help_open = 0;

        if (g_finished) {
            g_finished = 0;
            snprintf(status, sizeof status, "ready");
            status_color = BM_DIM;

            /* Back to where playing began.
             *
             * The head used to be left at the end of the song, which was wrong
             * twice over: nothing is there to look at, and SING would then play
             * from the top while the marker still said the end - the one thing
             * on screen that claims to say where the next note will come from,
             * disagreeing with where it does. Returning to the start of the
             * pass means pressing SING again plays the same passage, which is
             * what listening to a bar over and over is made of. */
            if (play_from >= 0.0f) {
                roll.head_ms = play_from;
                play_from = -1.0f;
            }
        }

        /* Phonemes are recomputed only when the text changes: it is cheap, but
         * not free, and doing it every frame would be silly. */
        if (dirty) {
            size_t n = 0;
            unsigned tf = BM_TEXT_MARKUP | (use_dict ? 0u : BM_TEXT_NO_DICT);
            if (bm_text_to_phonemes_ex(text, 0, phonemes, sizeof phonemes, &n,
                                       tf) != BM_OK) {
                snprintf(phonemes, sizeof phonemes, "(cannot convert that)");
            }
            dirty = 0;
        }

        BeginDrawing();
        ClearBackground(BM_BG);

        /* ---- header ---- */
        {
            Rectangle hex = { BM_PAD, BM_PAD, 34, 34 };
            DrawRectangleRec(hex, (Color){ 0xd9, 0x7a, 0x2b, 255 });
            DrawRectangleLinesEx(hex, 1, (Color){ 0x8a, 0x4d, 0x18, 255 });
            DrawRectangle((int)hex.x + 5, (int)hex.y + 15, 24, 6,
                          (Color){ 0x9a, 0x9d, 0x94, 255 });
            DrawRectangle((int)hex.x + 9, (int)hex.y + 17, 16, 2, BM_ALERT);

            bm_text_spaced(&ui, BM_FONT_TITLE, "B E N C M O U T H", 56, BM_PAD + 2, BM_TEXT);
            bm_label(&ui, "formant speech synthesis", 58, BM_PAD + 30);

            if (bm_info_button(&ui, (Rectangle){ W - BM_PAD - 26, BM_PAD + 4,
                                                 26, 26 })) {
                info_open = 1;
                info_st.scroll = 0.0f;
            }
        }
        bm_divider(BM_PAD, 58, W - 2 * BM_PAD);

        /* ---- tabs ---- */
        y = 66;
        if (bm_tabs(&ui, (Rectangle){ BM_PAD, y, W - 2 * BM_PAD, 26 },
                    TABS, TAB_COUNT, &tab)) {
            /* Put the voice back where it came from and take the new tab's.
             * The one in front is always `voice`, which is the only thing the
             * sliders and the engine ever see. */
            tab_voice[prev_tab] = voice;
            tab_fx[prev_tab]    = effects;
            if (prev_tab == 1) song.song.voice = voice;
            if (prev_tab == 2) roll.song.voice = voice;
            if (prev_tab == 1) song.song.effects = effects;
            if (prev_tab == 2) roll.song.effects = effects;

            voice   = tab_voice[tab];
            effects = tab_fx[tab];
            prev_tab = tab;
            bm_engine_set_voice(g_engine, &voice);
            bm_engine_set_effects(g_engine, &effects);
            /* Both dropdowns have to be told, or they go on displaying the
             * other tab's selection over this tab's sliders. An entry that no
             * longer matches - "edited", or a voice loaded from a file - leaves
             * the index alone, which is the honest outcome: there is no list
             * entry to point at. */
            voice_index = name_index(voice_names, voice_count, voice.name,
                                     voice_index);
            fx_index    = name_index(fx_names, fx_count, effects.name, fx_index);
            bm_ui_defocus(&ui);
            voice_open = 0;
            fx_open    = 0;
        }
        y += 30;

        if (tab == 0) {
            /* ---- text and phonemes ---- */
            bm_label(&ui, "TEXT TO SPEAK", BM_PAD, y);
            if (bm_textbox(&ui, ID_TEXT,
                           (Rectangle){ BM_PAD, y + 18, W - 2 * BM_PAD,
                                        PANEL_H - 18 - 66 },
                           text, TEXT_CAP, &text_st)) {
                dirty = 1;
            }
            /* Selectable and copyable, but not typable: what it says is
             * derived from the text above it and from the DICT button, so an
             * edit here would be overwritten on the next keystroke and would
             * have looked like the box losing what you told it. Lifting the
             * phonemes out, though, is the natural way to start a score. */
            bm_textview(&ui, ID_PHONEMES,
                        (Rectangle){ BM_PAD, y + PANEL_H - 58,
                                     W - 2 * BM_PAD, 58 },
                        phonemes, &phon_st, BM_DIM);
        } else if (tab == 1) {
            int act = bm_song_panel(&ui, &song,
                                    (Rectangle){ BM_PAD, y, W - 2 * BM_PAD,
                                                 (float)PANEL_H },
                                    use_dict);

            if (act == BM_SONG_ACT_LOAD) {
                char path[1024];
                char start[1024];
                int  dlg;

                snprintf(start, sizeof start, "%ssongs", GetApplicationDirectory());
                if (!DirectoryExists(start)) {
                    snprintf(start, sizeof start, "%s../Resources/songs",
                             GetApplicationDirectory());
                }
                if (!DirectoryExists(start)) snprintf(start, sizeof start, "songs");
                if (!DirectoryExists(start)) snprintf(start, sizeof start, ".");

                dlg = bm_open_dialog(GetWindowHandle(), "Load song", start,
                                     "BENCmouth song", "bmsong",
                                     path, sizeof path);
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(status, sizeof status,
                             "no file dialog available - install zenity or kdialog");
                    status_color = BM_ALERT;
                } else if (dlg == BM_DLG_OK) {
                    char err[192];
                    /* Into a scratch song, so a file that fails halfway
                     * through cannot leave the open one half-replaced. */
                    static bm_song  loaded;
                    static char     loaded_score[BM_SONG_SCORE_MAX];

                    if (bm_song_load(path, &loaded, loaded_score,
                                     sizeof loaded_score, err, sizeof err) == 0) {
                        song.song = loaded;
                        /* bm_song refers to its own name buffer, so the copy
                         * above left voice.name pointing into `loaded`. */
                        song.song.voice.name = song.song.voice_name;
                        memcpy(song.score, loaded_score, sizeof song.score);
                        snprintf(song.title, sizeof song.title, "%s",
                                 song.song.title);
                        /* The holds in this file were written at this file's
                         * tempo, so nothing needs retiming until somebody
                         * moves the control. */
                        song.tempo_applied = song.song.tempo;
                        song.score_st.caret = song.score_st.sel = 0;
                        song.score_st.scroll = 0.0f;
                        song.title_st.caret = song.title_st.sel = 0;
                        snprintf(song_path, sizeof song_path, "%s", path);

                        voice = song.song.voice;
                        effects = song.song.effects;
                        bm_engine_set_voice(g_engine, &voice);
                        bm_engine_set_effects(g_engine, &effects);
                        snprintf(status, sizeof status, "loaded %.100s  [%.40s]",
                                 GetFileName(path), song.song.voice_name);
                        status_color = BM_ACCENT;
                    } else {
                        snprintf(status, sizeof status, "%.150s", err);
                        status_color = BM_ALERT;
                    }
                }
            } else if (act == BM_SONG_ACT_SAVE) {
                char path[1024];
                int  dlg;
                char suggest[128];

                /* Suggest what it was opened or last written as, so saving a
                 * song twice does not mean retyping its name - falling back to
                 * the title only when there is no such name yet. */
                if (song_path[0] != '\0') {
                    snprintf(suggest, sizeof suggest, "%.110s",
                             GetFileName(song_path));
                } else {
                    snprintf(suggest, sizeof suggest, "%.100s.bmsong",
                             song.title[0] ? song.title : "untitled");
                }
                dlg = bm_save_dialog(GetWindowHandle(), "Save song", suggest,
                                     "BENCmouth song", "bmsong",
                                     path, sizeof path);
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "%s", suggest);
                    dlg = BM_DLG_OK;
                }

                if (dlg != BM_DLG_OK) {
                    snprintf(status, sizeof status, "save cancelled");
                    status_color = BM_DIM;
                } else {
                    /* Capture what is on screen now: the sliders edit `voice`,
                     * and the title has its own box. */
                    song.song.voice = voice;
                    song.song.effects = effects;
                    snprintf(song.song.title, sizeof song.song.title, "%s",
                             song.title);
                    if (bm_song_save(path, &song.song, song.score) == 0) {
                        snprintf(song_path, sizeof song_path, "%s", path);
                        snprintf(status, sizeof status, "wrote %.150s",
                                 GetFileName(path));
                        status_color = BM_ACCENT;
                    } else {
                        snprintf(status, sizeof status, "could not write %.150s",
                                 path);
                        status_color = BM_ALERT;
                    }
                }
            }
        } else {
            /* The playhead. Only while this tab's own song is what is
             * sounding, so that speaking from the text tab does not run a
             * cursor across a roll it has nothing to do with. */
            /* Straight off the transport rather than off the frame clock: it
             * is reading the position of the samples actually going out, so
             * the head sits where the sound is even if a frame is dropped. */
            int head = g_player.playing ? (int)bm_player_pos_ms(&g_player) : -1;
            /* The roll takes whatever height the window has spare.
             *
             * The other two tabs are a fixed amount of content and a taller
             * band would be empty; a piano roll is the one thing here that is
             * always short of room, and a bigger window should mean more of the
             * song rather than more background. BELOW_PANEL is what the layout
             * underneath comes to, measured rather than guessed - if a control
             * is added down there this number moves with it, and the status
             * line is what disappears if it does not. */
            float tall = (float)GetScreenHeight() - (float)y - BELOW_PANEL;
            int act;

            if (tall < (float)PANEL_H) tall = (float)PANEL_H;
            act = bm_roll_panel(&ui, &roll,
                                (Rectangle){ BM_PAD, y, W - 2 * BM_PAD, tall },
                                use_dict, &voice, head);
            y += tall - (float)PANEL_H;   /* what follows starts lower down */

            /* Whatever the roll wants said - what an undo did, mostly. Cleared
             * as it is taken, like every other one-shot message here. */
            if (roll.said[0] != '\0') {
                snprintf(status, sizeof status, "%.100s", roll.said);
                status_color = BM_DIM;
                roll.said[0] = '\0';
            }

            /* A note the roll wants said, because it has just been dragged to
             * a new pitch. Through the live engine rather than the rendered
             * score: it is one syllable, it has to start now, and the engine is
             * what plays something immediately.
             *
             * Not while the transport is running - you are already listening to
             * the song, and a note shouted over it would be answering a
             * question nobody asked. */
            if (roll.audition > 0) {
                int at = roll.audition - 1;
                roll.audition = 0;

                if (!g_player.playing && at < roll.song.roll.count) {
                    const bm_note *n = &roll.song.roll.note[at];
                    const char *sing = n->tie
                        ? bm_roll_tied_vowel(&roll.song.roll, at) : n->phon;
                    char one[BM_NOTE_PHON_MAX + 48];
                    char name[8];

                    if (sing != 0 && sing[0] != '\0') {
                        bm_roll_note_name(n->midi, name, sizeof name);
                        /* Short, and its own length rather than the note's: this
                         * is a pitch being checked, not the song being played,
                         * and a two-second note would still be sounding three
                         * drags later. */
                        snprintf(one, sizeof one, "[dur 260][note %s] %s",
                                 name, sing);
                        /* Stopped before the engine is rewritten. The audio
                         * thread is inside bm_read whenever it is speaking, and
                         * this happens on every lane the note crosses - far
                         * more often than SPEAK, which is the only other thing
                         * that re-queues a live engine. Clearing the flag first
                         * is what keeps the callback out of a sequence being
                         * rebuilt underneath it. */
                        g_speaking = 0;
                        bm_engine_set_voice(g_engine, &voice);
                        bm_engine_set_effects(g_engine, &effects);
                        bm_engine_reset(g_engine);
                        if (bm_speak_phonemes(g_engine, one, 0) == BM_OK) {
                            g_finished = 0;
                            g_speaking = 1;
                        }
                    }
                }
            }

            /* Dragging the playhead while it is running moves the sound with
             * it, which is what scrubbing means and what the whole rendered
             * buffer was for. */
            if (roll.head_moved) {
                roll.head_moved = 0;
                if (g_player.playing) bm_player_seek_ms(&g_player, roll.head_ms);
            }

            if (act == BM_ROLL_ACT_LOAD) {
                char path[1024];
                char start[1024];
                int  dlg;

                snprintf(start, sizeof start, "%ssongs", GetApplicationDirectory());
                if (!DirectoryExists(start)) {
                    snprintf(start, sizeof start, "%s../Resources/songs",
                             GetApplicationDirectory());
                }
                if (!DirectoryExists(start)) snprintf(start, sizeof start, "songs");
                if (!DirectoryExists(start)) snprintf(start, sizeof start, ".");

                dlg = bm_open_dialog(GetWindowHandle(), "Load song", start,
                                     "BENCmouth song", "bmsong",
                                     path, sizeof path);
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(status, sizeof status,
                             "no file dialog available - install zenity or kdialog");
                    status_color = BM_ALERT;
                } else if (dlg == BM_DLG_OK) {
                    char err[192];
                    static bm_song loaded;
                    static char    loaded_score[BM_SONG_SCORE_MAX];

                    if (bm_song_load(path, &loaded, loaded_score,
                                     sizeof loaded_score, err, sizeof err) != 0) {
                        snprintf(status, sizeof status, "%.150s", err);
                        status_color = BM_ALERT;
                    } else if (loaded.roll.count == 0) {
                        /* A song that was typed rather than drawn. Opening it
                         * here as an empty roll would look like the file having
                         * lost its music; it has not, and the SONG tab has it. */
                        snprintf(status, sizeof status,
                                 "%.90s has no notes in it - open it in the SONG tab",
                                 GetFileName(path));
                        status_color = BM_AMBER;
                    } else {
                        roll.song = loaded;
                        roll.song.voice.name = roll.song.voice_name;
                        snprintf(roll.title, sizeof roll.title, "%s",
                                 roll.song.title);
                        roll.tempo_applied = roll.song.tempo;
                        roll.selected = -1;
                        roll.scroll_ms = 0.0f;
                        roll.dirty = 1;
                        /* A different song is a different document, and undo
                         * should not walk back into the last one. */
                        bm_roll_history_init(&roll.hist);
                        snprintf(song_path, sizeof song_path, "%s", path);

                        voice = roll.song.voice;
                        effects = roll.song.effects;
                        tab_voice[2] = voice;
                        tab_fx[2] = effects;
                        bm_engine_set_voice(g_engine, &voice);
                        bm_engine_set_effects(g_engine, &effects);
                        bm_roll_ui_refresh(&roll, &voice);
                        snprintf(status, sizeof status,
                                 "loaded %.90s  -  %d notes", GetFileName(path),
                                 roll.song.roll.count);
                        status_color = BM_ACCENT;
                    }
                }
            } else if (act == BM_ROLL_ACT_SAVE) {
                char path[1024];
                char suggest[128];
                int  dlg;

                if (song_path[0] != '\0') {
                    snprintf(suggest, sizeof suggest, "%.110s",
                             GetFileName(song_path));
                } else {
                    snprintf(suggest, sizeof suggest, "%.100s.bmsong",
                             roll.title[0] ? roll.title : "untitled");
                }
                dlg = bm_save_dialog(GetWindowHandle(), "Save song", suggest,
                                     "BENCmouth song", "bmsong",
                                     path, sizeof path);
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "%s", suggest);
                    dlg = BM_DLG_OK;
                }

                if (dlg != BM_DLG_OK) {
                    snprintf(status, sizeof status, "save cancelled");
                    status_color = BM_DIM;
                } else {
                    roll.song.voice = voice;
                    roll.song.effects = effects;
                    snprintf(roll.song.title, sizeof roll.song.title, "%s",
                             roll.title);
                    /* The score written to the file is the one compiled from
                     * the notes, so the two halves of the file cannot disagree
                     * about what the song is. */
                    bm_roll_ui_refresh(&roll, &voice);
                    if (bm_song_save(path, &roll.song, roll.score) == 0) {
                        snprintf(song_path, sizeof song_path, "%s", path);
                        snprintf(status, sizeof status, "wrote %.150s",
                                 GetFileName(path));
                        status_color = BM_ACCENT;
                    } else {
                        snprintf(status, sizeof status, "could not write %.150s",
                                 path);
                        status_color = BM_ALERT;
                    }
                }
            }
        }

        /* Two popups at once would fight over which of them owns the mouse. */
        if (ui.menu_open) { voice_open = 0; fx_open = 0; }
        if (fx_open) voice_open = 0;
        y += PANEL_H + 8;

        /* ---- transport ---- */
        {
            Rectangle b = { BM_PAD, y, 96, 30 };
            /* Nothing to play through in editor mode: the plugin owns the
             * sound, and the host's transport is what starts it. A button that
             * looked live and did nothing would be worse than one that is
             * plainly not offered. */
            if (bm_button(&ui, b, tab == 0 ? "SPEAK" : "SING", g_audio)) {
              if (tab == 2) {
                /* The roll is a timeline, so it is rendered and then played
                 * rather than spoken: that is what lets SING start from the
                 * playhead instead of from the beginning, and what will let a
                 * host put the transport wherever it likes. Rendering happens
                 * here and not on every edit - measuring a score is cheap and
                 * has to be live, but running the DSP over it is neither. */
                bm_render *next = &g_render[g_render_slot ^ 1];
                bm_config  c2 = config;
                char       err[192];

                c2.voice = voice;
                c2.effects = effects;
                c2.use_dict = use_dict;

                bm_player_stop(&g_player);
                if (roll.dirty) bm_roll_ui_refresh(&roll, &voice);

                if (roll.score[0] == '\0') {
                    snprintf(status, sizeof status,
                             "nothing to sing - the notes have no words yet");
                    status_color = BM_AMBER;
                } else if (bm_render_score(next, roll.score, &c2,
                                           err, sizeof err) != 0) {
                    snprintf(status, sizeof status, "cannot sing that: %.130s",
                             err);
                    status_color = BM_ALERT;
                } else {
                    g_peak = 0.0f; g_limited = 0;
                    g_rms  = 0.0f; g_rms_sum = 0.0; g_rms_n = 0.0;
                    g_finished = 0;

                    bm_player_set_source(&g_player, next->pcm, next->len,
                                         next->rate);
                    g_player.loop = roll_loop;
                    bm_player_seek_ms(&g_player, (double)roll.head_ms);
                    bm_player_play(&g_player);
                    play_from = roll.head_ms;
                    g_render_slot ^= 1;

                    g_speaking = 1;
                    snprintf(status, sizeof status, "singing  -  %.2f s rendered",
                             bm_render_ms(next) / 1000.0);
                    status_color = BM_ACCENT;
                }
              } else {
                bm_result rc;

                /* Whatever the roll was playing gives way. The callback prefers
                 * the transport when it is running, so leaving it going would
                 * queue an utterance that was never heard - a SPEAK button that
                 * does nothing, for reasons on another tab. */
                bm_player_stop(&g_player);
                /* Whatever the roll was doing is over, and its head stays
                 * where it was: what finishes next is an utterance from
                 * another tab and has nothing to say about it. */
                play_from = -1.0f;

                bm_engine_set_voice(g_engine, &voice);
                bm_engine_set_effects(g_engine, &effects);
                /* A score goes in as phonemes. bm_speak_phonemes honours
                 * markup for free, which is the whole reason commands survive
                 * into the phoneme string instead of being resolved away in
                 * the front end. */
                rc = (tab == 0) ? bm_speak_text(g_engine, text, 0)
                                : bm_speak_phonemes(g_engine, sing_source(tab,
                                                    &song, &roll), 0);
                if (rc == BM_OK) {
                    g_peak = 0.0f; g_limited = 0;
                    g_rms  = 0.0f; g_rms_sum = 0.0; g_rms_n = 0.0;
                    g_finished = 0; g_speaking = 1;
                    snprintf(status, sizeof status,
                             tab == 0 ? "speaking" : "singing");
                    status_color = BM_ACCENT;
                } else {
                    /* A score can be wrong in ways text cannot - an unknown
                     * phoneme, an unterminated bracket - so say which. */
                    snprintf(status, sizeof status, "%s: %s",
                             tab == 0 ? "nothing to say" : "cannot sing that",
                             bm_strerror(rc));
                    status_color = BM_ALERT;
                }
              }
            }
            b.x += 104;
            if (bm_button(&ui, b, "STOP", g_audio && g_speaking)) {
                g_speaking = 0;
                bm_player_stop(&g_player);
                /* Stopped by hand leaves the head where it was stopped, which
                 * is the position somebody just chose to stop at. Only running
                 * out of song rewinds. */
                play_from = -1.0f;
                bm_engine_reset(g_engine);
                snprintf(status, sizeof status, "stopped");
                status_color = BM_DIM;
            }
            b.x += 104; b.width = 116;
            if (bm_button(&ui, b, "SAVE WAV", 1)) {
                char path[1024];
                char suggest[128];
                int  dlg;

                snprintf(suggest, sizeof suggest, "%.100s.wav",
                         (tab == 1 && song.title[0]) ? song.title :
                         (tab == 2 && roll.title[0]) ? roll.title : "bencmouth");
                dlg = bm_save_dialog(GetWindowHandle(), "Save WAV",
                                     suggest, "WAV audio", "wav",
                                     path, sizeof path);

                /* No dialog available - a bare X session with neither zenity
                 * nor kdialog. Writing to the working directory and saying so
                 * is better than refusing over a missing helper program. */
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "%s", suggest);
                    dlg = BM_DLG_OK;
                }

                if (dlg != BM_DLG_OK) {
                    snprintf(status, sizeof status, "save cancelled");
                    status_color = BM_DIM;
                } else {
                    /* Rendered rather than played: a second engine, so
                     * exporting does not interrupt what is sounding, and it
                     * renders whichever tab is in front. That is bm_render's
                     * whole job, and it used to be twenty lines of realloc
                     * loop here - the roll needed the same thing and two of
                     * them would have been two chances to get it wrong. */
                    static bm_render out;
                    bm_config c2 = config;
                    char err[192];
                    int ok;

                    c2.voice = voice;
                    c2.effects = effects;
                    c2.use_dict = use_dict;

                    if (tab == 0) {
                        /* The text tab is words, not a score, so it goes
                         * through the front end first. */
                        static char spoken[TEXT_CAP * 3];
                        size_t n = 0;
                        unsigned tf = BM_TEXT_MARKUP |
                                      (use_dict ? 0u : BM_TEXT_NO_DICT);

                        ok = (bm_text_to_phonemes_ex(text, 0, spoken,
                                                     sizeof spoken, &n, tf)
                                  == BM_OK) &&
                             bm_render_score(&out, spoken, &c2,
                                             err, sizeof err) == 0;
                    } else {
                        ok = bm_render_score(&out, sing_source(tab, &song, &roll),
                                             &c2, err, sizeof err) == 0;
                    }

                    if (ok && out.len > 0 &&
                        bm_wav_write(path, out.pcm, out.len, SAMPLE_RATE, 0) == 0) {
                        snprintf(status, sizeof status, "wrote %.120s  (%.2f s)",
                                 GetFileName(path), (double)out.len / SAMPLE_RATE);
                        status_color = BM_ACCENT;
                    } else if (!ok) {
                        snprintf(status, sizeof status, "cannot render: %.130s",
                                 err);
                        status_color = BM_ALERT;
                    } else {
                        /* An explicit precision: a path can be longer than the status
                         * line, and saying so beats letting snprintf decide. */
                        snprintf(status, sizeof status,
                                 "could not write %.150s", path);
                        status_color = BM_ALERT;
                    }
                }
            }

            /* LOOP is a transport control, so it lives on the transport row -
             * and only on the tab that has a transport to control. The other
             * two speak an utterance and stop, which is not something that can
             * be looped without inventing a gap nobody asked for. */
            if (tab == 2) {
                b.x += 124; b.width = 92;
                if (bm_toggle(&ui, b, "LOOP", &roll_loop, 1)) {
                    g_player.loop = roll_loop;
                }
            }
        }
        y += 42;
        bm_divider(BM_PAD, y, W - 2 * BM_PAD);
        y += 12;

        /* ---- voice ---- */
        {
            Rectangle b;
            bm_label(&ui, "VOICE", BM_PAD, y + 8);
            b = (Rectangle){ BM_PAD + 60, y, 220, 28 };
            if (bm_dropdown(&ui, b, voice_names, voice_count, &voice_index,
                            &voice_open, voice.name)) {
                const char *chain;

                voice = *voice_list[voice_index];
                bm_engine_set_voice(g_engine, &voice);
                snprintf(status, sizeof status, "voice: %s", voice.name);
                status_color = BM_DIM;

                /* Some presets are a voice *and* a chain, and picking one has
                 * to move the effects column too - Sentry without its ring
                 * modulator is a plain neutral tract, so a dropdown that
                 * changed only the left two columns would look broken. The
                 * ones that want no chain select None rather than leaving
                 * whatever the last voice brought, which would make a dry
                 * voice sound like whoever preceded it. */
                chain = bm_voice_chain(&voice);
                for (i = 0; i < fx_count; i++) {
                    if (strcmp(fx_names[i],
                               chain != 0 ? chain : "None") == 0) {
                        fx_index = i;
                        effects  = *bm_effects_preset_at(i);
                        bm_engine_set_effects(g_engine, &effects);
                        break;
                    }
                }
            }
            b = (Rectangle){ BM_PAD + 292, y, 76, 28 };
            if (bm_button(&ui, b, "LOAD", 1)) {
                char path[1024];
                char start[1024];
                int  dlg;

                /* Start in the voices folder that ships beside the binary,
                 * which is where the presets are and where SAVE's suggestions
                 * are most likely to be wanted. */
                /* Beside the binary, then inside a macOS bundle - where the
                 * executable sits in Contents/MacOS and anything shipped with
                 * it belongs in Contents/Resources - then the working
                 * directory, then give up and open wherever. */
                snprintf(start, sizeof start, "%svoices",
                         GetApplicationDirectory());
                if (!DirectoryExists(start)) {
                    snprintf(start, sizeof start, "%s../Resources/voices",
                             GetApplicationDirectory());
                }
                if (!DirectoryExists(start)) {
                    snprintf(start, sizeof start, "%s", "voices");
                }
                if (!DirectoryExists(start)) {
                    snprintf(start, sizeof start, ".");
                }

                dlg = bm_open_dialog(GetWindowHandle(), "Load voice", start,
                                     "BENCmouth voice", "bmvoice",
                                     path, sizeof path);

                if (dlg == BM_DLG_UNAVAILABLE) {
                    /* Unlike saving, there is no sensible default to fall back
                     * on: nothing here knows which file was wanted. */
                    snprintf(status, sizeof status,
                             "no file dialog available - install zenity or kdialog");
                    status_color = BM_ALERT;
                } else if (dlg == BM_DLG_OK) {
                    /* Loaded over a copy, so a file that fails halfway through
                     * cannot leave the live voice half-changed. Over the
                     * current voice rather than over defaults, which is what
                     * `bm -f` does - a file that sets only two keys is an edit,
                     * not a whole voice. */
                    bm_voice   next = voice;
                    bm_effects next_fx = effects;
                    char err[192];

                    if (bm_voicefile_load(path, &next, &next_fx, voice_name_buf,
                                          sizeof voice_name_buf,
                                          err, sizeof err) == 0) {
                        voice = next;
                        effects = next_fx;
                        bm_engine_set_effects(g_engine, &effects);
                        /* The file's own `name`, and failing that the file's
                         * name - which is nearly always what somebody meant by
                         * calling it that. "loaded" told you only that
                         * something had been. */
                        if (voice_name_buf[0] == '\0') {
                            snprintf(voice_name_buf, sizeof voice_name_buf,
                                     "%s", GetFileNameWithoutExt(path));
                        }
                        voice.name = voice_name_buf[0] != '\0'
                                         ? voice_name_buf : "loaded";
                        bm_engine_set_voice(g_engine, &voice);
                        /* A file naming a preset selects it in the dropdown;
                         * one that does not leaves the selection where it was,
                         * because there is nothing in the list to point at -
                         * but the control shows the loaded name either way,
                         * since it displays voice.name rather than the list. */
                        voice_index = name_index(voice_names, voice_count,
                                                 voice.name, voice_index);
                        fx_index    = name_index(fx_names, fx_count,
                                                 effects.name, fx_index);
                        snprintf(status, sizeof status, "loaded %.120s",
                                 GetFileName(path));
                        status_color = BM_ACCENT;
                    } else {
                        snprintf(status, sizeof status, "%.150s", err);
                        status_color = BM_ALERT;
                    }
                }
            }

            b.x += 84;
            if (bm_button(&ui, b, "SAVE", 1)) {
                char path[1024];
                int  dlg = bm_save_dialog(GetWindowHandle(), "Save voice",
                                          "bencmouth.bmvoice",
                                          "BENCmouth voice", "bmvoice",
                                          path, sizeof path);

                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "bencmouth.bmvoice");
                    dlg = BM_DLG_OK;
                }

                if (dlg != BM_DLG_OK) {
                    snprintf(status, sizeof status, "save cancelled");
                    status_color = BM_DIM;
                } else {
                    /* Saving names the voice after the file it is saved as.
                     * Before this, a voice that had been touched was called
                     * "edited": saving wrote `name = edited` into the file, and
                     * the dropdown went on showing whichever preset it had
                     * started from - so what was on screen, what was in the
                     * file, and what you had just named it were three different
                     * answers. Naming it after the file makes them one.
                     *
                     * Copied immediately because GetFileNameWithoutExt returns
                     * raylib's own scratch buffer, and voice.name has to
                     * outlive the next call to anything. */
                    snprintf(voice_name_buf, sizeof voice_name_buf, "%s",
                             GetFileNameWithoutExt(path));
                    if (voice_name_buf[0] != '\0') voice.name = voice_name_buf;

                    if (bm_voicefile_save(path, &voice, &effects) == 0) {
                        voice_index = name_index(voice_names, voice_count,
                                                 voice.name, voice_index);
                        snprintf(status, sizeof status,
                                 "wrote %.110s as \"%.60s\"",
                                 GetFileName(path), voice.name);
                        status_color = BM_ACCENT;
                    } else {
                        /* An explicit precision: a path can be longer than the
                         * status line, and saying so beats letting snprintf
                         * decide. */
                        snprintf(status, sizeof status,
                                 "could not write %.150s", path);
                        status_color = BM_ALERT;
                    }
                }
            }
            b.x += 84;
            if (bm_button(&ui, b, "RANDOM", 1)) {
                /* A different voice every press. GetRandomValue is raylib's,
                 * seeded from the clock, so this is genuinely a new draw. */
                unsigned seed = (unsigned)GetRandomValue(1, 1000000);
                bm_voice_random(&voice, seed);
                bm_engine_set_voice(g_engine, &voice);
                snprintf(status, sizeof status, "random voice, seed %u", seed);
                status_color = BM_ACCENT;
            }

            /* Greyed rather than hidden in a build without a dictionary: the
             * absence is the thing worth knowing, and a control that is simply
             * missing tells you nothing. */
            b.x += 84; b.width = 150;
            if (bm_toggle(&ui, b, have_dict ? (use_dict ? "DICT ON" : "DICT OFF")
                                            : "NO DICTIONARY",
                          &use_dict, have_dict)) {
                bm_engine_set_dictionary(g_engine, use_dict);
                dirty = 1;
                snprintf(status, sizeof status, "%s",
                         use_dict ? "dictionary on"
                                  : "dictionary off - letter-to-sound rules only");
                status_color = use_dict ? BM_DIM : BM_AMBER;
            }
        }
        y += 38;

        /* ---- parameters, in three columns ----
         *
         * Columns one and two are the speaker, column three is what is done to
         * them afterwards - the same line bm_voice and bm_effects are drawn
         * along. Nineteen voice parameters split ten and nine; the effects no
         * longer fit at all, so that column scrolls.
         *
         * Briefly four columns instead, which fitted everything at once and
         * cost 100 px of width and a 940 px minimum. Scrolling one column is
         * cheaper than that, and it is the arrangement that keeps working: the
         * effects list has grown five times and a layout that has to be redrawn
         * on each occasion is a layout that will be wrong again.
         *
         * The slider metrics are proportional, so the columns lay themselves
         * out without any of this knowing their width. */
        {
            float colw = (W - 4 * BM_PAD) / 3.0f;
            int   half = (NPARAMS + 1) / 2;
            float fx_x = BM_PAD + 2.0f * (colw + BM_PAD);
            /* One row goes to the dropdown, which does not scroll - it is the
             * heading, and a heading that scrolls off is a column you cannot
             * identify. */
            int   fx_visible = half - 1;

            for (i = 0; i < NPARAMS; i++) {
                int col = i / half;
                int rowi = i % half;
                Rectangle row = { BM_PAD + (float)col * (colw + BM_PAD),
                                  y + (float)rowi * 24, colw, 22 };
                float v = param_get(&voice, PARAMS[i].key);
                /* Ids from ID_VOICE_SLIDER up; see the block comment there for
                 * why sliders are numbered alongside the text boxes. */
                if (bm_slider(&ui, ID_VOICE_SLIDER + i, row, PARAMS[i].label, &v,
                              PARAMS[i].lo, PARAMS[i].hi, PARAMS[i].fmt)) {
                    bm_voice_set_param(&voice, PARAMS[i].key, 0, v);
                    /* Lands at the next frame boundary, so it will not click -
                     * and a mid-utterance change is audible right away, which
                     * is the whole point of tuning by ear. */
                    bm_engine_set_voice(g_engine, &voice);
                    voice.name = "edited";
                }
            }

            /* ---- the effects column ---- */
            {
                /* The dropdown is the heading. A separate heading plus a
                 * control somewhere else would cost a row this layout does not
                 * have, and "EFFECTS: <name>" is what a heading here would say
                 * anyway. */
                if (voice_open) fx_open = 0;
                bm_label(&ui, "FX", fx_x, y + 5);
                if (bm_dropdown(&ui, (Rectangle){ fx_x + 32, y - 1,
                                                  colw - 32, 24 },
                                fx_names, fx_count, &fx_index, &fx_open,
                                effects.name)) {
                    effects = *bm_effects_preset_at(fx_index);
                    bm_engine_set_effects(g_engine, &effects);
                    snprintf(status, sizeof status, "effects: %s", effects.name);
                    status_color = BM_DIM;
                }

                {
                    Rectangle band = { fx_x, y + 24.0f, colw,
                                       (float)fx_visible * 24.0f };
                    int k;

                    bm_scroll_rows(&ui, 1, band, NFXPARAMS, fx_visible,
                                   &fx_scroll);

                    for (k = 0; k < fx_visible; k++) {
                        int slot = fx_scroll + k;
                        Rectangle row;
                        float v;

                        if (slot >= NFXPARAMS) break;
                        /* 12 px of gutter for the bar, always - not only when
                         * it is showing. A column whose sliders change length
                         * depending on how many rows there are would twitch
                         * every time the list grew. */
                        row = (Rectangle){ fx_x, band.y + (float)k * 24,
                                           colw - 12.0f, 22 };
                        v = fx_get(&effects, FX_PARAMS[slot].key);
                        if (bm_slider(&ui, ID_FX_SLIDER + slot, row,
                                      FX_PARAMS[slot].label, &v,
                                      FX_PARAMS[slot].lo, FX_PARAMS[slot].hi,
                                      FX_PARAMS[slot].fmt)) {
                            bm_effects_set_param(&effects, FX_PARAMS[slot].key,
                                                 0, v);
                            bm_engine_set_effects(g_engine, &effects);
                            effects.name = "edited";
                        }
                    }
                }
            }
            y += (float)half * 24;
        }

        /* ---- scope and meter ---- */
        y += 6;
        bm_divider(BM_PAD, y, W - 2 * BM_PAD);
        y += 10;
        {
            int at = g_scope_at, k;
            for (k = 0; k < SCOPE_LEN; k++) {
                scope_copy[k] = g_scope[(at + k) % SCOPE_LEN];
            }
            /* The readouts sit under the meter rather than beside it: at
             * BM_PAD from the right edge, "peak 0.00" had nowhere to go and
             * was clipped. Widened from 172 when the RMS figure joined it -
             * the two belong on one line, because the whole point of showing
             * them is the distance between them. */
            float gx = W - BM_PAD - 212;
            bm_waveform((Rectangle){ BM_PAD, y, gx - BM_PAD - 12, 64 },
                        scope_copy, SCOPE_LEN);
            bm_meter((Rectangle){ gx, y + 4, 208, 16 }, g_peak, g_rms,
                     g_limited);
            {
                char pk[64];
                /* RMS in decibels and peak as a fraction, which is not an
                 * inconsistency: peak is read against the limiter at 0.85, so
                 * a linear number is the one that answers "how close am I",
                 * while loudness is only ever compared with another loudness
                 * and decibels are what that comparison is done in. */
                if (g_rms > 0.0f) {
                    snprintf(pk, sizeof pk, "peak %.2f  rms %.1f dB",
                             (double)g_peak,
                             20.0 * log10((double)g_rms));
                } else {
                    snprintf(pk, sizeof pk, "peak %.2f  rms   --", (double)g_peak);
                }
                bm_text(&ui, BM_FONT_SMALL, pk, gx, y + 26, BM_TEXT);
                snprintf(pk, sizeof pk, "%d Hz  mono", SAMPLE_RATE);
                bm_label(&ui, pk, gx, y + 46);
            }
        }
        y += 74;

        /* ---- back to the plugin ----
         *
         * The song is formatted every frame and compared with what was last
         * sent, rather than any edit being made to remember to announce itself.
         * One comparison catches a note moved, a slider turned, a tempo, a
         * title and a tie; a flag per edit site catches whichever ones somebody
         * remembered, which is a different list every month.
         *
         * Held back while the mouse is down and for a moment after. Publishing
         * costs the plugin a re-render - tens of milliseconds - and a drag that
         * published every frame would ask for sixty of them a second and stutter
         * the audio it was trying to edit. */
        if (editor_name != 0 && editor_shm.block != 0) {
            static char now_text[BM_SHM_TEXT];
            static char sent_text[BM_SHM_TEXT];
            long n;

            roll.song.voice = voice;
            roll.song.effects = effects;
            snprintf(roll.song.title, sizeof roll.song.title, "%s", roll.title);
            if (roll.dirty) bm_roll_ui_refresh(&roll, &voice);

            n = bm_song_format(&roll.song, roll.score, now_text, sizeof now_text);
            if (n > 0 && memcmp(now_text, sent_text, (size_t)n + 1) != 0) {
                if (!editor_dirty) editor_published = GetTime();
                editor_dirty = 1;
            }
            if (editor_dirty && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                GetTime() - editor_published > 0.15) {
                if (bm_shm_publish(&editor_shm.block->to_plugin, now_text,
                                   (size_t)n) == 0) {
                    memcpy(sent_text, now_text, (size_t)n + 1);
                    editor_dirty = 0;
                }
            }
        }

        bm_text(&ui, BM_FONT_SMALL, status, BM_PAD, y, status_color);

        /* ---- information ---- */
        if (info_open) {
            float H = (float)GetScreenHeight();
            float pw = W - 120.0f;
            float ph = H - 80.0f;
            Rectangle p;
            float ly, cy;

            if (pw > 760.0f) pw = 760.0f;
            if (ph > 620.0f) ph = 620.0f;
            p = (Rectangle){ (W - pw) * 0.5f, (H - ph) * 0.5f, pw, ph };

            DrawRectangle(0, 0, (int)W, (int)H, (Color){ 0, 0, 0, 200 });
            bm_panel(p);

            /* The dialog's own controls are live again - the block rectangle
             * above is there to stop the layout underneath, not this. */
            ui.blocking = 0;

            ly = p.y + 22.0f;
            if (logo.id != 0) {
                float lw = pw - 260.0f;
                float lh;
                if (lw > 300.0f) lw = 300.0f;
                lh = lw * (float)logo.height / (float)logo.width;
                DrawTexturePro(logo,
                               (Rectangle){ 0, 0, (float)logo.width,
                                            (float)logo.height },
                               (Rectangle){ p.x + (pw - lw) * 0.5f, ly, lw, lh },
                               (Vector2){ 0, 0 }, 0.0f, BM_TEXT);
                ly += lh + 18.0f;
            }

            {
                char line[128];
                float tw;

                snprintf(line, sizeof line, "BENCmouth %d.%d.%d",
                         BM_VERSION_MAJOR, BM_VERSION_MINOR, BM_VERSION_PATCH);
                tw = bm_text_measure(&ui, BM_FONT_BODY, line, 0.0f);
                bm_text(&ui, BM_FONT_BODY, line, p.x + (pw - tw) * 0.5f, ly,
                        BM_TEXT);
                ly += 26.0f;

                snprintf(line, sizeof line, "a formant speech synthesizer in C99");
                tw = bm_text_measure(&ui, BM_FONT_SMALL, line, 0.0f);
                bm_text(&ui, BM_FONT_SMALL, line, p.x + (pw - tw) * 0.5f, ly,
                        BM_DIM);
                ly += 24.0f;

                snprintf(line, sizeof line, "Copyright (c) 2026 Ben Ropple");
                tw = bm_text_measure(&ui, BM_FONT_SMALL, line, 0.0f);
                bm_text(&ui, BM_FONT_SMALL, line, p.x + (pw - tw) * 0.5f, ly,
                        BM_TEXT);
                ly += 26.0f;
            }

            bm_divider(p.x + 20.0f, ly, pw - 40.0f);
            ly += 10.0f;

            cy = p.y + ph - 46.0f;
            bm_textview(&ui, ID_ABOUT,
                        (Rectangle){ p.x + 20.0f, ly, pw - 40.0f,
                                     cy - ly - 10.0f },
                        about, &info_st, BM_DIM);

            if (bm_button(&ui, (Rectangle){ p.x + pw - 116.0f, cy, 96, 30 },
                          "CLOSE", 1) ||
                IsKeyPressed(KEY_ESCAPE)) {
                info_open = 0;
            }
        }

        if (song.ref_open) {
            bm_song_reference(&ui, &song, W, (float)GetScreenHeight());
        }
        if (roll.help_open) {
            bm_roll_help(&ui, &roll, W, (float)GetScreenHeight());
        }

        /* Last, so a dropdown list or a context menu is above the layout it
         * covers rather than under it. */
        bm_ui_overlay(&ui);

        EndDrawing();

        if (shot != 0 && --shot_frames <= 0) {
            char file[512];
            snprintf(file, sizeof file, "%.400s-%s.png", shot, TABS[tab]);
            TakeScreenshot(file);
            if (tab + 1 < TAB_COUNT) {
                tab++;
                prev_tab = tab;
                voice = tab_voice[tab];
                effects = tab_fx[tab];
                shot_frames = 4;
            } else {
                break;
            }
        }
    }

    g_speaking = 0;
    if (g_audio) {
        UnloadAudioStream(stream);
        CloseAudioDevice();
    }
    if (editor_name != 0) bm_shm_close(&editor_shm);
    if (logo.id != 0) UnloadTexture(logo);
    bm_ui_free(&ui);
    CloseWindow();
    return 0;
}
