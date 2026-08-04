/*
 * BENCmouth GUI - a native "save as" dialog
 *
 * raylib has no file dialog, and the alternatives were all worse than this
 * file. Bundling one of the single-header dialog libraries would end the claim
 * that raylib is the only third-party dependency in the project, over a
 * feature that is a system call on every platform it targets. Drawing a file
 * browser out of the widget set would work and look right, but it would be a
 * home-made file browser: no network shares, no sidebar, none of the places
 * the operating system already knows the user keeps things.
 *
 * So each platform gets the dialog it already has. Nothing is linked that the
 * system does not already ship:
 *
 *   Windows   GetSaveFileName from comdlg32, which is part of the OS
 *   macOS     NSSavePanel, reached through osascript
 *   Unix      zenity or kdialog, whichever is installed
 *
 * The Unix case is the only one that can come up empty, so the return value
 * distinguishes "the user said no" from "there is nothing here to ask with" -
 * a caller that cannot tell those apart either loses the file or nags about a
 * cancellation the user meant.
 */

#ifndef BM_FILEDLG_H
#define BM_FILEDLG_H

#include <stddef.h>

enum {
    BM_DLG_CANCELLED   = 0,   /* the user dismissed it                     */
    BM_DLG_OK          = 1,   /* `out` holds a path                        */
    BM_DLG_UNAVAILABLE = -1   /* no dialog on this machine; caller decides */
};

/* Asks where to save. `filter_desc`/`filter_ext` describe one file type, e.g.
 * "WAV audio" and "wav" - no leading dot. `out` receives an absolute path.
 *
 * `owner` is the native window handle to parent the dialog to - raylib's
 * GetWindowHandle(), or NULL. It is passed in rather than fetched here so that
 * this file never includes raylib.h: raylib and windows.h both define
 * Rectangle, CloseWindow, ShowCursor and LoadImage, and a translation unit
 * that needs windows.h cannot have both. */
int bm_save_dialog(void *owner, const char *title, const char *default_name,
                   const char *filter_desc, const char *filter_ext,
                   char *out, size_t cap);

#endif /* BM_FILEDLG_H */
