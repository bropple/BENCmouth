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
#include "bm_song_ui.h"
#include "bm_songfile.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Taller than it was. Song mode needs a panel with two columns in it, and the
 * voice grew three sliders, which between them added about 80 px of layout
 * that has to come from somewhere. */
#define WIN_W       900
#define WIN_H       780
#define WIN_MIN_W   800
#define WIN_MIN_H   740

/* The band the active tab's panel gets. Fixed rather than proportional: the
 * controls below it are a fixed height each, so a proportional panel would
 * push the status line off the bottom of a short window. */
#define PANEL_H     210
#define TEXT_CAP    2048
#define SCOPE_LEN   2048
#define SAMPLE_RATE 22050

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
/* Latched by the audio thread when an utterance runs out, cleared by the UI
 * once it has been noticed. A flag rather than an edge: see audio_cb. */
static volatile int      g_finished;
static float             g_scope[SCOPE_LEN];
static volatile int      g_scope_at;
static volatile float    g_peak;
static volatile int      g_limited;

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

        if (g_speaking) {
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

            g_scope[g_scope_at] = s;
            g_scope_at = (g_scope_at + 1) % SCOPE_LEN;

            out[done + i] = (short)bm_pcm_sample(s, 0);
        }
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

/* The keys are the .voice file keys, so what the sliders write and what a
 * saved file contains cannot drift apart. */
static const param_row PARAMS[] = {
    { "f0_base",         "pitch",        60.0f, 260.0f, "%.0f Hz" },
    { "f0_range",        "range",         0.0f,  10.0f, "%.1f st" },
    { "f0_flutter",      "flutter",       0.0f,   1.0f, "%.2f"    },
    { "vibrato",         "vibrato",       0.0f,   3.0f, "%.2f st" },
    { "vibrato_rate",    "vib. rate",     0.0f,  12.0f, "%.1f Hz" },
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
    { "open_quotient",   "open quot.",    0.3f,   0.7f, "%.2f"    },
    { "whisper",         "whisper",       0.0f,   1.0f, "%.2f"    },
    { "gain",            "gain",          0.0f,   1.5f, "%.2f"    },
    { "coarticulation",  "coart.",        0.0f,   1.0f, "%.2f"    },
    { "prosody",         "prosody",       0.0f,   1.0f, "%.2f"    },
    { "formant_glide",   "glide",         0.0f,   1.0f, "%.2f"    },
    { "bandwidth_track", "bandwidth",     0.0f,   1.0f, "%.2f"    },
    { "flatten",         "flatten",       0.0f,   1.0f, "%.2f"    }
};
#define NPARAMS ((int)(sizeof PARAMS / sizeof PARAMS[0]))

/* The effects chain, in its own column. Same table shape, different struct -
 * and that separation is the point: a voice describes a speaker, an effect is
 * something done to the sound afterwards. See bm_effects in bencmouth.h. */
static const param_row FX_PARAMS[] = {
    { "ring",    "ring",       0.0f,   1.0f,  "%.2f"    },
    { "ring_hz", "ring freq",  0.0f, 400.0f,  "%.0f Hz" },
    { "comb",    "comb",       0.0f,   1.0f,  "%.2f"    },
    { "comb_hz", "comb freq", 40.0f, 900.0f,  "%.0f Hz" },
    { "drive",   "drive",      0.0f,   1.0f,  "%.2f"    },
    { "crush",   "crush",      0.0f,   1.0f,  "%.2f"    },
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

/* Alphabetical, case-insensitive, for the voice dropdown.
 *
 * The presets are declared in the order they were written - default, Retro, the
 * classic set, then whatever arrived last - which is a useful order to read the
 * source in and a useless one to hunt a name in once there are more than a
 * dozen. The list is sorted for display only; nothing else depends on preset
 * order, because everything else looks them up by name. */
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
    const char *fx_names[16];
    int   fx_count = 0, fx_index = 0, fx_open = 0;
    bm_effects effects;
    int   i, dirty = 1;
    int   have_dict = 0, use_dict = 1;
    int   info_open = 0;

    /* Two tabs, and each keeps its own voice. Song mode wants prosody off and
     * a little vibrato; speech wants the opposite, and one shared voice would
     * mean every trip through the song tab quietly retuned the text tab. The
     * sliders always edit whichever is in front. */
    static const char *TABS[] = { "TEXT", "SONG" };
    int        tab = 0;
    bm_voice   stashed_voice;
    bm_effects stashed_effects;
    bm_song_ui song;
    static char song_path[1024] = "";

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

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    {
        int w = WIN_W, h = WIN_H;
        parse_size(argc, argv, &w, &h);
        InitWindow(w, h, "BENCmouth");
    }
    shot = parse_shot(argc, argv);
    if (shot != 0) shot_frames = 8;   /* let the font and logo textures land */
    /* ESC closes the information window and nothing else. raylib exits on it
     * by default, which in a program built around a text field means one
     * stray keystroke throws away what you were typing. */
    SetExitKey(KEY_NULL);
    SetWindowMinSize(WIN_MIN_W, WIN_MIN_H);
    SetTargetFPS(60);
    InitAudioDevice();

    bm_ui_init(&ui);
    bm_song_ui_init(&song);
    stashed_voice = voice;
    stashed_effects = effects;
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
    stream = LoadAudioStream(SAMPLE_RATE, 16, 1);
    SetAudioStreamCallback(stream, audio_cb);
    PlayAudioStream(stream);

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

        /* The utterance ends on the audio thread, which has no way to say so
         * beyond this flag. Without it the status line went on reading
         * "speaking" over a silent engine - and a readout that is wrong about
         * the one thing it reports is worse than no readout at all. */
        /* Modal. Published before any widget runs, so nothing underneath the
         * scrim reacts to a click meant for the dialog. */
        if (info_open || song.ref_open) {
            ui.blocking = 1;
            ui.block = (Rectangle){ 0, 0, W, (float)GetScreenHeight() };
            voice_open = 0;
            ui.menu_open = 0;
        }
        /* Only one modal at a time, and the one just asked for wins. */
        if (info_open) song.ref_open = 0;

        if (g_finished) {
            g_finished = 0;
            snprintf(status, sizeof status, "ready");
            status_color = BM_DIM;
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
                    TABS, 2, &tab)) {
            /* Swap the voices over. The one in front is always `voice`, which
             * is the only thing the sliders and the engine ever see. */
            if (tab == 1) {
                stashed_voice = voice;
                stashed_effects = effects;
                voice = song.song.voice;
                effects = song.song.effects;
            } else {
                song.song.voice = voice;
                song.song.effects = effects;
                voice = stashed_voice;
                effects = stashed_effects;
            }
            bm_engine_set_voice(g_engine, &voice);
            bm_engine_set_effects(g_engine, &effects);
            ui.focus = 0;
            voice_open = 0;
        }
        y += 30;

        if (tab == 0) {
            /* ---- text and phonemes ---- */
            bm_label(&ui, "TEXT TO SPEAK", BM_PAD, y);
            if (bm_textbox(&ui, 1,
                           (Rectangle){ BM_PAD, y + 18, W - 2 * BM_PAD,
                                        PANEL_H - 18 - 66 },
                           text, TEXT_CAP, &text_st)) {
                dirty = 1;
            }
            bm_textview(&ui, (Rectangle){ BM_PAD, y + PANEL_H - 58,
                                          W - 2 * BM_PAD, 58 },
                        phonemes, &phon_st, BM_DIM);
        } else {
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
        }

        /* Two popups at once would fight over which of them owns the mouse. */
        if (ui.menu_open) { voice_open = 0; fx_open = 0; }
        if (fx_open) voice_open = 0;
        y += PANEL_H + 8;

        /* ---- transport ---- */
        {
            Rectangle b = { BM_PAD, y, 96, 30 };
            if (bm_button(&ui, b, tab == 0 ? "SPEAK" : "SING", 1)) {
                bm_result rc;

                bm_engine_set_voice(g_engine, &voice);
                bm_engine_set_effects(g_engine, &effects);
                /* A score goes in as phonemes. bm_speak_phonemes honours
                 * markup for free, which is the whole reason commands survive
                 * into the phoneme string instead of being resolved away in
                 * the front end. */
                rc = (tab == 0) ? bm_speak_text(g_engine, text, 0)
                                : bm_speak_phonemes(g_engine, song.score, 0);
                if (rc == BM_OK) {
                    g_peak = 0.0f; g_limited = 0;
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
            b.x += 104;
            if (bm_button(&ui, b, "STOP", g_speaking)) {
                g_speaking = 0;
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
                         (tab == 1 && song.title[0]) ? song.title : "bencmouth");
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
                    bm_engine_storage s2;
                    bm_engine *e2 = 0;
                    bm_config c2 = config;
                    float *pcm = 0;
                    size_t cap = 0, len = 0;

                    c2.voice = voice;
                    c2.effects = effects;
                    c2.use_dict = use_dict;
                    /* A second engine rather than the live one, so exporting
                     * does not interrupt playback - and it renders whichever
                     * tab is in front. */
                    if (bm_engine_init(&s2, &c2, &e2) == BM_OK &&
                        (tab == 0 ? bm_speak_text(e2, text, 0)
                                  : bm_speak_phonemes(e2, song.score, 0))
                            == BM_OK) {
                        while (bm_is_speaking(e2)) {
                            size_t got;
                            if (len + 4096 > cap) {
                                cap = cap ? cap * 2 : 65536;
                                pcm = (float *)realloc(pcm, cap * sizeof *pcm);
                                if (pcm == 0) break;
                            }
                            got = bm_read(e2, pcm + len, 4096);
                            if (got == 0) break;
                            len += got;
                        }
                    }
                    if (pcm != 0 && len > 0 &&
                        bm_wav_write(path, pcm, len, SAMPLE_RATE, 0) == 0) {
                        snprintf(status, sizeof status, "wrote %.120s  (%.2f s)",
                                 GetFileName(path), (double)len / SAMPLE_RATE);
                        status_color = BM_ACCENT;
                    } else {
                        /* An explicit precision: a path can be longer than the status
                         * line, and saying so beats letting snprintf decide. */
                        snprintf(status, sizeof status,
                                 "could not write %.150s", path);
                        status_color = BM_ALERT;
                    }
                    free(pcm);
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
                            &voice_open)) {
                voice = *voice_list[voice_index];
                bm_engine_set_voice(g_engine, &voice);
                snprintf(status, sizeof status, "voice: %s", voice.name);
                status_color = BM_DIM;
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
                                     "BENCmouth voice", "voice",
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
                        voice.name = voice_name_buf[0] != '\0'
                                         ? voice_name_buf : "loaded";
                        bm_engine_set_voice(g_engine, &voice);
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
                                          "bencmouth.voice",
                                          "BENCmouth voice", "voice",
                                          path, sizeof path);

                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "bencmouth.voice");
                    dlg = BM_DLG_OK;
                }

                if (dlg != BM_DLG_OK) {
                    snprintf(status, sizeof status, "save cancelled");
                    status_color = BM_DIM;
                } else if (bm_voicefile_save(path, &voice, &effects) == 0) {
                    snprintf(status, sizeof status, "wrote %.150s", GetFileName(path));
                    status_color = BM_ACCENT;
                } else {
                    /* An explicit precision: a path can be longer than the status
                         * line, and saying so beats letting snprintf decide. */
                        snprintf(status, sizeof status,
                                 "could not write %.150s", path);
                    status_color = BM_ALERT;
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
         * Seventeen voice parameters and seven effect parameters stacked in one
         * list would be 580 px of slider. Two columns fitted the voice but left
         * nowhere for the effects; three fits both in the same 216 px the voice
         * alone used to take, because the effects column is one shorter.
         *
         * The split is not arbitrary: columns one and two are the speaker,
         * column three is what is done to them afterwards, which is the same
         * line bm_voice and bm_effects are drawn along. */
        {
            float colw = (W - 4 * BM_PAD) / 3.0f;
            int   half = (NPARAMS + 1) / 2;

            for (i = 0; i < NPARAMS; i++) {
                int col = i / half;
                int rowi = i % half;
                Rectangle row = { BM_PAD + (float)col * (colw + BM_PAD),
                                  y + (float)rowi * 24, colw, 22 };
                float v = param_get(&voice, PARAMS[i].key);
                if (bm_slider(&ui, row, PARAMS[i].label, &v,
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
                float fx_x = BM_PAD + 2.0f * (colw + BM_PAD);

                /* The dropdown is the column heading. A separate heading plus a
                 * control somewhere else would cost a row this layout does not
                 * have, and "EFFECTS: <name>" is what a heading here would say
                 * anyway. */
                if (voice_open) fx_open = 0;
                bm_label(&ui, "EFFECTS", fx_x, y + 5);
                if (bm_dropdown(&ui, (Rectangle){ fx_x + 76, y - 1,
                                                  colw - 76, 24 },
                                fx_names, fx_count, &fx_index, &fx_open)) {
                    effects = *bm_effects_preset_at(fx_index);
                    bm_engine_set_effects(g_engine, &effects);
                    snprintf(status, sizeof status, "effects: %s", effects.name);
                    status_color = BM_DIM;
                }

                for (i = 0; i < NFXPARAMS; i++) {
                    Rectangle row = { fx_x, y + (float)(i + 1) * 24, colw, 22 };
                    float v = fx_get(&effects, FX_PARAMS[i].key);
                    if (bm_slider(&ui, row, FX_PARAMS[i].label, &v,
                                  FX_PARAMS[i].lo, FX_PARAMS[i].hi,
                                  FX_PARAMS[i].fmt)) {
                        bm_effects_set_param(&effects, FX_PARAMS[i].key, 0, v);
                        bm_engine_set_effects(g_engine, &effects);
                        effects.name = "edited";
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
             * was clipped. */
            float gx = W - BM_PAD - 172;
            bm_waveform((Rectangle){ BM_PAD, y, gx - BM_PAD - 12, 64 },
                        scope_copy, SCOPE_LEN);
            bm_meter((Rectangle){ gx, y + 4, 168, 16 }, g_peak, g_limited);
            {
                char pk[48];
                snprintf(pk, sizeof pk, "peak %.2f", (double)g_peak);
                bm_text(&ui, BM_FONT_SMALL, pk, gx, y + 26, BM_TEXT);
                snprintf(pk, sizeof pk, "%d Hz  mono", SAMPLE_RATE);
                bm_label(&ui, pk, gx, y + 46);
            }
        }
        y += 74;

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
            bm_textview(&ui, (Rectangle){ p.x + 20.0f, ly, pw - 40.0f,
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

        /* Last, so a dropdown list or a context menu is above the layout it
         * covers rather than under it. */
        bm_ui_overlay(&ui);

        EndDrawing();

        if (shot != 0 && --shot_frames <= 0) {
            char file[512];
            snprintf(file, sizeof file, "%.400s-%s.png", shot, TABS[tab]);
            TakeScreenshot(file);
            if (tab == 0) {
                tab = 1;
                voice = song.song.voice;
                shot_frames = 4;
            } else {
                break;
            }
        }
    }

    g_speaking = 0;
    UnloadAudioStream(stream);
    CloseAudioDevice();
    if (logo.id != 0) UnloadTexture(logo);
    bm_ui_free(&ui);
    CloseWindow();
    return 0;
}
