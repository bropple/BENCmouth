/*
 * BENCmouth - voice file loading
 *
 * Host layer, because it touches the filesystem. The parsing of an individual
 * `key = value` pair lives in core (bm_voice_set_param), so an embedded target
 * can accept voice settings from a string without linking any of this.
 *
 * Format - deliberately boring, so it diffs well and can be hand-edited:
 *
 *     # BENCmouth voice
 *     name    = Gravel
 *     preset  = retro        # start from a preset, then override
 *     f0_base = 96
 *     throat  = 0.88
 *     tilt    = 9
 *
 * `preset` may appear anywhere but is applied where it appears, so put it
 * first unless you specifically want it to reset earlier settings.
 */

#ifndef BM_VOICEFILE_H
#define BM_VOICEFILE_H

#include "bencmouth.h"

/* Loads a voice file over `voice`, which should already hold a sensible
 * starting point (bm_voice_default, or a preset).
 *
 * On a parse error, writes a human-readable message with a line number into
 * `err` and returns nonzero. Unknown keys are errors: a typo that silently
 * does nothing produces a voice that mysteriously sounds wrong, which is far
 * worse to debug than a refusal to load.
 *
 * `name_buf` receives the voice's `name` field, if any, because bm_voice.name
 * is a borrowed pointer and the caller must own the storage. */
int bm_voicefile_load(const char *path, bm_voice *voice,
                      char *name_buf, size_t name_cap,
                      char *err, size_t err_cap);

/* Writes `voice` in the same format, so a tuned voice can be dumped and
 * shared. Pass "-" for stdout. */
int bm_voicefile_save(const char *path, const bm_voice *voice);

#endif /* BM_VOICEFILE_H */
