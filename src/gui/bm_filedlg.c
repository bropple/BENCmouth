/*
 * BENCmouth GUI - a native "save as" dialog
 * See bm_filedlg.h for why this is three platform cases and not a library.
 */

/* popen/pclose are POSIX, and -std=c99 alone does not declare them. */
#if defined(__APPLE__)
#  define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bm_filedlg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
#if defined(_WIN32)

#include <windows.h>
#include <commdlg.h>

int bm_save_dialog(void *owner, const char *title, const char *default_name,
                   const char *filter_desc, const char *filter_ext,
                   char *out, size_t cap)
{
    OPENFILENAMEA ofn;
    char path[1024];
    char filter[128];
    size_t n = 0;

    if (out == 0 || cap < 2) return BM_DLG_CANCELLED;

    /* The filter is a run of NUL-terminated strings ending in a second NUL,
     * which is why it is assembled by hand rather than with snprintf. */
    n += (size_t)snprintf(filter + n, sizeof filter - n, "%s (*.%s)",
                          filter_desc, filter_ext);
    filter[n++] = '\0';
    n += (size_t)snprintf(filter + n, sizeof filter - n, "*.%s", filter_ext);
    filter[n++] = '\0';
    n += (size_t)snprintf(filter + n, sizeof filter - n, "All files (*.*)");
    filter[n++] = '\0';
    n += (size_t)snprintf(filter + n, sizeof filter - n, "*.*");
    filter[n++] = '\0';
    filter[n++] = '\0';

    snprintf(path, sizeof path, "%s", default_name);

    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize  = sizeof ofn;
    ofn.hwndOwner    = (HWND)owner;
    ofn.lpstrFilter  = filter;
    ofn.lpstrFile    = path;
    ofn.nMaxFile     = (DWORD)sizeof path;
    ofn.lpstrTitle   = title;
    ofn.lpstrDefExt  = filter_ext;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST |
                       OFN_NOCHANGEDIR;

    if (!GetSaveFileNameA(&ofn)) return BM_DLG_CANCELLED;

    snprintf(out, cap, "%s", path);
    return BM_DLG_OK;
}

/* ------------------------------------------------------------------ */
#else

/* Reads one line of output from a command. Returns 0 if the command could not
 * be run or printed nothing - which for these dialogs means cancelled. */
static int read_line(const char *cmd, char *out, size_t cap)
{
    FILE *p = popen(cmd, "r");
    size_t n;

    if (p == 0) return 0;
    if (fgets(out, (int)cap, p) == 0) { pclose(p); return 0; }
    pclose(p);

    n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
    return n > 0;
}

static int have(const char *tool)
{
    char cmd[64];
    snprintf(cmd, sizeof cmd, "command -v %s >/dev/null 2>&1", tool);
    return system(cmd) == 0;
}

int bm_save_dialog(void *owner, const char *title, const char *default_name,
                   const char *filter_desc, const char *filter_ext,
                   char *out, size_t cap)
{
    char cmd[1024];

    (void)owner;   /* X11 and Cocoa parent these dialogs themselves */
    if (out == 0 || cap < 2) return BM_DLG_CANCELLED;

#if defined(__APPLE__)
    (void)filter_desc;
    (void)filter_ext;
    /* `choose file name` is the Cocoa save panel. AppleScript raises an error
     * on cancel, so stderr is dropped and an empty read means cancelled. */
    snprintf(cmd, sizeof cmd,
             "osascript -e 'POSIX path of (choose file name with prompt \"%s\" "
             "default name \"%s\")' 2>/dev/null",
             title, default_name);
    return read_line(cmd, out, cap) ? BM_DLG_OK : BM_DLG_CANCELLED;
#else
    if (have("zenity")) {
        snprintf(cmd, sizeof cmd,
                 "zenity --file-selection --save --confirm-overwrite "
                 "--title='%s' --filename='%s' "
                 "--file-filter='%s | *.%s' --file-filter='All files | *' "
                 "2>/dev/null",
                 title, default_name, filter_desc, filter_ext);
        return read_line(cmd, out, cap) ? BM_DLG_OK : BM_DLG_CANCELLED;
    }
    if (have("kdialog")) {
        snprintf(cmd, sizeof cmd,
                 "kdialog --title '%s' --getsavefilename '%s' '*.%s|%s' "
                 "2>/dev/null",
                 title, default_name, filter_ext, filter_desc);
        return read_line(cmd, out, cap) ? BM_DLG_OK : BM_DLG_CANCELLED;
    }
    return BM_DLG_UNAVAILABLE;
#endif
}

#endif
