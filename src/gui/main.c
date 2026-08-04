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
#include "bm_filedlg.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W       900
#define WIN_H       700
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
    { "speed",           "speed",         0.5f,   2.0f, "%.2f"    },
    { "throat",          "throat",        0.7f,   1.3f, "%.2f"    },
    { "mouth",           "mouth",         0.7f,   1.3f, "%.2f"    },
    { "tilt",            "tilt",          0.0f,  14.0f, "%.1f dB" },
    { "breathiness",     "breath",        0.0f,  12.0f, "%.1f dB" },
    { "open_quotient",   "open quot.",    0.3f,   0.7f, "%.2f"    },
    { "gain",            "gain",          0.0f,   1.5f, "%.2f"    },
    { "coarticulation",  "coart.",        0.0f,   1.0f, "%.2f"    },
    { "prosody",         "prosody",       0.0f,   1.0f, "%.2f"    },
    { "formant_glide",   "glide",         0.0f,   1.0f, "%.2f"    },
    { "bandwidth_track", "bandwidth",     0.0f,   1.0f, "%.2f"    }
};
#define NPARAMS ((int)(sizeof PARAMS / sizeof PARAMS[0]))

static float param_get(const bm_voice *v, const char *key)
{
    /* The voice is a flat block of floats after the name pointer, in the same
     * order as bm_voice_set_param accepts. Reading it back positionally keeps
     * this table the only place the order is written down. */
    static const char *ORDER[] = {
        "f0_base", "f0_range", "f0_flutter", "speed", "throat", "mouth",
        "breathiness", "tilt", "open_quotient", "gain",
        "coarticulation", "prosody", "formant_glide", "bandwidth_track"
    };
    const float *f = (const float *)(const void *)&v->f0_base;
    int i;
    for (i = 0; i < (int)(sizeof ORDER / sizeof ORDER[0]); i++) {
        if (strcmp(ORDER[i], key) == 0) return f[i];
    }
    return 0.0f;
}

/* Optional "WxH" on the command line. Useful for a cramped desktop, and it is
 * what produced the screenshot in the README at a sensible resolution. */
static void parse_size(int argc, char **argv, int *w, int *h)
{
    int i, a, b;
    for (i = 1; i < argc; i++) {
        if (sscanf(argv[i], "%dx%d", &a, &b) == 2 && a >= 800 && b >= 680 &&
            a <= 8192 && b <= 8192) {
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

    const char *voice_names[16];
    int   voice_count = 0, voice_index = 0, voice_open = 0;
    int   i, dirty = 1;
    int   have_dict = 0, use_dict = 1;
    bm_edit text_st, phon_st;
    Color status_color;

    (void)scope;

    bm_config_default(&config);
    config.sample_rate = SAMPLE_RATE;
    have_dict = bm_dict_count() > 0;
    use_dict  = have_dict;
    config.markup = 1;          /* the GUI is a place to experiment */
    voice = config.voice;

    for (i = 0; i < bm_voice_preset_count() && i < 16; i++) {
        voice_names[voice_count++] = bm_voice_preset_at(i)->name;
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    {
        int w = WIN_W, h = WIN_H;
        parse_size(argc, argv, &w, &h);
        InitWindow(w, h, "BENCmouth");
    }
    SetWindowMinSize(800, 680);
    SetTargetFPS(60);
    InitAudioDevice();

    bm_ui_init(&ui);
    memset(&text_st, 0, sizeof text_st);
    memset(&phon_st, 0, sizeof phon_st);
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
     * icon has to be loaded from disk - and from more than one place, because
     * the program is as likely to be run from a menu as from its own
     * directory. */
#if !defined(_WIN32)
    {
        char beside[1024];
        const char *candidates[3];
        size_t k;

        /* Beside the executable first. Someone who unpacks the archive and
         * launches it from a file manager gets whatever working directory the
         * file manager felt like, which is rarely the one holding the assets. */
        snprintf(beside, sizeof beside, "%sassets/icon/hex-64.png",
                 GetApplicationDirectory());
        candidates[0] = beside;
        candidates[1] = "assets/icon/hex-64.png";
        candidates[2] = "/usr/share/bencmouth/hex-64.png";

        for (k = 0; k < sizeof candidates / sizeof candidates[0]; k++) {
            if (FileExists(candidates[k])) {
                Image icon = LoadImage(candidates[k]);
                if (icon.data != 0) { SetWindowIcon(icon); UnloadImage(icon); }
                break;
            }
        }
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
        }
        bm_divider(BM_PAD, 58, W - 2 * BM_PAD);

        /* ---- text and phonemes ---- */
        y = 72;
        bm_label(&ui, "TEXT TO SPEAK", BM_PAD, y);
        y += 18;
        if (bm_textbox(&ui, 1, (Rectangle){ BM_PAD, y, W - 2 * BM_PAD, 114 },
                       text, TEXT_CAP, &text_st)) {
            dirty = 1;
        }
        /* Two popups at once would fight over which of them owns the mouse. */
        if (ui.menu_open) voice_open = 0;
        y += 122;
        bm_textview(&ui, (Rectangle){ BM_PAD, y, W - 2 * BM_PAD, 58 },
                    phonemes, &phon_st, BM_DIM);
        y += 66;

        /* ---- transport ---- */
        {
            Rectangle b = { BM_PAD, y, 96, 30 };
            if (bm_button(&ui, b, "SPEAK", 1)) {
                bm_engine_set_voice(g_engine, &voice);
                if (bm_speak_text(g_engine, text, 0) == BM_OK) {
                    g_peak = 0.0f; g_limited = 0;
                    g_finished = 0; g_speaking = 1;
                    snprintf(status, sizeof status, "speaking");
                    status_color = BM_ACCENT;
                } else {
                    snprintf(status, sizeof status, "nothing to say");
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
                int  dlg = bm_save_dialog(GetWindowHandle(), "Save WAV",
                                          "bencmouth.wav", "WAV audio", "wav",
                                          path, sizeof path);

                /* No dialog available - a bare X session with neither zenity
                 * nor kdialog. Writing to the working directory and saying so
                 * is better than refusing over a missing helper program. */
                if (dlg == BM_DLG_UNAVAILABLE) {
                    snprintf(path, sizeof path, "bencmouth.wav");
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
                    c2.use_dict = use_dict;
                    if (bm_engine_init(&s2, &c2, &e2) == BM_OK &&
                        bm_speak_text(e2, text, 0) == BM_OK) {
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
                voice = *bm_voice_preset_at(voice_index);
                bm_engine_set_voice(g_engine, &voice);
                snprintf(status, sizeof status, "voice: %s", voice.name);
                status_color = BM_DIM;
            }
            b = (Rectangle){ BM_PAD + 292, y, 76, 28 };
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
                } else if (bm_voicefile_save(path, &voice) == 0) {
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

        /* ---- parameters, in two columns ----
         * Fourteen stacked would be 300 px of slider and would push the scope
         * off the bottom of any sensible window. */
        {
            float colw = (W - 3 * BM_PAD) * 0.5f;
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

        /* Last, so a dropdown list or a context menu is above the layout it
         * covers rather than under it. */
        bm_ui_overlay(&ui);

        EndDrawing();
    }

    g_speaking = 0;
    UnloadAudioStream(stream);
    CloseAudioDevice();
    bm_ui_free(&ui);
    CloseWindow();
    return 0;
}
