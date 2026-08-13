/*
 * BENCmouth - the two functions a freestanding link still needs
 *
 * `make check-freestanding` proves the core includes no <string.h>, and that is
 * true and worth keeping. It does not mean the core links without one.
 *
 * A C compiler is permitted to emit calls to memcpy, memmove, memset and memcmp
 * for code that never names them - struct assignment, array initialisation,
 * zeroing an aggregate - and GCC does. Linking the core for a bare-metal target
 * with -nostdlib therefore fails on undefined `memcpy` and `memset` no matter
 * how clean the source is. This is not a defect in the core; it is the part of
 * the freestanding contract the environment is supposed to hold up.
 *
 * As of this harness those two are the complete list. Neither memmove nor
 * memcmp is referenced; a real port should still check its own build rather
 * than trust that, since the set depends on compiler and optimisation level:
 *
 *     arm-none-eabi-nm -u *.o | sort -u
 *
 * Everything else undefined at link time is libgcc - __aeabi_fmul, __aeabi_fadd
 * and friends for soft float, __aeabi_uldivmod for the one 64-bit division in
 * bm_measure.c - which is why the link ends in -lgcc. A real firmware would use
 * its vendor SDK's string routines instead of these; they are deliberately the
 * dumbest correct implementations, because the point here is to run the
 * synthesizer, not to benchmark a memcpy.
 */

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n--) *d++ = (unsigned char)c;
    return dst;
}
