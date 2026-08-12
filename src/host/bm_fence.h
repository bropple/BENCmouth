/*
 * BENCmouth - a memory barrier, in one place
 *
 * Two things here hand data from one thread to another without a lock: the
 * player, which is given a freshly rendered score while an audio callback may
 * be reading the last one, and the shared block, which carries a song between
 * the plugin and its editor. Both publish a pointer and a length, or a payload
 * and a sequence, and both depend on those becoming visible in the order they
 * were written.
 *
 * `volatile` does not give that. It stops the *compiler* reordering accesses
 * to volatile objects; it says nothing about the processor. On x86 that
 * distinction never shows, because the hardware orders stores anyway - which is
 * exactly why the first version of both of these was written with a compiler
 * barrier and a confident comment, passed four thousand rounds of a torn-read
 * test on the machine it was written on, and then failed on the first Apple
 * Silicon runner it met. arm64 is weakly ordered. The reader saw two halves of
 * two different songs.
 *
 * So: a real fence, in one header, used by both. A second copy of this would be
 * right until the day one of them changed.
 */

#ifndef BM_FENCE_H
#define BM_FENCE_H

#if defined(_MSC_VER)
#  include <intrin.h>
#  define BM_FENCE() MemoryBarrier()
#elif defined(__ATOMIC_SEQ_CST)
/* GCC 4.7+ and every clang. A full fence rather than acquire/release: it is
 * stronger than needed, it costs one `dmb ish` on a path that runs a few times
 * a second, and there is only one of it to get right. */
#  define BM_FENCE() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#elif defined(__GNUC__)
#  define BM_FENCE() __sync_synchronize()
#else
/* Deliberately a build error. Publishing without a barrier is not a slower
 * program, it is one that hands over torn data on any machine that reorders -
 * and the symptom is a corrupt song or a read off the end of a buffer, neither
 * of which looks anything like a missing fence. */
#  error "no memory barrier for this compiler - see src/host/bm_fence.h"
#endif

#endif /* BM_FENCE_H */
