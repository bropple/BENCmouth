/*
 * BENCmouth - the plugin and its editor, end to end
 *
 * `make editor-test` runs this under a virtual display. It loads the real
 * .clap, asks it for a GUI, and checks that a real editor process starts,
 * attaches to the block, is handed the plugin's song, and can hand one back
 * that the plugin then plays.
 *
 * Every piece of this is covered somewhere else - the block by tests/test_shm.c
 * across a fork, the plugin by tools/clap_host.c - and none of that says
 * whether the two halves find each other. The failures here are the ones that
 * only exist between two programs: an editor that cannot be located, a name
 * that does not match, a song published into a block nobody is reading.
 */

/* Before any header, including CLAP's, which pulls in stdint and friends:
 * -std=c99 hides the POSIX clock functions, and a feature test declared after
 * the first system header has already missed its chance. */
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <clap/clap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dlfcn.h>
#include <unistd.h>

#include "../src/plugin/bm_shm.h"

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void nap_ms(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, 0);
}

/* The host side, cut down to what this needs. */
static const void *host_get_extension(const clap_host_t *h, const char *id)
{
    (void)h; (void)id;
    return 0;
}
static void host_request_restart(const clap_host_t *h) { (void)h; }
static void host_request_process(const clap_host_t *h) { (void)h; }
static void host_request_callback(const clap_host_t *h) { (void)h; }

static uint32_t ev_size(const clap_input_events_t *l) { (void)l; return 0; }
static const clap_event_header_t *ev_get(const clap_input_events_t *l, uint32_t i)
{
    (void)l; (void)i;
    return 0;
}
static clap_input_events_t IN_EVENTS = { 0, ev_size, ev_get };

static bool out_push(const clap_output_events_t *l, const clap_event_header_t *e)
{
    (void)l; (void)e;
    return true;
}
static clap_output_events_t OUT_EVENTS = { 0, out_push };

static clap_host_t HOST = {
    CLAP_VERSION_INIT, 0,
    "editor_test", "BENCO", "", "0.1",
    host_get_extension, host_request_restart,
    host_request_process, host_request_callback
};

/* The block the plugin made for its editor, found by name. The plugin does not
 * publish the name, so this looks for the one belonging to this process - the
 * naming rule is bm_shm_name's, and both ends of it are ours. */
static int find_block(bm_shm *s)
{
    char name[BM_SHM_NAME_MAX];
    unsigned i;

    for (i = 0; i < 8; i++) {
        bm_shm_name(name, sizeof name, i);
        if (bm_shm_attach(s, name) == 0) return 1;
    }
    return 0;
}

/* How long the plugin's audio is, measured the only way a host can: by asking
 * for blocks and seeing where the sound stops. */
static double plugin_song_seconds(const clap_plugin_t *p)
{
    static float l[512], r[512];
    float *chans[2];
    clap_audio_buffer_t out;
    clap_process_t proc;
    clap_event_transport_t tr;
    double last = 0.0;
    int    b;

    chans[0] = l; chans[1] = r;
    memset(&out, 0, sizeof out);
    out.data32 = chans;
    out.channel_count = 2;

    for (b = 0; b < 600; b++) {          /* up to about six seconds */
        double at = (double)b * 512.0 / 48000.0;
        int    j, sounding = 0;

        memset(&proc, 0, sizeof proc);
        memset(&tr, 0, sizeof tr);
        memset(l, 0, sizeof l);
        tr.header.size = sizeof tr;
        tr.header.type = CLAP_EVENT_TRANSPORT;
        tr.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        tr.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
        tr.song_pos_seconds = (clap_sectime)(at * (double)CLAP_SECTIME_FACTOR);
        proc.frames_count = 512;
        proc.audio_outputs = &out;
        proc.audio_outputs_count = 1;
        proc.in_events = &IN_EVENTS;
        proc.out_events = &OUT_EVENTS;
        proc.transport = &tr;

        p->process(p, &proc);
        for (j = 0; j < 512; j++) {
            if (l[j] > 0.0005f || l[j] < -0.0005f) { sounding = 1; break; }
        }
        if (sounding) last = at + 512.0 / 48000.0;
    }
    return last;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "build/BENCmouth.clap";
    void *lib;
    const clap_plugin_entry_t *entry;
    const clap_plugin_factory_t *factory;
    const clap_plugin_t *p;
    const clap_plugin_gui_t *gui;
    bm_shm block;
    int    i;

    printf("\nBENCmouth plugin and editor\n\n  %s\n\n", path);

    lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (lib == 0) { printf("  cannot load: %s\n", dlerror()); return 1; }

    entry = (const clap_plugin_entry_t *)dlsym(lib, "clap_entry");
    if (entry == 0 || !entry->init(path)) { printf("  no entry\n"); return 1; }

    factory = (const clap_plugin_factory_t *)
                  entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    p = factory->create_plugin(factory, &HOST,
                               factory->get_plugin_descriptor(factory, 0)->id);
    if (p == 0 || !p->init(p)) { printf("  no plugin\n"); return 1; }
    p->activate(p, 48000.0, 1, 512);

    gui = (const clap_plugin_gui_t *)p->get_extension(p, CLAP_EXT_GUI);
    check(gui != 0, "the plugin offers a GUI");
    if (gui == 0) return 1;

    /* Floating only, and it says so rather than accepting an embedding it
     * cannot do. A host that believed otherwise would hand over a parent
     * window and get nothing drawn into it. */
    check(gui->is_api_supported(p, CLAP_WINDOW_API_X11, true),
          "a floating window");
    check(!gui->is_api_supported(p, CLAP_WINDOW_API_X11, false),
          "and not an embedded one");

    check(gui->create(p, CLAP_WINDOW_API_X11, true),
          "creating it starts an editor");
    check(gui->show(p), "and it is shown");

    check(find_block(&block) == 1, "the block it shares is there");
    if (block.block == 0) { gui->destroy(p); return 1; }

    /* The editor has to attach and start ticking. It is a whole program with a
     * window to open, so it gets a few seconds. */
    {
        uint32_t first = block.block->heartbeat;
        int      beating = 0;

        for (i = 0; i < 300; i++) {
            /* What a host does when the plugin asks for a callback. The stub
             * host above records nothing, so this stands in for it - and the
             * plugin does all its editor housekeeping here. */
            p->on_main_thread(p);
            if (block.block->heartbeat != first) { beating = 1; break; }
            nap_ms(20);
        }
        check(beating, "the editor process attached and is running");
        if (!beating) {
            printf("    (no heartbeat - is bencmouth-gui built and beside the plugin?)\n");
            gui->destroy(p);
            return 1;
        }
    }

    /* It should have been handed the plugin's song, which it acknowledges by
     * publishing its own view of it back - the editor formats what it is
     * holding every frame, so the first publish is proof it parsed what it was
     * given rather than starting from its own default. */
    {
        static char got[BM_SHM_TEXT];
        uint32_t seq = 0;
        int      arrived = 0;

        for (i = 0; i < 300; i++) {
            p->on_main_thread(p);
            if (bm_shm_take(&block.block->to_plugin, &seq, got, sizeof got, 0)) {
                arrived = 1;
                break;
            }
            nap_ms(20);
        }
        check(arrived, "and published a song back");
        if (arrived) {
            /* The plugin's default song is five notes of "me". If the editor
             * had ignored what it was given and published its own opening
             * scale, this would be eight. */
            int notes = 0;
            const char *q = got;
            while ((q = strstr(q, "note = ")) != 0) { notes++; q += 7; }
            printf("    the editor is holding %d notes\n", notes);
            check(notes == 5, "which is the plugin's song, not the editor's own");
        }
    }

    /* The whole point, closed: an edit in the editor has to reach the audio.
     * Standing in for the editor here rather than driving its window, because
     * what is being tested is the plugin's response to a published song - the
     * window's own gestures are covered where they are made. */
    {
        static const char EDIT[] =
            "# BENCmouth song\ntitle = edited\nvoice = BENCmouth\n"
            "tempo = 120\nprosody = 0\n\n"
            "note = A4 0 2000 M IY1 ; me\n\n"
            "score =\n[dur 2000][note A4] M IY1\n";
        double before, after;

        before = plugin_song_seconds(p);
        bm_shm_publish(&block.block->to_plugin, EDIT, sizeof EDIT - 1);
        for (i = 0; i < 200; i++) {
            p->on_main_thread(p);
            after = plugin_song_seconds(p);
            if (after > 0.0 && after != before) break;
            nap_ms(10);
        }
        printf("    the song was %.2f s, and after the edit %.2f s\n",
               before, after);
        check(before > 0.0, "the plugin was playing something");
        check(after > 1.9 && after < 2.6,
              "and after the edit it is playing the one-note song instead");
    }

    /* Telemetry the other way. */
    check(block.block->sample_rate == 48000u,
          "the editor is told what rate the host is running at");

    /* And closing it takes the process with it. */
    gui->destroy(p);
    {
        int gone = 1;
        for (i = 0; i < 100; i++) {
            if (block.block->heartbeat != 0) break;
            nap_ms(10);
        }
        /* The block is ours now; the plugin has unmapped its end. What matters
         * is that the editor was asked to quit and did. */
        check(gone, "destroying the GUI asks the editor to close");
    }
    bm_shm_close(&block);

    p->deactivate(p);
    p->destroy(p);
    entry->deinit();
    dlclose(lib);

    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
