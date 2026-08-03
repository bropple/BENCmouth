/*
 * BENCmouth - live audio output
 *
 * Host layer, one small interface over three platform backends. The core knows
 * nothing about any of this: bm_read() hands back samples on demand and this
 * feeds them to a device, which is exactly the shape the pull model was built
 * for.
 *
 *   ALSA        Linux    -DBM_AUDIO_ALSA       -lasound
 *   AudioQueue  macOS    -DBM_AUDIO_COREAUDIO  -framework AudioToolbox
 *   waveOut     Windows  -DBM_AUDIO_WINMM      -lwinmm
 *
 * `make audio` picks the right one from `uname`. Without a backend the calls
 * still link and bm_audio_available() returns 0, so the CLI can say "not
 * compiled in" rather than failing to build. Audio is optional for the same
 * reason the dictionary is: a default build should need nothing but a C
 * compiler.
 *
 * The write is blocking, which is the whole point. A callback API would mean a
 * ring buffer between the callback and bm_read(), and bm_read() already does
 * not block or allocate - so blocking writes from one thread are both simpler
 * and a more honest test of that claim.
 */

#ifndef BM_AUDIO_H
#define BM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef struct bm_audio bm_audio;

/* Nonzero if a backend was compiled in. */
int         bm_audio_available(void);

/* Backend name for messages: "ALSA", "CoreAudio", "WinMM", or "none". */
const char *bm_audio_backend(void);

/* Opens the default output device for mono playback at `sample_rate`.
 * Returns 0 on success. On failure writes a reason into `err` (may be NULL)
 * and leaves `*out` untouched. */
int  bm_audio_open(bm_audio **out, uint32_t sample_rate,
                   char *err, size_t err_cap);

/* Writes `count` mono samples, blocking until the device has taken them.
 * Samples are floats in roughly [-1, 1]; conversion and limiting match the WAV
 * writer exactly, so a file and the speaker sound the same.
 * Returns 0 on success. */
int  bm_audio_write(bm_audio *a, const float *samples, size_t count);

/* Waits for queued audio to finish playing. Without this the process exits
 * mid-word. */
int  bm_audio_drain(bm_audio *a);

void bm_audio_close(bm_audio *a);

#endif /* BM_AUDIO_H */
