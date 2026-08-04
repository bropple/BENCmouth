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
 *
 * A file may also carry an effects chain, since what makes something sound like
 * a particular character is often the voice and the effects together:
 *
 *     effects = sentinel      # an effects preset, same idea as `preset`
 *     drive   = 0.9           # then override
 *
 * The two namespaces do not overlap, so a key is looked up as a voice setting
 * first and as an effect second, and only then rejected.
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
 * is a borrowed pointer and the caller must own the storage.
 *
 * `effects` must not be null. It could have been optional, with effect keys
 * rejected when it is absent, but every caller has one and a "this file has
 * settings I am not going to apply" mode is a trap rather than a convenience. */
int bm_voicefile_load(const char *path, bm_voice *voice, bm_effects *effects,
                      char *name_buf, size_t name_cap,
                      char *err, size_t err_cap);

/* Writes `voice` in the same format, so a tuned voice can be dumped and
 * shared. Pass "-" for stdout. The effects block is written only when
 * something is switched on, so a file for a plain voice stays a plain file. */
int bm_voicefile_save(const char *path, const bm_voice *voice,
                      const bm_effects *effects);

#endif /* BM_VOICEFILE_H */
