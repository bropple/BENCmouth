/*
 * BENCmouth GUI - assets compiled into the binary
 *
 * Generated into src/gui/bm_embed.c by tools/mkembed.c at build time. The
 * files in assets/ stay the single source; nothing here is committed.
 *
 * The font is in here because a font beside an executable is a font that can
 * go missing - and when it went missing the window silently came up in
 * raylib's fallback face. The licences are in here because embedding the font
 * without them would not be allowed: the OFL lets the font be bundled with
 * software provided every copy carries the copyright notice and the licence in
 * a form the user can easily view. The information window is that form.
 */

#ifndef BM_EMBED_H
#define BM_EMBED_H

/* Terminus (TTF), unmodified. */
extern const unsigned char BM_FONT_TTF[];
extern const unsigned int  BM_FONT_TTF_LEN;

/* The BENCO wordmark, white on transparent. */
extern const unsigned char BM_LOGO_PNG[];
extern const unsigned int  BM_LOGO_PNG_LEN;

/* H. Hex, for the window icon where there is no such thing as an executable
 * resource. */
extern const unsigned char BM_ICON_PNG[];
extern const unsigned int  BM_ICON_PNG_LEN;

/* Licence texts, NUL-terminated - the repository's own files, so what the
 * window shows and what the archive ships cannot drift apart. */
extern const unsigned char BM_LICENSE_MIT[];
extern const unsigned int  BM_LICENSE_MIT_LEN;

extern const unsigned char BM_NOTICE[];
extern const unsigned int  BM_NOTICE_LEN;

extern const unsigned char BM_LICENSE_OFL[];
extern const unsigned int  BM_LICENSE_OFL_LEN;

#endif /* BM_EMBED_H */
