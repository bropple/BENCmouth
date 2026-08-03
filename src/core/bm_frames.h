/*
 * BENCmouth - phoneme sequence to parameter frames
 *
 * Turns a list of phonemes into the continuous parameter track the
 * synthesizer consumes. This is where speech stops being a list of symbols and
 * becomes something that moves.
 *
 * The model is targets and transitions: each phoneme names where the
 * articulators want to be, and the generator glides between those targets
 * rather than stepping. Real articulators have mass and cannot teleport, and
 * the glide is not cosmetic - for stops, almost the entire perceptual cue to
 * *which* stop it was lives in how neighbouring formants bend toward the
 * closure, not in the burst itself.
 *
 * Each phoneme expands into up to four segments, in this order:
 *
 *   TRANSITION  glide from wherever the previous phoneme ended to this target
 *   CLOSURE     stops and affricates only: silence, or a faint voice bar
 *   BURST       stops and affricates only: the release
 *   STEADY      hold the target; diphthongs glide toward their end target here
 *
 * Transition precedes closure so that formants have already reached the stop's
 * locus before the mouth shuts, which is the order it happens in a real vocal
 * tract.
 *
 * Prosody here is deliberately minimal - declination plus stress. bm_prosody.c
 * will take it over and do it properly.
 */

#ifndef BM_FRAMES_H
#define BM_FRAMES_H

#include "bencmouth.h"
#include "bm_phonemes.h"

/* Stress slot value meaning "the input carried no stress digit". Distinct from
 * 0, which means the input explicitly said unstressed. */
#define BM_STRESS_UNMARKED 0xFFu

/* Per-phoneme overrides set by inline markup. Zero in every field means "no
 * override", so a sequence with no markup costs nothing but the memory.
 *
 * Held per phoneme rather than as running parser state because the generator
 * can be reset and replayed, and a command's effect has to survive that. */
typedef struct bm_phoneme_mod {
    float          f0;      /* absolute Hz;   0 = use the voice */
    float          speed;   /* multiplier;    0 = use the voice */
    unsigned short dur_ms;  /* steady length; 0 = use the phoneme */
} bm_phoneme_mod;

typedef struct bm_frame_gen {
    float    frame_rate;
    bm_voice voice;

    const bm_phoneme *seq[BM_MAX_PHONEMES];
    unsigned char     stress[BM_MAX_PHONEMES];
    bm_phoneme_mod    mod[BM_MAX_PHONEMES];
    int               count;

    int index;         /* phoneme being emitted */
    int segment;
    int frame_in_seg;

    int frames_total;  /* whole utterance, for the declination contour */
    int frames_done;

    bm_frame from;     /* state at the start of the current transition */
    bm_frame to;
    bm_frame last;     /* most recently emitted, becomes the next `from` */
} bm_frame_gen;

void bm_frame_gen_init(bm_frame_gen *g, float frame_rate, const bm_voice *voice);
void bm_frame_gen_reset(bm_frame_gen *g);

/* Parses whitespace-separated ARPABET with optional stress digits, e.g.
 * "HH AH0 L OW1". Unknown tokens are rejected rather than skipped, because
 * silently dropping a phoneme produces a subtly wrong word that is far harder
 * to diagnose than a loud error. */
bm_result bm_frame_gen_set_phonemes(bm_frame_gen *g, const char *phonemes,
                                    size_t len);

/* Writes the next frame. Returns 1 while frames remain, 0 when the utterance
 * is finished. */
int bm_frame_gen_next(bm_frame_gen *g, bm_frame *out);

/* Total frames the current sequence will produce. */
int bm_frame_gen_length(const bm_frame_gen *g);

#endif /* BM_FRAMES_H */
