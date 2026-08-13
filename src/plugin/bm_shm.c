/*
 * BENCmouth - the block the plugin and its editor share
 * See bm_shm.h for what crosses it and why it is a separate process at all.
 */

/* Declared here rather than passed on the command line. The project builds with
 * -std=c99, which is strict ISO C and hides shm_open, mmap and ftruncate behind
 * a feature test - so a file that needs them has to say so itself, or every
 * target that compiles it has to remember to. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "bm_shm.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

/* The barrier lives in one place, because the player needs the same one for
 * the same reason - see src/host/bm_fence.h, which is also where the story of
 * how this was got wrong is written down. Included by a relative path so that
 * this file goes on depending on nothing but a compiler. */
#include "../host/bm_fence.h"

#define BM_BARRIER() BM_FENCE()

void bm_shm_name(char *out, size_t cap, unsigned counter)
{
    unsigned long pid;

#if defined(_WIN32)
    pid = (unsigned long)GetCurrentProcessId();
#else
    pid = (unsigned long)getpid();
#endif

    /* A leading slash is what shm_open wants and what Windows tolerates as an
     * object name. One name per instance, so two copies of the plugin in one
     * project do not talk to each other's editors. */
    snprintf(out, cap, "/bencmouth-%lu-%u", pid, counter);
}

/* ------------------------------------------------------------------ */

static int map(bm_shm *s, int create)
{
#if defined(_WIN32)
    HANDLE h;

    if (create) {
        h = CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0,
                               (DWORD)sizeof(bm_shm_block), s->name + 1);
        /* An existing name means another instance is already using it, which
         * means the name was not unique - and sharing it would be two plugins
         * writing one block. */
        if (h != 0 && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(h);
            return -1;
        }
    } else {
        h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, s->name + 1);
    }
    if (h == 0) return -1;

    s->block = (bm_shm_block *)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0,
                                             sizeof(bm_shm_block));
    if (s->block == 0) { CloseHandle(h); return -1; }
    s->handle = (void *)h;
    return 0;
#else
    int flags = create ? (O_CREAT | O_EXCL | O_RDWR) : O_RDWR;

    s->fd = shm_open(s->name, flags, 0600);
    if (s->fd < 0) return -1;

    if (create && ftruncate(s->fd, (off_t)sizeof(bm_shm_block)) != 0) {
        close(s->fd);
        shm_unlink(s->name);
        s->fd = -1;
        return -1;
    }

    s->block = (bm_shm_block *)mmap(0, sizeof(bm_shm_block),
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    s->fd, 0);
    if (s->block == MAP_FAILED) {
        s->block = 0;
        close(s->fd);
        if (create) shm_unlink(s->name);
        s->fd = -1;
        return -1;
    }
    return 0;
#endif
}

int bm_shm_create(bm_shm *s, const char *name)
{
    if (s == 0 || name == 0) return -1;

    memset(s, 0, sizeof *s);
    s->fd = -1;
    snprintf(s->name, sizeof s->name, "%s", name);

    if (map(s, 1) != 0) return -1;

    memset(s->block, 0, sizeof *s->block);
    s->block->abi = BM_SHM_ABI;
    BM_BARRIER();
    /* The magic goes in last, so a reader that attaches while this is running
     * sees a block that is not ready rather than one that is half ready. */
    s->block->magic = BM_SHM_MAGIC;
    s->owner = 1;
    return 0;
}

int bm_shm_attach(bm_shm *s, const char *name)
{
    if (s == 0 || name == 0) return -1;

    memset(s, 0, sizeof *s);
    s->fd = -1;
    snprintf(s->name, sizeof s->name, "%s", name);

    if (map(s, 0) != 0) return -1;

    if (s->block->magic != BM_SHM_MAGIC || s->block->abi != BM_SHM_ABI) {
        bm_shm_close(s);
        return -1;
    }
    return 0;
}

void bm_shm_close(bm_shm *s)
{
    if (s == 0) return;

#if defined(_WIN32)
    if (s->block != 0) UnmapViewOfFile(s->block);
    if (s->handle != 0) CloseHandle((HANDLE)s->handle);
    s->handle = 0;
#else
    if (s->block != 0) munmap(s->block, sizeof(bm_shm_block));
    if (s->fd >= 0) close(s->fd);
    /* Only the end that created it removes the name. On POSIX the mapping
     * survives the unlink for anyone who already has it open, which is what
     * makes it safe to do this while an editor is still attached. */
    if (s->owner) shm_unlink(s->name);
    s->fd = -1;
#endif
    s->block = 0;
    s->owner = 0;
}

/* ------------------------------------------------------------------ */

int bm_shm_publish(bm_shm_channel *c, const char *text, size_t len)
{
    if (c == 0 || text == 0) return -1;
    if (len > BM_SHM_TEXT) return -1;

    c->seq++;                    /* odd: a reader now knows to wait */
    BM_BARRIER();

    memcpy(c->text, text, len);
    c->len = (uint32_t)len;

    BM_BARRIER();
    c->seq++;                    /* even, and different from what it was */
    return 0;
}

int bm_shm_take(bm_shm_channel *c, uint32_t *last_seq,
                char *out, size_t cap, size_t *len)
{
    uint32_t before, after;
    uint32_t n;

    if (c == 0 || last_seq == 0 || out == 0 || cap == 0) return 0;

    before = c->seq;
    if ((before & 1u) != 0u) return 0;        /* mid-write; ask again later */
    if (before == *last_seq) return 0;        /* nothing new */

    BM_BARRIER();
    n = c->len;
    if (n > BM_SHM_TEXT) return 0;
    if ((size_t)n >= cap) n = (uint32_t)(cap - 1);
    memcpy(out, c->text, n);
    BM_BARRIER();

    after = c->seq;
    /* Written while being read. The copy may be two halves of two different
     * songs, so it is thrown away rather than parsed - and the sequence is
     * left alone, so the next poll takes the new one. */
    if (after != before) return 0;

    out[n] = '\0';
    if (len != 0) *len = (size_t)n;
    *last_seq = before;
    return 1;
}
