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
    float          f0;      /* Hz;            0 = use the voice */
    float          speed;   /* multiplier;    0 = use the voice */
    unsigned short dur_ms;  /* steady length; 0 = use the phoneme */

    /* [dur]: how long the whole run of phonemes carrying this group is to
     * last, and which run that is. Where dur_ms says "hold the vowel this
     * long", these say "make all of this take this long, consonants
     * included" - see the [dur] note above apply_dur_groups in bm_frames.c.
     *
     * The group id is carried per phoneme rather than the run being found by
     * looking for equal totals, because two adjacent notes of the same length
     * are the common case in a melody and would otherwise merge into one. */
    unsigned short dur_total; /* ms;            0 = not in a group */
    unsigned short dur_group; /* 1-based id;    0 = not in a group */

    /* [glide]: how long an absolute note takes to be reached, rather than
     * being arrived at in one frame. 0 is an instant change, which is what a
     * note always was.
     *
     * It belongs to the note being moved to rather than to the voice, because
     * a slur is something a score asks for on one note and not a mode a singer
     * is in: real singing steps cleanly onto a re-articulated note and slides
     * onto a tied one, and a voice-wide setting cannot tell those apart. */
    unsigned short f0_glide_ms;

    /* Whether f0 replaces the planned contour or transposes it.
     *
     * [pitch] transposes: the intonation of everything after it is preserved,
     * shifted to a new base, which is what you want when speaking. [note]
     * replaces: a sung note is that pitch and not that pitch plus whatever
     * accent the prosody planner had in mind for the syllable. Getting this
     * wrong put A4 at 525 Hz instead of 440. */
    unsigned char  f0_absolute;
} bm_phoneme_mod;

typedef struct bm_frame_gen {
    float    frame_rate;
    bm_voice voice;

    const bm_phoneme *seq[BM_MAX_PHONEMES];
    unsigned char     stress[BM_MAX_PHONEMES];
    bm_phoneme_mod    mod[BM_MAX_PHONEMES];

    /* Byte offset in the input string where each phoneme's token began.
     * Carried so that a caller measuring a score can say which text produced
     * the sound it is looking at - an editor drawing notes on a grid has to
     * map a span back to the characters that made it, and recovering that by
     * re-parsing outside would mean two parsers that must agree forever. */
    uint32_t          src[BM_MAX_PHONEMES];

    /* Planned by bm_prosody.c when the voice asks for it. Consulted only when
     * voice.prosody > 0; below that the older whole-utterance contour runs
     * instead, untouched. */
    float             f0_plan[BM_MAX_PHONEMES];
    float             dur_plan[BM_MAX_PHONEMES];

    int               count;

    int index;         /* phoneme being emitted */
    int segment;
    int frame_in_seg;

    int frames_total;  /* whole utterance, for the declination contour */
    int frames_done;

    float f0_smooth;   /* pitch chases its target; see bm_frame_gen_next */
    int   f0_started;

    /* A [glide] in progress: where it started, where it is going, and how far
     * through it is. Held rather than derived because the pitch it starts from
     * is wherever the last frame left off - which is not the previous note when
     * a glide is interrupted by a faster one. */
    float f0_glide_from;
    float f0_glide_to;
    int   f0_glide_len;    /* frames */
    int   f0_glide_at;

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

/* Frames phoneme `index` occupies, every segment of it together.
 *
 * Exposed so that measuring an utterance and rendering it go through the same
 * arithmetic. A second implementation of "how long is this" would be free to
 * drift from the one that makes the sound, and a piano roll drawn from a
 * timeline that disagrees with the audio is worse than one with no timeline. */
int bm_frame_gen_phoneme_frames(const bm_frame_gen *g, int index);

#endif /* BM_FRAMES_H */
