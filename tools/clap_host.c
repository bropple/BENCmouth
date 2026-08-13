/*
 * A CLAP host, small enough to read and real enough to prove something.
 *
 * `make clap-test` builds this, loads the plugin the way a DAW does and plays
 * it. That the plugin compiles says nothing about whether the transport lands
 * on the right sample, whether a project's state survives being saved and
 * reloaded, or whether it makes any sound at all - and those are exactly the
 * things that are tedious to discover inside a DAW, where every failure looks
 * the same from the outside.
 *
 * It is deliberately not a general host: it loads one plugin, drives one
 * output port, and checks what comes out.
 */

#include <clap/clap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  define OPEN(p)     ((void *)LoadLibraryA(p))
#  define SYM(h, n)   ((void *)GetProcAddress((HMODULE)(h), (n)))
#  define CLOSE(h)    FreeLibrary((HMODULE)(h))
#else
#  include <dlfcn.h>
#  define OPEN(p)     dlopen((p), RTLD_NOW | RTLD_LOCAL)
#  define SYM(h, n)   dlsym((h), (n))
#  define CLOSE(h)    dlclose(h)
#endif

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* ------------------------------------------------------------------ *
 * The host side of the conversation
 * ------------------------------------------------------------------ */

static int  g_callback_wanted = 0;

static const void *host_get_extension(const clap_host_t *h, const char *id)
{
    (void)h; (void)id;
    return 0;                       /* no log, no threadpool, no timers */
}
static void host_request_restart(const clap_host_t *h) { (void)h; }
static void host_request_process(const clap_host_t *h) { (void)h; }
static void host_request_callback(const clap_host_t *h)
{
    (void)h;
    g_callback_wanted = 1;
}

static clap_host_t HOST = {
    CLAP_VERSION_INIT,
    0,
    "clap_host", "BENCO", "", "0.1",
    host_get_extension,
    host_request_restart,
    host_request_process,
    host_request_callback
};

/* An empty event queue, which is what most blocks have. */
static uint32_t ev_size(const clap_input_events_t *l) { (void)l; return 0; }
static const clap_event_header_t *ev_get(const clap_input_events_t *l, uint32_t i)
{
    (void)l; (void)i;
    return 0;
}
static clap_input_events_t IN_EVENTS = { 0, ev_size, ev_get };

static bool out_try_push(const clap_output_events_t *l,
                         const clap_event_header_t *e)
{
    (void)l; (void)e;
    return true;
}
static clap_output_events_t OUT_EVENTS = { 0, out_try_push };

/* A stream over a fixed buffer, for the state round trip. */
typedef struct {
    char  *data;
    size_t len, cap, at;
} membuf;

static membuf g_state;

static int64_t st_write(const clap_ostream_t *s, const void *buf, uint64_t n)
{
    membuf *m = (membuf *)s->ctx;
    if (m->len + n > m->cap) n = m->cap - m->len;
    memcpy(m->data + m->len, buf, (size_t)n);
    m->len += (size_t)n;
    return (int64_t)n;
}

static int64_t st_read(const clap_istream_t *s, void *buf, uint64_t n)
{
    membuf *m = (membuf *)s->ctx;
    if (m->at + n > m->len) n = m->len - m->at;
    memcpy(buf, m->data + m->at, (size_t)n);
    m->at += (size_t)n;
    return (int64_t)n;
}

/* ------------------------------------------------------------------ */

#define RATE   48000.0
#define BLOCK  512

static float bufL[BLOCK], bufR[BLOCK];

/* Runs one block with the transport at `sec`, and reports the loudest sample.
 * `beats_only` drives the fallback path, for hosts that report musical time
 * and no seconds - the position is handed over as beats at 120 BPM instead. */
static float run_block_at(const clap_plugin_t *p, double sec, int playing,
                          int beats_only);

static float run_block(const clap_plugin_t *p, double sec, int playing)
{
    return run_block_at(p, sec, playing, 0);
}

static float run_block_at(const clap_plugin_t *p, double sec, int playing,
                          int beats_only)
{
    clap_audio_buffer_t out;
    clap_process_t      proc;
    clap_event_transport_t tr;
    float *chans[2];
    float  peak = 0.0f;
    uint32_t i;

    memset(&out, 0, sizeof out);
    memset(&proc, 0, sizeof proc);
    memset(&tr, 0, sizeof tr);
    memset(bufL, 0, sizeof bufL);
    memset(bufR, 0, sizeof bufR);

    chans[0] = bufL;
    chans[1] = bufR;
    out.data32 = chans;
    out.channel_count = 2;

    tr.header.size = sizeof tr;
    tr.header.type = CLAP_EVENT_TRANSPORT;
    tr.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    if (beats_only) {
        tr.flags = CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TEMPO |
                   (playing ? CLAP_TRANSPORT_IS_PLAYING : 0u);
        tr.tempo = 120.0;                       /* two beats a second */
        tr.song_pos_beats =
            (clap_beattime)(sec * 2.0 * (double)CLAP_BEATTIME_FACTOR);
    } else {
        tr.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                   (playing ? CLAP_TRANSPORT_IS_PLAYING : 0u);
        tr.song_pos_seconds = (clap_sectime)(sec * (double)CLAP_SECTIME_FACTOR);
    }

    proc.frames_count = BLOCK;
    proc.audio_outputs = &out;
    proc.audio_outputs_count = 1;
    proc.audio_inputs_count = 0;
    proc.in_events = &IN_EVENTS;
    proc.out_events = &OUT_EVENTS;
    proc.transport = &tr;

    if (p->process(p, &proc) == CLAP_PROCESS_ERROR) {
        check(0, "process returned an error");
        return 0.0f;
    }

    for (i = 0; i < BLOCK; i++) {
        float a = bufL[i] < 0.0f ? -bufL[i] : bufL[i];
        if (a > peak) peak = a;
    }
    return peak;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "build/BENCmouth.clap";
    void *lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *factory;
    const clap_plugin_descriptor_t *desc;
    const clap_plugin_t *p;

    printf("\nBENCmouth CLAP host tests\n\n  %s\n\n", path);

    lib = OPEN(path);
    if (lib == 0) {
#if !defined(_WIN32)
        printf("  cannot load: %s\n", dlerror());
#endif
        printf("\nFAILED (1 failure)\n\n");
        return 1;
    }

    entry = (const clap_plugin_entry_t *)SYM(lib, "clap_entry");
    check(entry != 0, "the bundle exports clap_entry");
    if (entry == 0) return 1;

    check(entry->init(path), "the entry initializes");

    factory = (const clap_plugin_factory_t *)
                  entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    check(factory != 0, "it offers a plugin factory");
    if (factory == 0) return 1;

    check(factory->get_plugin_count(factory) == 1, "with one plugin in it");
    desc = factory->get_plugin_descriptor(factory, 0);
    check(desc != 0 && desc->id != 0, "which describes itself");
    printf("    %s  %s  by %s\n", desc->id, desc->name, desc->vendor);

    p = factory->create_plugin(factory, &HOST, desc->id);
    check(p != 0, "and creates");
    if (p == 0) return 1;

    check(p->init(p), "it initializes");

    /* Extensions a host would ask for. */
    {
        const clap_plugin_audio_ports_t *ports = (const clap_plugin_audio_ports_t *)
            p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
        clap_audio_port_info_t info;

        check(ports != 0, "it has audio ports");
        check(ports->count(p, false) == 1 && ports->count(p, true) == 0,
              "one out, none in - it is an instrument");
        memset(&info, 0, sizeof info);
        check(ports->get(p, 0, false, &info) && info.channel_count == 2,
              "and the output is stereo");
    }

    check(p->activate(p, RATE, 1, BLOCK), "it activates at 48 kHz");
    check(p->start_processing(p), "and starts processing");

    /* ---- the transport ---- */
    printf("transport\n");
    {
        float at_start, just_after, at_middle, stopped, past_end;

        /* The very first block of an utterance is almost silent, and that is
         * the frame generator working: it starts from a vocal tract at rest and
         * glides out of it, so the sound arrives over about 40 ms rather than
         * beginning at full amplitude. Asserting it is *quiet* here rather than
         * loud is what makes the next check mean something - the plugin is at
         * the top of the song, not somewhere in the middle of it. */
        at_start = run_block(p, 0.0, 1);
        check(at_start < 0.01f, "at the very top of the song it is still quiet");
        printf("    peak %.6f at 0 s\n", (double)at_start);

        just_after = run_block(p, 0.05, 1);
        check(just_after > 0.01f, "fifty milliseconds in, the note has arrived");
        printf("    peak %.3f at 0.05 s\n", (double)just_after);

        at_middle = run_block(p, 1.2, 1);
        check(at_middle > 0.0001f, "and it is sounding in the middle of it");
        printf("    peak %.3f at 1.2 s\n", (double)at_middle);

        stopped = run_block(p, 1.2, 0);
        check(stopped == 0.0f, "a stopped transport is silent");

        /* The default song is three seconds long at most. */
        past_end = run_block(p, 60.0, 1);
        check(past_end == 0.0f, "and so is a position past the end");
        printf("    peak %.3f at 60 s\n", (double)past_end);
    }

    /* Located, not free-running: the same position twice gives the same audio.
     * A plugin that ignored the transport and just advanced would not. */
    printf("locating\n");
    {
        float first[BLOCK];
        float peak_a, peak_b;
        int   same = 1, i;

        peak_a = run_block(p, 0.8, 1);
        memcpy(first, bufL, sizeof first);
        peak_b = run_block(p, 0.8, 1);
        for (i = 0; i < BLOCK; i++) {
            if (first[i] != bufL[i]) { same = 0; break; }
        }
        check(peak_a > 0.0001f && peak_b > 0.0001f, "the same second sounds");
        check(same, "and gives the same samples both times");

        /* A host that reports musical time and no seconds still gets the right
         * part of the song, by way of the tempo. */
        {
            float beats = run_block_at(p, 0.8, 1, 1);
            int   agrees = 1;
            for (i = 0; i < BLOCK; i++) {
                if (first[i] != bufL[i]) { agrees = 0; break; }
            }
            check(beats > 0.0001f, "a beats-only transport finds the song too");
            check(agrees, "and lands on the same samples as the seconds one");
        }
    }

    /* ---- parameters ---- */
    printf("parameters\n");
    {
        const clap_plugin_params_t *params = (const clap_plugin_params_t *)
            p->get_extension(p, CLAP_EXT_PARAMS);
        clap_param_info_t info;
        double v = 0.0;

        check(params != 0, "it has parameters");
        check(params->count(p) >= 1, "at least one");
        check(params->get_info(p, 0, &info), "which describe themselves");
        printf("    %s: %.2f to %.2f, default %.2f\n", info.name,
               info.min_value, info.max_value, info.default_value);
        check(params->get_value(p, info.id, &v), "and have a value");
    }

    /* ---- state ---- */
    printf("state\n");
    {
        static char store[65536];
        clap_ostream_t o;
        clap_istream_t in;
        const clap_plugin_state_t *state = (const clap_plugin_state_t *)
            p->get_extension(p, CLAP_EXT_STATE);

        check(state != 0, "it saves state");

        g_state.data = store;
        g_state.cap = sizeof store;
        g_state.len = 0;
        g_state.at = 0;

        o.ctx = &g_state;
        o.write = st_write;
        check(state->save(p, &o), "saving works");
        check(g_state.len > 32, "and produces something");
        printf("    %lu bytes, beginning \"%.24s\"\n",
               (unsigned long)g_state.len, store);
        check(strstr(store, "note = ") != 0,
              "which is the song as text, notes and all");

        in.ctx = &g_state;
        in.read = st_read;
        check(state->load(p, &in), "and it loads back");

        /* Loading asks for a main-thread callback to re-render, exactly as it
         * would in a host. Without this the plugin would be silent until
         * something else woke it. */
        check(g_callback_wanted, "loading asks the host for a callback");
        p->on_main_thread(p);
        check(run_block(p, 0.5, 1) > 0.0001f,
              "and after it, the restored song plays");
    }

    /* ---- loading a song from a file ---- */
    printf("presets\n");
    {
        const clap_plugin_preset_load_t *pre =
            (const clap_plugin_preset_load_t *)
                p->get_extension(p, CLAP_EXT_PRESET_LOAD);
        const char *song = (argc > 2) ? argv[2] : 0;

        check(pre != 0, "a song can be loaded from a file");
        if (pre != 0 && song != 0) {
            check(pre->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                     song, 0),
                  "and the one given on the command line loads");
            check(run_block(p, 0.4, 1) > 0.0001f, "and then plays");

            check(!pre->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_FILE,
                                      "no-such-file.bmsong", 0),
                  "a file that is not there is refused rather than crashed on");
            check(!pre->from_location(p, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN,
                                      0, "whatever"),
                  "and so is a kind of location this plugin does not have");
        }
    }

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    CLOSE(lib);

    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
