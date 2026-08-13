/*
 * BENCmouth - starting the editor, and finding it first
 *
 * The editor is the standalone GUI run as `bencmouth-gui --editor <name>`, so
 * the plugin has to locate a program it did not install and was not told about.
 * Nothing guarantees the two arrived together: a .clap goes to one directory a
 * host scans and the application goes wherever applications go, and a user who
 * installs one and not the other is not doing anything wrong.
 *
 * So the search is a list of places it is reasonable to look, tried in order,
 * and a failure that says which places were tried - "the editor could not be
 * found" with no list is a bug report nobody can act on.
 */

#ifndef BM_SPAWN_H
#define BM_SPAWN_H

#include <stddef.h>

typedef struct bm_spawn {
    void *handle;        /* Windows: the process handle. POSIX: unused. */
    long  pid;           /* 0 when nothing is running */
} bm_spawn;

/* Where the plugin binary is, for the search to start from. Called once, with
 * whatever the host handed to clap_entry's init. */
void bm_spawn_set_origin(const char *plugin_path);

/* Starts `bencmouth-gui --editor <shm_name>`. Returns 0, or -1 with a message
 * in `err` naming what was tried. */
int  bm_spawn_editor(bm_spawn *s, const char *shm_name, char *err, size_t cap);

/* Nonzero while the process is still running. */
int  bm_spawn_alive(bm_spawn *s);

/* Reaps it if it has exited. Does not kill: the editor is asked to close
 * through the shared block, so that it can put its own window away. */
void bm_spawn_reap(bm_spawn *s);

/* Last resort, for an editor that has stopped answering. */
void bm_spawn_kill(bm_spawn *s);

#endif /* BM_SPAWN_H */
