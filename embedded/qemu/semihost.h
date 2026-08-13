/*
 * BENCmouth - ARM semihosting, just enough of it
 *
 * The guest talks to the host by executing BKPT 0xAB with an operation number
 * in r0 and an argument in r1; QEMU traps it. Two operations are enough here -
 * write a NUL-terminated string, and exit - so this avoids newlib entirely and
 * keeps the harness as freestanding as the code it is testing.
 *
 * Number formatting is hand-rolled for the same reason: pulling in printf to
 * print an integer would link a chunk of libc into a test whose whole point is
 * that the core does not need one.
 */

#ifndef BM_SEMIHOST_H
#define BM_SEMIHOST_H

#define SYS_WRITE0 0x04
#define SYS_EXIT   0x18

#define ADP_Stopped_ApplicationExit 0x20026u

static inline int semihost(int op, void *arg)
{
    register int   r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm__ volatile ("bkpt #0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

static inline void sh_puts(const char *s)
{
    semihost(SYS_WRITE0, (void *)s);
}

static inline void sh_exit(void)
{
    semihost(SYS_EXIT, (void *)ADP_Stopped_ApplicationExit);
}

/* Unsigned decimal. 32 bits needs ten digits plus a terminator. */
static inline void sh_putu(unsigned long v)
{
    char buf[12];
    int  i = (int)sizeof buf - 1;

    buf[i] = '\0';
    do { buf[--i] = (char)('0' + (v % 10ul)); v /= 10ul; } while (v && i > 0);
    sh_puts(&buf[i]);
}

static inline void sh_puthex(unsigned long v)
{
    static const char D[] = "0123456789abcdef";
    char buf[11];
    int  i;

    buf[0] = '0'; buf[1] = 'x';
    for (i = 0; i < 8; i++) buf[2 + i] = D[(v >> (28 - 4 * i)) & 0xfu];
    buf[10] = '\0';
    sh_puts(buf);
}

/* key=value lines, so the runner can grep the results out without parsing. */
static inline void sh_kv(const char *k, unsigned long v)
{
    sh_puts(k); sh_puts("="); sh_putu(v); sh_puts("\n");
}

static inline void sh_kvhex(const char *k, unsigned long v)
{
    sh_puts(k); sh_puts("="); sh_puthex(v); sh_puts("\n");
}

#endif /* BM_SEMIHOST_H */
