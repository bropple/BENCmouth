/*
 * BENCmouth - the thing that actually runs on the emulated Cortex-M
 *
 * Two questions, one program.
 *
 * Correctness: does the core produce the same audio on ARM that it produces on
 * x86? The same source builds for the host, so both print a checksum over the
 * rendered samples and they must match exactly. This matters most for the
 * fixed-point path, which is pure integer arithmetic and therefore has no
 * excuse for differing between architectures - if it does, that is a real
 * portability bug and not a rounding artefact.
 *
 * Cost: how many instructions does a sample take? The QEMU plugin counts the
 * whole run, so the harness is built twice - once rendering, once not - and the
 * difference divided by the sample count is the per-sample figure. Without that
 * subtraction the answer includes engine init and .bss clearing, which is a few
 * hundred thousand instructions of noise.
 */

#include "bencmouth.h"

#ifdef BM_QEMU_TARGET
#  include "semihost.h"
#  define OUT_KV(k, v)    sh_kv((k), (unsigned long)(v))
#  define OUT_KVHEX(k, v) sh_kvhex((k), (unsigned long)(v))
#  define OUT_DONE()      sh_exit()
#else
#  include <stdio.h>
#  define OUT_KV(k, v)    printf("%s=%lu\n", (k), (unsigned long)(v))
#  define OUT_KVHEX(k, v) printf("%s=0x%08lx\n", (k), (unsigned long)(v))
#  define OUT_DONE()      ((void)0)
#endif

#ifndef BENCH_RATE
#  define BENCH_RATE 22050
#endif

/* Set at build time to measure the fixed cost of everything that is not
 * rendering; see the note above. */
#ifndef BENCH_RENDER
#  define BENCH_RENDER 1
#endif

static const char BENCH_TEXT[] =
    "the quick brown fox jumps over the lazy dog";

/* .bss, not the stack: the engine is tens of kilobytes and the harness should
 * not be the reason a target runs out of stack. */
static bm_engine_storage g_storage;
static bm_sample         g_buf[256];

int main(void)
{
    bm_engine *e;
    bm_config  cfg;
    unsigned long total = 0ul;
    unsigned long hash  = 2166136261ul;   /* FNV-1a */
    size_t n, i;

    bm_config_default(&cfg);
    cfg.sample_rate = BENCH_RATE;

    if (bm_engine_init(&g_storage, &cfg, &e) != BM_OK) {
        OUT_KV("error_init", 1);
        OUT_DONE();
        return 1;
    }

    OUT_KV("engine_bytes", (unsigned long)bm_engine_size());
    OUT_KV("rate", (unsigned long)BENCH_RATE);

#if BENCH_RENDER
    if (bm_speak_text(e, BENCH_TEXT, 0) != BM_OK) {
        OUT_KV("error_speak", 1);
        OUT_DONE();
        return 1;
    }

    for (;;) {
        n = bm_read(e, g_buf, sizeof g_buf / sizeof g_buf[0]);
        if (n == 0) break;
        for (i = 0; i < n; i++) {
            /* Hash the raw 16 bits. With BM_SAMPLE_FLOAT=0 that is the sample
             * itself; the float build is compared separately and only against
             * another float build. */
            unsigned long v = (unsigned long)(unsigned short)(short)g_buf[i];
            hash = (hash ^ (v & 0xffu)) * 16777619ul;
            hash = (hash ^ ((v >> 8) & 0xffu)) * 16777619ul;
        }
        total += (unsigned long)n;
    }
#endif

    OUT_KV("samples", total);
    OUT_KVHEX("checksum", hash & 0xfffffffful);
    OUT_KV("rendered", (unsigned long)BENCH_RENDER);
    OUT_DONE();
    return 0;
}
