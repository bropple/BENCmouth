/*
 * BENCmouth as a CLAP instrument.
 *
 * It is a score player, not a synthesizer a host plays notes on. That is not a
 * shortcut, it is what the instrument is: what BENCmouth sings is a syllable at
 * a pitch for a length, and MIDI has no channel to carry the syllable. A note
 * list and a lyric list matched up by order would come apart the moment either
 * was edited. So the plugin owns the song - the notes, the words and the voice
 * - the host owns the transport, and the two meet at a position in seconds.
 *
 * Which makes the audio side almost nothing. The score is rendered to samples
 * whenever it changes (bm_render.h) and the host's playhead indexes into what
 * came out (bm_player.h), so process() is a locate and a memcpy. Everything
 * that could stall - parsing, planning, running the DSP - happens on the main
 * thread, and the audio thread never allocates, never locks and never touches
 * the engine.
 *
 * The plugin is deliberately usable with no editor at all: it opens with a song
 * in it, follows the transport, and saves whatever it holds into the project.
 * The editor is the standalone binary in a second process, for the reason
 * BENCsynth's is - raylib keeps its window in one file-scope global, so a
 * process gets exactly one window however many instances a host loads.
 */

#include "bencmouth.h"
#include "bm_player.h"
#include "bm_render.h"
#include "bm_roll.h"
#include "bm_shm.h"
#include "bm_songfile.h"
#include "bm_spawn.h"

#include <clap/clap.h>
#include <clap/factory/preset-discovery.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BM_CLAP_ID "net.ropple.bencmouth"

/* From the public header, so the version a host displays and the version the
 * library reports are the same number written once. */
#define BM_STR2(x) #x
#define BM_STR(x)  BM_STR2(x)
#define BM_CLAP_VERSION BM_STR(BM_VERSION_MAJOR) "." BM_STR(BM_VERSION_MINOR) \
                        "." BM_STR(BM_VERSION_PATCH)

/* What the plugin opens with, so that dropping it on a track and pressing play
 * makes a sound. An empty instrument gives no way to tell "installed wrong"
 * from "working, and silent because it has nothing to sing". */
static const char DEFAULT_SONG[] =
    "# BENCmouth song\n"
    "title = Untitled\n"
    "voice = BENCmouth\n"
    "tempo = 120\n"
    "vibrato = 0.28\n"
    "vibrato_rate = 5.5\n"
    "prosody = 0\n"
    "\n"
    "note = C4 0 500 M IY1 ; me\n"
    "note = D4 500 500 M IY1 ; me\n"
    "note = E4 1000 500 M IY1 ; me\n"
    "note = F4 1500 500 M IY1 ; me\n"
    "note = G4 2000 1000 M IY1 ; me\n"
    "\n"
    "score =\n"
    "[dur 500][note C4] M IY1\n"
    "[dur 500][note D4] M IY1\n"
    "[dur 500][note E4] M IY1\n"
    "[dur 500][note F4] M IY1\n"
    "[dur 1000][note G4] M IY1\n";

/* ------------------------------------------------------------------ *
 * Parameters
 *
 * Two, and the shortness of the list is a consequence of the design rather
 * than a gap in it. The audio is rendered ahead of time, so anything that
 * changes how the voice sounds means rendering it again - tens of milliseconds,
 * on another thread, with the old audio still playing until it lands. That is
 * fine for an edit and no good at all for automation: a filter sweep made of
 * re-renders is a stutter.
 *
 * So the parameters here are the ones that can be applied to samples that
 * already exist. Gain is a multiply. Sync is a decision about where to read.
 * Everything else about the voice lives in the song, which is state, and is
 * edited where songs are edited.
 * ------------------------------------------------------------------ */

enum {
    PARAM_GAIN = 0,
    PARAM_SYNC,
    PARAM_COUNT
};

typedef struct bm_clap {
    clap_plugin_t         plugin;
    const clap_host_t    *host;

    double   sample_rate;
    int      active;

    /* The song, and the score it compiles to. Main thread only. */
    bm_song  song;
    char     score[BM_SONG_SCORE_MAX];

    /* Two renders, used alternately: the audio thread may be inside a memcpy
     * from one when the other is being written, and bm_render_score
     * reallocates. The buffer a callback could still be reading stays alive
     * until the render after next. */
    bm_render render[2];
    int       slot;

    bm_player player;

    /* Set by anything that invalidates the audio, read on the main thread. The
     * host is asked for a callback rather than being polled. */
    volatile int need_render;

    /* Live, and read by the audio thread every block. */
    volatile double gain;
    volatile int    sync;

    /* The editor, when there is one: a second process, a block they share, and
     * the sequence of the last song taken from it. */
    bm_shm    shm;
    char      shm_name[BM_SHM_NAME_MAX];
    bm_spawn  spawn;
    uint32_t  editor_seq;
    int       editor_open;

    /* Why there is no sound, when there is no sound. Reported through the
     * host's log if it has one, because "silent" is the same symptom as a
     * dozen different mistakes. */
    char note[192];
} bm_clap;

/* Defined with the rest of the editor, below: destroy has to be reachable from
 * plugin teardown and from the editor going away on its own. */
static void gui_destroy(const clap_plugin_t *p);
static void pump_editor(bm_clap *s);
static const clap_plugin_gui_t *gui_extension(void);

static bm_clap *self_of(const clap_plugin_t *p)
{
    return (bm_clap *)p->plugin_data;
}

static void say(bm_clap *s, const char *what)
{
    const clap_host_log_t *log;

    snprintf(s->note, sizeof s->note, "%s", what);
    if (s->host == 0 || s->host->get_extension == 0) return;

    log = (const clap_host_log_t *)s->host->get_extension(s->host, CLAP_EXT_LOG);
    if (log != 0 && log->log != 0) log->log(s->host, CLAP_LOG_INFO, what);
}

/* ------------------------------------------------------------------ *
 * The song, and turning it into samples
 * ------------------------------------------------------------------ */

/* Main thread. Compiles the roll if there is one, renders, and publishes the
 * result to the player. */
static void rebuild(bm_clap *s)
{
    bm_render *next = &s->render[s->slot ^ 1];
    bm_config  cfg;
    char       err[192];

    s->need_render = 0;

    /* A drawn song carries its notes and its score, and they agree - but the
     * notes are what an editor changed, so they are what the score is made
     * from. A typed song has no notes and keeps the score it came with. */
    if (s->song.roll.count > 0) {
        if (bm_roll_compile(&s->song.roll, s->score, sizeof s->score) < 0) {
            say(s, "the song has more notes than the score buffer holds");
            return;
        }
    }

    if (s->score[0] == '\0') {
        bm_player_set_source(&s->player, 0, 0, (uint32_t)s->sample_rate);
        say(s, "nothing to sing: the song has no notes with words in them");
        return;
    }

    bm_config_default(&cfg);
    cfg.sample_rate = (uint32_t)s->sample_rate;
    cfg.voice   = s->song.voice;
    cfg.effects = s->song.effects;

    if (bm_render_score(next, s->score, &cfg, err, sizeof err) != 0) {
        bm_player_set_source(&s->player, 0, 0, (uint32_t)s->sample_rate);
        say(s, err);
        return;
    }

    bm_player_set_source(&s->player, next->pcm, next->len, next->rate);
    s->slot ^= 1;
    s->note[0] = '\0';
}

static void ask_for_rebuild(bm_clap *s)
{
    s->need_render = 1;
    if (s->host != 0 && s->host->request_callback != 0) {
        s->host->request_callback(s->host);
    }
}

/* ------------------------------------------------------------------ *
 * Audio ports
 * ------------------------------------------------------------------ */

static uint32_t ap_count(const clap_plugin_t *p, bool is_input)
{
    (void)p;
    return is_input ? 0u : 1u;
}

static bool ap_get(const clap_plugin_t *p, uint32_t index, bool is_input,
                   clap_audio_port_info_t *info)
{
    (void)p;
    if (is_input || index != 0) return false;

    info->id = 0;
    snprintf(info->name, sizeof info->name, "%s", "out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    /* Stereo, carrying one voice on both sides. BENCmouth is mono and saying
     * so with a mono port would be more honest about the signal - but a host
     * that puts a mono instrument on a stereo track is left to decide what to
     * do about it, and they do not agree. Two identical channels is the answer
     * every host handles the same way. */
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t EXT_AUDIO_PORTS = { ap_count, ap_get };

/* ------------------------------------------------------------------ *
 * Parameters
 * ------------------------------------------------------------------ */

static uint32_t pa_count(const clap_plugin_t *p)
{
    (void)p;
    return PARAM_COUNT;
}

static bool pa_get_info(const clap_plugin_t *p, uint32_t index,
                        clap_param_info_t *info)
{
    (void)p;
    if (index >= PARAM_COUNT) return false;

    memset(info, 0, sizeof *info);
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->cookie = 0;

    switch (index) {
    case PARAM_GAIN:
        snprintf(info->name, sizeof info->name, "%s", "Gain");
        snprintf(info->module, sizeof info->module, "%s", "Output");
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 1.0;
        break;
    case PARAM_SYNC:
    default:
        snprintf(info->name, sizeof info->name, "%s", "Follow transport");
        snprintf(info->module, sizeof info->module, "%s", "Transport");
        info->flags |= CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 1.0;
        break;
    }
    return true;
}

static bool pa_get_value(const clap_plugin_t *p, clap_id id, double *out)
{
    bm_clap *s = self_of(p);

    switch (id) {
    case PARAM_GAIN: *out = s->gain; return true;
    case PARAM_SYNC: *out = s->sync ? 1.0 : 0.0; return true;
    default: return false;
    }
}

static bool pa_value_to_text(const clap_plugin_t *p, clap_id id, double value,
                             char *out, uint32_t cap)
{
    (void)p;
    switch (id) {
    case PARAM_GAIN:
        snprintf(out, cap, "%.2f", value);
        return true;
    case PARAM_SYNC:
        snprintf(out, cap, "%s", value >= 0.5 ? "follow" : "free");
        return true;
    default:
        return false;
    }
}

static bool pa_text_to_value(const clap_plugin_t *p, clap_id id,
                             const char *text, double *out)
{
    (void)p;
    if (text == 0) return false;

    if (id == PARAM_SYNC) {
        *out = (text[0] == 'f' && text[1] == 'o') ? 1.0 : 0.0;
        return true;
    }
    if (id == PARAM_GAIN) {
        *out = atof(text);
        return true;
    }
    return false;
}

static void apply_param(bm_clap *s, clap_id id, double value)
{
    switch (id) {
    case PARAM_GAIN: s->gain = value; break;
    case PARAM_SYNC: s->sync = (value >= 0.5) ? 1 : 0; break;
    default: break;
    }
}

/* Events that arrive when the plugin is not processing - a host restoring
 * automation, or moving a knob while stopped. */
static void pa_flush(const clap_plugin_t *p, const clap_input_events_t *in,
                     const clap_output_events_t *out)
{
    bm_clap *s = self_of(p);
    uint32_t i, n;

    (void)out;
    if (in == 0) return;

    n = in->size(in);
    for (i = 0; i < n; i++) {
        const clap_event_header_t *h = in->get(in, i);
        if (h != 0 && h->space_id == CLAP_CORE_EVENT_SPACE_ID &&
            h->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t *e =
                (const clap_event_param_value_t *)h;
            apply_param(s, e->param_id, e->value);
        }
    }
}

static const clap_plugin_params_t EXT_PARAMS = {
    pa_count, pa_get_info, pa_get_value, pa_value_to_text, pa_text_to_value,
    pa_flush
};

/* ------------------------------------------------------------------ *
 * State
 *
 * The whole song, as the text of a .bmsong. Not a private binary blob: a
 * project file then contains something a person can read, and the same bytes
 * open in the editor and in `bm -S`. It also means the format only has to be
 * got right once.
 * ------------------------------------------------------------------ */

static bool st_save(const clap_plugin_t *p, const clap_ostream_t *stream)
{
    bm_clap *s = self_of(p);
    static char text[BM_SONG_FILE_MAX];
    long  n;
    int64_t at = 0;

    if (stream == 0) return false;

    n = bm_song_format(&s->song, s->score, text, sizeof text);
    if (n < 0) return false;

    while (at < n) {
        int64_t w = stream->write(stream, text + at, (uint64_t)(n - at));
        /* Zero is not an error and not progress either - a host may be asking
         * to be called again later. Anything negative is a real failure. */
        if (w < 0) return false;
        if (w == 0) break;
        at += w;
    }
    return at == n;
}

static bool st_load(const clap_plugin_t *p, const clap_istream_t *stream)
{
    bm_clap *s = self_of(p);
    static char text[BM_SONG_FILE_MAX];
    static bm_song loaded;
    static char    loaded_score[BM_SONG_SCORE_MAX];
    char    err[192];
    int64_t at = 0;

    if (stream == 0) return false;

    for (;;) {
        int64_t got = stream->read(stream, text + at,
                                   (uint64_t)((int64_t)sizeof text - 1 - at));
        if (got < 0) return false;
        if (got == 0) break;
        at += got;
        if (at >= (int64_t)sizeof text - 1) break;
    }
    text[at] = '\0';

    /* Into a scratch song, so a state blob that fails halfway through cannot
     * leave the plugin holding half of one song and half of another. */
    if (bm_song_parse(text, (size_t)at, &loaded, loaded_score,
                      sizeof loaded_score, err, sizeof err) != 0) {
        say(s, err);
        return false;
    }

    s->song = loaded;
    /* bm_song refers to its own name buffer, so the copy above left the voice
     * pointing into the scratch one. */
    s->song.voice.name = s->song.voice_name;
    memcpy(s->score, loaded_score, sizeof s->score);

    ask_for_rebuild(s);
    return true;
}

static const clap_plugin_state_t EXT_STATE = { st_save, st_load };

/* ------------------------------------------------------------------ *
 * Loading a song from a file
 *
 * The one way to get a song into the plugin that does not need the editor. A
 * .bmsong is already a file a host can point at, and CLAP has an extension for
 * exactly that - so this costs a few lines and means a plugin with no window
 * yet is still something you can put your own music into.
 *
 * Whether it can be reached depends on the host: it is the same extension a
 * preset browser uses, and not every host offers one.
 * ------------------------------------------------------------------ */

static bool pre_from_location(const clap_plugin_t *p, uint32_t location_kind,
                              const char *location, const char *load_key)
{
    bm_clap *s = self_of(p);
    static bm_song loaded;
    static char    loaded_score[BM_SONG_SCORE_MAX];
    char err[192];

    (void)load_key;

    if (location_kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || location == 0) {
        say(s, "only a .bmsong file can be loaded here");
        return false;
    }

    if (bm_song_load(location, &loaded, loaded_score, sizeof loaded_score,
                     err, sizeof err) != 0) {
        say(s, err);
        return false;
    }

    s->song = loaded;
    s->song.voice.name = s->song.voice_name;
    memcpy(s->score, loaded_score, sizeof s->score);

    /* From the main thread, so this can render at once rather than asking to
     * be called back - the host is waiting to know whether it worked. */
    rebuild(s);
    return true;
}

static const clap_plugin_preset_load_t EXT_PRESET_LOAD = { pre_from_location };

/* ------------------------------------------------------------------ *
 * The plugin
 * ------------------------------------------------------------------ */

static bool pl_init(const clap_plugin_t *p)
{
    (void)p;
    return true;
}

static void pl_destroy(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);

    gui_destroy(p);
    bm_render_free(&s->render[0]);
    bm_render_free(&s->render[1]);
    free(s);
}

static bool pl_activate(const clap_plugin_t *p, double sample_rate,
                        uint32_t min_frames, uint32_t max_frames)
{
    bm_clap *s = self_of(p);

    (void)min_frames;
    (void)max_frames;

    /* The score has to be rendered at the host's rate, so a rate change is a
     * re-render. Done here rather than deferred: activate is allowed to take
     * its time, and being ready before the first block beats being silent for
     * the first one. */
    s->sample_rate = sample_rate;
    s->active = 1;
    rebuild(s);
    return true;
}

static void pl_deactivate(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);
    s->active = 0;
    bm_player_stop(&s->player);
}

static bool pl_start_processing(const clap_plugin_t *p) { (void)p; return true; }
static void pl_stop_processing(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);
    bm_player_stop(&s->player);
}

static void pl_reset(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);
    bm_player_stop(&s->player);
    bm_player_locate(&s->player, 0);
}

static clap_process_status pl_process(const clap_plugin_t *p,
                                      const clap_process_t *proc)
{
    bm_clap *s = self_of(p);
    float   *left, *right;
    uint32_t frames, i;
    size_t   real = 0;
    double   gain;
    int      playing = 0;

    if (proc == 0 || proc->audio_outputs_count < 1) return CLAP_PROCESS_ERROR;

    left  = proc->audio_outputs[0].data32[0];
    right = (proc->audio_outputs[0].channel_count > 1)
                ? proc->audio_outputs[0].data32[1] : 0;
    if (left == 0) return CLAP_PROCESS_ERROR;

    frames = proc->frames_count;

    /* Parameters first, so a value that arrives with this block applies to it.
     * Sample-accurate automation inside a block is not offered: the whole
     * signal is a memcpy from a rendered buffer, and splitting the block to
     * apply a gain ramp a sample earlier would be precision nobody asked for
     * on a plugin whose sound is decided before the block begins. */
    pa_flush(p, proc->in_events, proc->out_events);
    gain = s->gain;

    /* Where the host is. Seconds rather than beats: the score is written in
     * milliseconds and knows nothing about tempo, so asking the transport for
     * musical time would mean converting through a tempo the song does not
     * have. A song follows the timeline, not the metronome. */
    if (proc->transport != 0) {
        playing = (proc->transport->flags & CLAP_TRANSPORT_IS_PLAYING) ? 1 : 0;

        if (s->sync) {
            double sec = -1.0;

            if (proc->transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) {
                sec = (double)proc->transport->song_pos_seconds /
                      (double)CLAP_SECTIME_FACTOR;
            } else if ((proc->transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) &&
                       (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) &&
                       proc->transport->tempo > 0.0) {
                /* Some hosts report only musical time. Converting through the
                 * current tempo is exact while that tempo holds, and wrong
                 * across a tempo change earlier in the timeline - it has no way
                 * to know there was one. Hosts that offer seconds are believed
                 * instead, which is all of the ones that have a tempo map. */
                double beats = (double)proc->transport->song_pos_beats /
                               (double)CLAP_BEATTIME_FACTOR;
                sec = beats * 60.0 / proc->transport->tempo;
            }

            if (sec >= 0.0) {
                /* Located every block, so the plugin cannot drift from the host
                 * and a jump - a loop, a scrub, a click in the ruler - is
                 * followed exactly rather than caught up with. */
                bm_player_locate(&s->player,
                                 (size_t)(sec * s->sample_rate + 0.5));
            }
        }
    } else {
        /* No transport at all: a host that just wants sound, or a test. Play
         * from wherever the last block left off. */
        playing = 1;
    }

    if (playing) {
        s->player.playing = 1;
        real = bm_player_read(&s->player, left, frames);
    } else {
        s->player.playing = 0;
        memset(left, 0, frames * sizeof *left);
    }

    if (gain != 1.0) {
        for (i = 0; i < frames; i++) left[i] *= (float)gain;
    }
    if (right != 0) memcpy(right, left, frames * sizeof *left);

    /* Silence is still a signal here: the plugin has a whole song in it and is
     * between notes, so telling the host it is done would let it stop calling
     * process and leave the rest of the song unplayed. */
    (void)real;
    return CLAP_PROCESS_CONTINUE;
}

static void pl_on_main_thread(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);

    if (s->need_render) rebuild(s);
    pump_editor(s);

    /* An open editor keeps asking. on_main_thread only happens because
     * something requested it, so without this the plugin would pump the editor
     * exactly once - at the moment the window opened - and never notice
     * another edit. The first version did precisely that, and the editor test
     * caught it by finding a sample rate that had never been written. */
    if (s->editor_open && s->host != 0 && s->host->request_callback != 0) {
        s->host->request_callback(s->host);
    }
}

static const void *pl_get_extension(const clap_plugin_t *p, const char *id)
{
    (void)p;
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &EXT_AUDIO_PORTS;
    if (strcmp(id, CLAP_EXT_PARAMS)      == 0) return &EXT_PARAMS;
    if (strcmp(id, CLAP_EXT_STATE)       == 0) return &EXT_STATE;
    if (strcmp(id, CLAP_EXT_PRESET_LOAD) == 0) return &EXT_PRESET_LOAD;
    if (strcmp(id, CLAP_EXT_GUI)         == 0) return gui_extension();
    return 0;
}

/* ------------------------------------------------------------------ *
 * The editor
 *
 * Floating, and only floating. CLAP is the one format that supports a
 * plugin-created top-level window outright, and that is exactly what raylib
 * needs: it owns its window and its GL context, and there is no supported way
 * to make it draw into a parent handed over by a host.
 *
 * The window belongs to a second process for a reason that cannot be worked
 * around in this one - raylib keeps its window in a file-scope global, so a
 * process gets one however many instances a host loads. See bm_shm.h.
 * ------------------------------------------------------------------ */

static unsigned g_editor_counter;

static bool gui_is_api_supported(const clap_plugin_t *p, const char *api,
                                 bool is_floating)
{
    (void)p; (void)api;
    /* True regardless of the api string: a floating window is ours to make, so
     * the host's windowing API has nothing to do with it. */
    return is_floating;
}

static bool gui_get_preferred_api(const clap_plugin_t *p, const char **api,
                                  bool *is_floating)
{
    (void)p;
#if defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#elif defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
#else
    *api = CLAP_WINDOW_API_X11;
#endif
    *is_floating = true;
    return true;
}

static bool gui_create(const clap_plugin_t *p, const char *api, bool is_floating)
{
    bm_clap *s = self_of(p);
    char     err[256];
    static char text[BM_SONG_FILE_MAX];
    long     n;

    (void)api;
    if (!is_floating) return false;         /* embedded is not offered */
    if (s->editor_open) return true;

    bm_shm_name(s->shm_name, sizeof s->shm_name, g_editor_counter++);
    if (bm_shm_create(&s->shm, s->shm_name) != 0) {
        say(s, "could not make a shared block for the editor");
        return false;
    }

    /* The song goes in before the process starts, so the editor has something
     * to open with rather than a window that fills in a moment later. */
    n = bm_song_format(&s->song, s->score, text, sizeof text);
    if (n > 0) bm_shm_publish(&s->shm.block->to_editor, text, (size_t)n);

    if (bm_spawn_editor(&s->spawn, s->shm_name, err, sizeof err) != 0) {
        bm_shm_close(&s->shm);
        say(s, err);
        return false;
    }

    s->editor_open = 1;
    /* Starts the chain: every callback asks for the next one for as long as
     * the window is open. */
    if (s->host != 0 && s->host->request_callback != 0) {
        s->host->request_callback(s->host);
    }
    return true;
}

static void gui_destroy(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);

    if (!s->editor_open) return;

    /* Asked, not killed: the editor closes its own window and lets go of the
     * block. Killed only if it will not, which is what bm_spawn_reap's timeout
     * is for. */
    if (s->shm.block != 0) s->shm.block->quit = 1u;
    bm_spawn_reap(&s->spawn);
    if (bm_spawn_alive(&s->spawn)) bm_spawn_kill(&s->spawn);
    bm_shm_close(&s->shm);
    s->editor_open = 0;
}

static bool gui_set_scale(const clap_plugin_t *p, double scale)
{
    (void)p; (void)scale;
    return false;
}

static bool gui_get_size(const clap_plugin_t *p, uint32_t *w, uint32_t *h)
{
    (void)p;
    /* What the standalone opens at. A floating window sizes itself, so this is
     * a hint for hosts that ask before they have one. */
    *w = 900;
    *h = 814;
    return true;
}

static bool gui_can_resize(const clap_plugin_t *p) { (void)p; return true; }

static bool gui_set_parent(const clap_plugin_t *p, const clap_window_t *w)
{
    (void)p; (void)w;
    return false;                            /* floating only */
}

static bool gui_set_transient(const clap_plugin_t *p, const clap_window_t *w)
{
    (void)p; (void)w;
    return false;
}

static void gui_suggest_title(const clap_plugin_t *p, const char *title)
{
    (void)p; (void)title;
}

static bool gui_show(const clap_plugin_t *p)
{
    bm_clap *s = self_of(p);
    return s->editor_open != 0;
}

static bool gui_hide(const clap_plugin_t *p)
{
    (void)p;
    /* There is no hiding a window in another process without a channel for
     * saying so, and a host that wants it gone calls destroy. Saying false
     * here is honest; pretending would leave a window on screen that the host
     * believes is not. */
    return false;
}

static const clap_plugin_gui_t EXT_GUI = {
    gui_is_api_supported,
    gui_get_preferred_api,
    gui_create,
    gui_destroy,
    gui_set_scale,
    gui_get_size,
    gui_can_resize,
    0,                       /* get_resize_hints: it is not embedded */
    0,                       /* adjust_size */
    0,                       /* set_size */
    gui_set_parent,
    gui_set_transient,
    gui_suggest_title,
    gui_show,
    gui_hide
};

static const clap_plugin_gui_t *gui_extension(void) { return &EXT_GUI; }

/* Once a frame's worth of housekeeping, on the main thread: take whatever the
 * editor has published, and tell it where the transport is. */
static void pump_editor(bm_clap *s)
{
    static char text[BM_SONG_FILE_MAX];
    static bm_song loaded;
    static char    loaded_score[BM_SONG_SCORE_MAX];
    size_t n = 0;
    char   err[192];

    if (!s->editor_open || s->shm.block == 0) return;

    s->shm.block->pos_ms = bm_player_pos_ms(&s->player);
    s->shm.block->playing = (uint32_t)(s->player.playing ? 1 : 0);
    s->shm.block->sample_rate = (uint32_t)s->sample_rate;

    if (bm_shm_take(&s->shm.block->to_plugin, &s->editor_seq,
                    text, sizeof text, &n) && n > 0) {
        if (bm_song_parse(text, n, &loaded, loaded_score,
                          sizeof loaded_score, err, sizeof err) == 0) {
            s->song = loaded;
            s->song.voice.name = s->song.voice_name;
            memcpy(s->score, loaded_score, sizeof s->score);
            rebuild(s);
        } else {
            say(s, err);
        }
    }

    /* An editor that has gone - closed by the user, or crashed - leaves the
     * plugin holding the last song it sent, which is the right outcome: the
     * music does not disappear because a window did. */
    if (!bm_spawn_alive(&s->spawn)) gui_destroy(&s->plugin);
}

/* ------------------------------------------------------------------ *
 * The factory
 * ------------------------------------------------------------------ */

static const char *FEATURES[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    "vocal",
    0
};

static const clap_plugin_descriptor_t DESCRIPTOR = {
    CLAP_VERSION_INIT,
    BM_CLAP_ID,
    "BENCmouth",
    "BENCO",
    "https://github.com/bropple/BENCmouth",
    "", "",
    BM_CLAP_VERSION,
    "A formant speech synthesizer that sings a score.",
    FEATURES
};

static const clap_plugin_t *make_plugin(const clap_host_t *host)
{
    bm_clap *s = (bm_clap *)calloc(1, sizeof *s);
    static char score_scratch[BM_SONG_SCORE_MAX];
    char err[192];

    if (s == 0) return 0;

    s->host = host;
    s->sample_rate = 44100.0;
    s->gain = 1.0;
    s->sync = 1;
    bm_player_init(&s->player);

    /* Opens with a song in it - see DEFAULT_SONG. A state load replaces this
     * before a sample is produced, so a project always gets its own. */
    if (bm_song_parse(DEFAULT_SONG, sizeof DEFAULT_SONG - 1, &s->song,
                      score_scratch, sizeof score_scratch,
                      err, sizeof err) == 0) {
        memcpy(s->score, score_scratch, sizeof s->score);
    } else {
        bm_song_init(&s->song);
        s->score[0] = '\0';
    }

    s->plugin.desc = &DESCRIPTOR;
    s->plugin.plugin_data = s;
    s->plugin.init = pl_init;
    s->plugin.destroy = pl_destroy;
    s->plugin.activate = pl_activate;
    s->plugin.deactivate = pl_deactivate;
    s->plugin.start_processing = pl_start_processing;
    s->plugin.stop_processing = pl_stop_processing;
    s->plugin.reset = pl_reset;
    s->plugin.process = pl_process;
    s->plugin.get_extension = pl_get_extension;
    s->plugin.on_main_thread = pl_on_main_thread;

    return &s->plugin;
}

static uint32_t fac_count(const clap_plugin_factory_t *f)
{
    (void)f;
    return 1;
}

static const clap_plugin_descriptor_t *fac_get(const clap_plugin_factory_t *f,
                                               uint32_t index)
{
    (void)f;
    return (index == 0) ? &DESCRIPTOR : 0;
}

static const clap_plugin_t *fac_create(const clap_plugin_factory_t *f,
                                       const clap_host_t *host, const char *id)
{
    (void)f;
    if (host == 0 || id == 0 || strcmp(id, DESCRIPTOR.id) != 0) return 0;
    return make_plugin(host);
}

static const clap_plugin_factory_t FACTORY = { fac_count, fac_get, fac_create };

static bool entry_init(const char *path)
{
    /* Where the .clap is, which is where the search for an editor beside it
     * starts. The host is the only thing that knows this. */
    bm_spawn_set_origin(path);
    return true;
}
static void entry_deinit(void) {}

static const void *entry_get_factory(const char *id)
{
    if (id != 0 && strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) return &FACTORY;
    return 0;
}

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entry_init,
    entry_deinit,
    entry_get_factory
};
