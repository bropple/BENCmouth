/*
 * BENCmouth - phrase-level prosody
 *
 * Plans a pitch contour and duration adjustments across a phoneme sequence,
 * before any of it becomes frames.
 *
 * What this replaces: bm_frames.c used to fall linearly in pitch across the
 * whole utterance and add a flat 6% bump on stressed phonemes. That is enough
 * to stop speech sounding like a list, and nothing like enough to sound like
 * sentences. It has no notion of where one phrase ends and the next begins, so
 * a paragraph declined steadily into the floor; and it could not distinguish a
 * question from a statement, because the front end collapsed both to silence.
 *
 * What this does instead:
 *
 *   - segments at boundary phonemes, so each phrase declines from its own
 *     starting pitch rather than continuing the previous one downhill
 *   - places pitch accents on stressed syllables, which is what the dictionary
 *     made possible by supplying stress at all
 *   - shapes the last third of each phrase by boundary type: a fall for a full
 *     stop, a rise for a question, a small non-committal lift for a comma
 *   - lengthens the final syllable before a boundary, which is one of the
 *     strongest and most reliable cues that a phrase has ended
 *
 * Everything is scaled by voice->f0_range (semitones of excursion) and
 * voice->prosody (0..1). At prosody 0 the caller should ignore this plan
 * entirely and keep the older contour - see the Retro contract.
 */

#ifndef BM_PROSODY_H
#define BM_PROSODY_H

#include "bencmouth.h"
#include "bm_phonemes.h"

/* Fills `f0_out` with a target pitch in Hz per phoneme, and `dur_out` with a
 * duration multiplier per phoneme. Both arrays must hold `count` entries.
 *
 * A pure function of its inputs: no state, no allocation, and therefore
 * testable by inspecting the numbers rather than by listening. */
void bm_prosody_plan(const bm_phoneme *const *seq,
                     const unsigned char *stress,
                     int count,
                     const bm_voice *voice,
                     float *f0_out,
                     float *dur_out);

#endif /* BM_PROSODY_H */
