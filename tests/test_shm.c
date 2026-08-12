/*
 * BENCmouth - the plugin/editor shared block
 *
 * Tested across a real process boundary rather than inside one. A block that
 * two threads can pass a song through proves almost nothing about the case it
 * exists for: separate address spaces, separate mappings of one file, and a
 * reader that may be looking while a writer is halfway through.
 *
 * So this forks. The child attaches, echoes what it is given and publishes back
 * a song of its own, and the parent checks what arrived. The torn-read test
 * then writes continuously from the child while the parent reads, and asserts
 * the one property a seqlock is for: every song that comes out is a whole one,
 * never two halves of two.
 */

#include "../src/plugin/bm_shm.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>

/* nanosleep rather than usleep: usleep is obsolescent and hidden by the very
 * feature test bm_shm.c has to set to see shm_open at all. */
static void nap_ms(long ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, 0);
}
#endif

static int failures = 0;

static void check(int ok, const char *what)
{
    printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

#if defined(_WIN32)
int main(void)
{
    printf("\nBENCmouth shared block tests\n\n");
    printf("  skipped: this test forks, and Windows does not.\n");
    printf("  The Windows mapping is exercised by the plugin itself.\n\n");
    return 0;
}
#else

/* One song, recognisable at a glance and long enough to span a page. */
static void make_song(char *out, size_t cap, int n)
{
    size_t at = 0;
    int i;

    at += (size_t)snprintf(out + at, cap - at,
                           "# BENCmouth song\ntitle = round %d\nscore =\n", n);
    for (i = 0; i < 200 && at + 64 < cap; i++) {
        at += (size_t)snprintf(out + at, cap - at,
                               "[dur 500][note C4] M IY1  %d\n", n);
    }
}

/* Every line of a song has to carry the same round number. A copy made of two
 * different writes will not. */
static int song_is_whole(const char *text)
{
    const char *p = strstr(text, "title = round ");
    int want, got;

    if (p == 0) return 0;
    want = atoi(p + 14);

    p = text;
    while ((p = strstr(p, "M IY1  ")) != 0) {
        got = atoi(p + 7);
        if (got != want) return 0;
        p += 7;
    }
    return 1;
}

static void test_across_a_fork(void)
{
    bm_shm  s;
    char    name[BM_SHM_NAME_MAX];
    static char song[BM_SHM_TEXT];
    static char back[BM_SHM_TEXT];
    uint32_t seq = 0;
    size_t   len = 0;
    pid_t    kid;
    int      status = 0, tries;

    printf("across a process boundary\n");

    bm_shm_name(name, sizeof name, 1);
    check(bm_shm_create(&s, name) == 0, "the plugin end creates a block");

    make_song(song, sizeof song, 7);
    check(bm_shm_publish(&s.block->to_editor, song, strlen(song)) == 0,
          "and publishes a song into it");
    s.block->sample_rate = 48000u;
    s.block->pos_ms = 1234.5;

    kid = fork();
    if (kid == 0) {
        /* The editor end: a different process, its own mapping. */
        bm_shm   e;
        static char got[BM_SHM_TEXT];
        uint32_t es = 0;
        int      ok = 0;

        if (bm_shm_attach(&e, name) == 0) {
            int i;
            for (i = 0; i < 200; i++) {
                if (bm_shm_take(&e.block->to_editor, &es, got, sizeof got, 0)) {
                    ok = song_is_whole(got) && strstr(got, "round 7") != 0;
                    break;
                }
                nap_ms(1);
            }
            if (ok && e.block->sample_rate == 48000u && e.block->pos_ms > 1234.0) {
                static char mine[BM_SHM_TEXT];
                make_song(mine, sizeof mine, 9);
                bm_shm_publish(&e.block->to_plugin, mine, strlen(mine));
            } else {
                ok = 0;
            }
            e.block->heartbeat = 42u;
            bm_shm_close(&e);
        }
        _exit(ok ? 0 : 1);
    }

    check(kid > 0, "and forks an editor");

    /* Wait for the child's answer the way the plugin does: by polling. */
    for (tries = 0; tries < 500; tries++) {
        if (bm_shm_take(&s.block->to_plugin, &seq, back, sizeof back, &len)) break;
        nap_ms(1);
    }
    waitpid(kid, &status, 0);

    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "the editor read the song, and the telemetry with it");
    check(tries < 500, "and the plugin got one back");
    check(len > 0 && song_is_whole(back) && strstr(back, "round 9") != 0,
          "whole, and the one the editor sent");
    check(s.block->heartbeat == 42u, "a plain word crosses too");

    bm_shm_close(&s);
    check(s.block == 0, "closing clears the handle");

    /* And the name is gone, so the next instance cannot attach to a stale
     * block from a plugin that has been destroyed. */
    {
        bm_shm again;
        check(bm_shm_attach(&again, name) != 0,
              "the name is removed when the owner closes");
    }
}

/* The property the seqlock exists for. */
static void test_a_writer_that_never_stops(void)
{
    bm_shm  s;
    char    name[BM_SHM_NAME_MAX];
    static char got[BM_SHM_TEXT];
    uint32_t seq = 0;
    pid_t    kid;
    int      reads = 0, torn = 0, i;

    printf("a reader looking while a writer is writing\n");

    bm_shm_name(name, sizeof name, 2);
    if (bm_shm_create(&s, name) != 0) { check(0, "create"); return; }

    kid = fork();
    if (kid == 0) {
        bm_shm e;
        static char mine[BM_SHM_TEXT];
        int n;

        if (bm_shm_attach(&e, name) == 0) {
            for (n = 0; n < 4000; n++) {
                make_song(mine, sizeof mine, n);
                bm_shm_publish(&e.block->to_plugin, mine, strlen(mine));
            }
            bm_shm_close(&e);
        }
        _exit(0);
    }

    /* Until the writer is actually finished, rather than for a fixed number of
     * turns. A counted loop finishes long before the child does - the read side
     * is a sequence compare and the write side is a memcpy of 64 KB - so it
     * caught seven songs out of four thousand and would have missed a tear in
     * any of the rest. */
    for (i = 0; i < 20000000; i++) {
        if (bm_shm_take(&s.block->to_plugin, &seq, got, sizeof got, 0)) {
            reads++;
            if (!song_is_whole(got)) torn++;
        }
        if ((i & 0xFFF) == 0 && waitpid(kid, 0, WNOHANG) == kid) break;
    }
    waitpid(kid, 0, 0);

    printf("    %d songs read while one was being written continuously\n", reads);
    check(reads > 0, "the reader saw something");
    check(torn == 0, "and every song it saw was a whole one");

    bm_shm_close(&s);
}

static void test_the_edges(void)
{
    bm_shm   s;
    char     name[BM_SHM_NAME_MAX];
    static char big[BM_SHM_TEXT + 16];
    static char out[BM_SHM_TEXT];
    uint32_t seq = 0;

    printf("edges\n");

    bm_shm_name(name, sizeof name, 3);
    if (bm_shm_create(&s, name) != 0) { check(0, "create"); return; }

    memset(big, 'A', sizeof big);
    check(bm_shm_publish(&s.block->to_plugin, big, sizeof big) == -1,
          "a song too large for the block is refused, not truncated");

    check(bm_shm_take(&s.block->to_plugin, &seq, out, sizeof out, 0) == 0,
          "and nothing is there to read after a refusal");

    bm_shm_publish(&s.block->to_plugin, "hello", 5);
    check(bm_shm_take(&s.block->to_plugin, &seq, out, sizeof out, 0) == 1,
          "a real one reads");
    check(bm_shm_take(&s.block->to_plugin, &seq, out, sizeof out, 0) == 0,
          "and only once - the same song is not delivered twice");

    check(bm_shm_attach(&s, "/bencmouth-nothing-here") != 0,
          "attaching to a block that does not exist fails rather than crashes");

    {
        /* Two plugins must not land on one block. */
        bm_shm a, b;
        char   n2[BM_SHM_NAME_MAX];
        bm_shm_name(n2, sizeof n2, 4);
        check(bm_shm_create(&a, n2) == 0, "one instance creates its block");
        check(bm_shm_create(&b, n2) != 0, "and a second cannot take the name");
        bm_shm_close(&a);
    }
}

int main(void)
{
    printf("\nBENCmouth shared block tests\n\n");
    test_across_a_fork();
    test_a_writer_that_never_stops();
    test_the_edges();
    printf("\n%s (%d failure%s)\n\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
#endif /* !_WIN32 */
