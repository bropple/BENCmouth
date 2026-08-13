/*
 * BENCmouth - the block the plugin and its editor share
 *
 * The editor is a second process. It has to be: raylib keeps its window, GL
 * context, input and timing in one file-scope global, so a process gets exactly
 * one window however many plugin instances a host loads. Threading cannot fix a
 * singleton; a process boundary gives each instance its own by construction.
 * It is also why a crashing editor does not take the DAW with it.
 *
 * What crosses is deliberately small. A song is text - it is a .bmsong either
 * way - so there is one thing to serialize and it is the thing the file format
 * already describes. No note structs cross the boundary, no versioned binary
 * layout, and nothing that has to be kept in agreement between two builds:
 * if the editor and the plugin were compiled a month apart, the worst that can
 * happen is a header key one of them does not know, which the parser already
 * has an answer for.
 *
 *   to_plugin   the editor's song, whenever it changes
 *   to_editor   the plugin's song, so the editor opens on the project's music
 *   telemetry   where the transport is, so the editor can draw a playhead
 *
 * Every channel is a seqlock: the writer bumps the sequence odd, writes, and
 * bumps it even. A reader that sees an odd sequence, or a different one either
 * side of the copy, tries again next frame. There is no lock anywhere near the
 * audio thread, so a hung or killed editor can never block it - the worst it
 * can do is stop publishing.
 */

#ifndef BM_SHM_H
#define BM_SHM_H

#include <stddef.h>
#include <stdint.h>

/* A song is a .bmsong, and BM_SONG_FILE_MAX is what one can be. Spelled out
 * rather than included from the host layer so that this file depends on
 * nothing: it is mapped by two programs that share no other code. */
#define BM_SHM_TEXT   65536
#define BM_SHM_MAGIC  0x424D5348u      /* "BMSH" */
#define BM_SHM_ABI    1u

/* Enough for "bencmouth-<pid>-<n>" and then some. */
#define BM_SHM_NAME_MAX 64

typedef struct bm_shm_channel {
    volatile uint32_t seq;             /* odd while being written */
    volatile uint32_t len;
    char              text[BM_SHM_TEXT];
} bm_shm_channel;

typedef struct bm_shm_block {
    volatile uint32_t magic;
    volatile uint32_t abi;

    /* The editor bumps this every frame it is alive. The plugin watches it
     * rather than the process handle: a wedged editor still has a process. */
    volatile uint32_t heartbeat;

    /* The plugin asks the editor to close - when the window is hidden, or the
     * plugin is being destroyed. Asked rather than killed, so the editor can
     * put its own window away tidily. */
    volatile uint32_t quit;

    bm_shm_channel to_plugin;
    bm_shm_channel to_editor;

    /* Plugin to editor, and read once a frame for a playhead. Plain volatile
     * words rather than a seqlock: each is one value, and one frame stale is
     * invisible in a cursor. */
    volatile double   pos_ms;
    volatile uint32_t playing;
    volatile uint32_t sample_rate;
} bm_shm_block;

typedef struct bm_shm {
    bm_shm_block *block;
    char          name[BM_SHM_NAME_MAX];
    int           owner;               /* created it, so unlinks it */
    void         *handle;              /* Windows needs one; POSIX does not */
    int           fd;
} bm_shm;

/* Creates and zeroes the block. The plugin does this and picks the name. */
int  bm_shm_create(bm_shm *s, const char *name);

/* Attaches to one somebody else created. The editor does this. */
int  bm_shm_attach(bm_shm *s, const char *name);

/* Unmaps, and removes the name if this end created it. */
void bm_shm_close(bm_shm *s);

/* A name no other instance will pick, built from the process id and a counter.
 * `out` must hold BM_SHM_NAME_MAX. */
void bm_shm_name(char *out, size_t cap, unsigned counter);

/* Writes `len` bytes into a channel. Refuses anything that will not fit rather
 * than truncating: half a song is a song that will not parse. */
int  bm_shm_publish(bm_shm_channel *c, const char *text, size_t len);

/* Reads a channel if it has changed since `*last_seq`. Returns 1 when
 * something was copied out, 0 when there was nothing new or the write was
 * still in progress - in which case the caller simply asks again next frame.
 *
 * `*last_seq` starts at 0, which no completed write ever leaves behind, so the
 * first poll after attaching always delivers whatever is already there. */
int  bm_shm_take(bm_shm_channel *c, uint32_t *last_seq,
                 char *out, size_t cap, size_t *len);

#endif /* BM_SHM_H */
