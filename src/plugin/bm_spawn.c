/*
 * BENCmouth - starting the editor, and finding it first
 * See bm_spawn.h.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bm_spawn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  define BM_EDITOR_EXE "bencmouth-gui.exe"
#else
#  include <signal.h>
#  include <time.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  define BM_EDITOR_EXE "bencmouth-gui"
#endif

#define BM_PATH_MAX 1024

static char g_origin[BM_PATH_MAX];

void bm_spawn_set_origin(const char *plugin_path)
{
    if (plugin_path == 0) { g_origin[0] = '\0'; return; }
    snprintf(g_origin, sizeof g_origin, "%s", plugin_path);
}

/* The directory `path` is in, written into `out`. */
static void parent_dir(const char *path, char *out, size_t cap)
{
    size_t n;

    snprintf(out, cap, "%s", path ? path : "");
    n = strlen(out);
    while (n > 0 && out[n - 1] != '/' && out[n - 1] != '\\') n--;
    if (n > 0) n--;                       /* drop the separator itself */
    out[n] = '\0';
}

static int is_runnable(const char *path)
{
#if defined(_WIN32)
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path, X_OK) == 0;
#endif
}

/* Fills `out` with the nth place worth looking. Returns 0 when the list is
 * finished. The order is nearest-first: something installed beside the plugin
 * is much more likely to be the matching build than something on PATH. */
static int candidate(int n, char *out, size_t cap)
{
    char dir[BM_PATH_MAX];

    /* The .clap itself is a bundle directory on macOS and a file elsewhere, so
     * "beside the plugin" means two different places. Both are tried. */
    parent_dir(g_origin, dir, sizeof dir);

    switch (n) {
    case 0:  snprintf(out, cap, "%s/" BM_EDITOR_EXE, dir); break;
    case 1:  snprintf(out, cap, "%s/../" BM_EDITOR_EXE, dir); break;
#if defined(__APPLE__)
    /* Where a .app puts its executable, under both names it might have. */
    case 2:  snprintf(out, cap, "/Applications/BENCmouth.app/Contents/MacOS/BENCmouth"); break;
    case 3:  snprintf(out, cap, "%s/Applications/BENCmouth.app/Contents/MacOS/BENCmouth",
                      getenv("HOME") ? getenv("HOME") : ""); break;
#elif defined(_WIN32)
    case 2:  snprintf(out, cap, "%s\\BENCmouth\\" BM_EDITOR_EXE,
                      getenv("ProgramFiles") ? getenv("ProgramFiles") : "C:\\Program Files");
             break;
    case 3:  return 0;
#else
    case 2:  snprintf(out, cap, "%s/.local/bin/" BM_EDITOR_EXE,
                      getenv("HOME") ? getenv("HOME") : ""); break;
    case 3:  snprintf(out, cap, "/usr/local/bin/" BM_EDITOR_EXE); break;
#endif
    case 4:  snprintf(out, cap, "/usr/bin/" BM_EDITOR_EXE); break;
    case 5:
        /* An override for anyone whose layout is none of the above, and the
         * first thing to reach for when this has gone wrong. */
        if (getenv("BENCMOUTH_EDITOR") == 0) return 0;
        snprintf(out, cap, "%s", getenv("BENCMOUTH_EDITOR"));
        break;
    default: return 0;
    }
    return 1;
}

int bm_spawn_editor(bm_spawn *s, const char *shm_name, char *err, size_t cap)
{
    char path[BM_PATH_MAX];
    char tried[BM_PATH_MAX];
    int  n;
    size_t at = 0;

    if (s == 0 || shm_name == 0) return -1;
    memset(s, 0, sizeof *s);
    tried[0] = '\0';

    for (n = 0; candidate(n, path, sizeof path); n++) {
        if (!is_runnable(path)) {
            int w = snprintf(tried + at, sizeof tried - at, "%s%s",
                             at ? ", " : "", path);
            if (w > 0 && (size_t)w < sizeof tried - at) at += (size_t)w;
            continue;
        }

#if defined(_WIN32)
        {
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            char line[BM_PATH_MAX * 2];

            memset(&si, 0, sizeof si);
            si.cb = sizeof si;
            memset(&pi, 0, sizeof pi);
            snprintf(line, sizeof line, "\"%s\" --editor %s", path, shm_name);

            if (!CreateProcessA(0, line, 0, 0, FALSE, 0, 0, 0, &si, &pi)) {
                continue;
            }
            CloseHandle(pi.hThread);
            s->handle = (void *)pi.hProcess;
            s->pid = (long)pi.dwProcessId;
            return 0;
        }
#else
        {
            pid_t kid = fork();

            if (kid < 0) continue;
            if (kid == 0) {
                /* The editor must not inherit a hold on the host's terminal or
                 * its file descriptors beyond the standard three, and it must
                 * not become a zombie the host has to reap - so it is its own
                 * session and its own process group. */
                setsid();
                execl(path, BM_EDITOR_EXE, "--editor", shm_name, (char *)0);
                _exit(127);
            }
            s->pid = (long)kid;
            return 0;
        }
#endif
    }

    if (err != 0 && cap > 0) {
        snprintf(err, cap, "no editor found - looked in: %s", tried);
    }
    return -1;
}

int bm_spawn_alive(bm_spawn *s)
{
    if (s == 0 || s->pid == 0) return 0;

#if defined(_WIN32)
    {
        DWORD code = 0;
        if (s->handle == 0) return 0;
        if (!GetExitCodeProcess((HANDLE)s->handle, &code)) return 0;
        return code == STILL_ACTIVE;
    }
#else
    {
        int status = 0;
        pid_t r = waitpid((pid_t)s->pid, &status, WNOHANG);
        if (r == (pid_t)s->pid) { s->pid = 0; return 0; }
        /* Not ours to reap - it was reparented, which happens when the host
         * forks between the spawn and this call. Ask the kernel instead. */
        if (r < 0) return kill((pid_t)s->pid, 0) == 0;
        return 1;
    }
#endif
}

void bm_spawn_reap(bm_spawn *s)
{
    if (s == 0 || s->pid == 0) return;

#if defined(_WIN32)
    if (s->handle != 0) {
        WaitForSingleObject((HANDLE)s->handle, 2000);
        CloseHandle((HANDLE)s->handle);
        s->handle = 0;
    }
#else
    {
        int i;
        /* A couple of seconds for a window to close itself, then give up
         * waiting. The process is not killed here: an editor that is slow to
         * quit is still an editor, and the block it is holding is not freed
         * until it goes. */
        for (i = 0; i < 200; i++) {
            if (waitpid((pid_t)s->pid, 0, WNOHANG) == (pid_t)s->pid) break;
            {
                struct timespec t;
                t.tv_sec = 0;
                t.tv_nsec = 10 * 1000000L;
                nanosleep(&t, 0);
            }
        }
    }
#endif
    s->pid = 0;
}

void bm_spawn_kill(bm_spawn *s)
{
    if (s == 0 || s->pid == 0) return;

#if defined(_WIN32)
    if (s->handle != 0) {
        TerminateProcess((HANDLE)s->handle, 0);
        CloseHandle((HANDLE)s->handle);
        s->handle = 0;
    }
#else
    kill((pid_t)s->pid, SIGKILL);
    waitpid((pid_t)s->pid, 0, 0);
#endif
    s->pid = 0;
}
