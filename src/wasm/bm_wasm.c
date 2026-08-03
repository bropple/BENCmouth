/*
 * BENCmouth - WebAssembly entry points
 *
 * A bare wasm32 build with no libc at all - not Emscripten. That is possible
 * only because the core was written freestanding from the start: no stdio, no
 * stdlib, no math, no string, and no allocation. `make check-freestanding`
 * has been asserting exactly this since the beginning, and this is what it
 * was for.
 *
 * The consequence is a very small module. There is no runtime to ship, no
 * emulated filesystem, and no generated glue beyond the few dozen lines in
 * bencmouth.js.
 *
 * Memory model: everything the engine needs is a static buffer here, in the
 * module's linear memory. JavaScript writes text into the text buffer, calls
 * speak, then repeatedly calls read and copies samples out of the output
 * buffer. Nothing is allocated at any point, which is the same property that
 * makes the library work on a microcontroller.
 */

#include "bencmouth.h"

/* ------------------------------------------------------------------ *
 * Freestanding builtins
 *
 * -nostdlib means nothing provides these, but a compiler may still emit calls
 * to them for structure assignment or array initialisation. They are tiny and
 * providing them is far simpler than trying to talk the optimiser out of
 * wanting them.
 * ------------------------------------------------------------------ */

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);
void *memmove(void *dst, const void *src, unsigned long n);

void *memcpy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;
    unsigned long i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

void *memmove(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    unsigned long i;

    if (d == s || n == 0) return dst;
    if (d < s) {
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

/* ------------------------------------------------------------------ *
 * State
 * ------------------------------------------------------------------ */

#define BM_WASM_CHUNK 4096

static bm_engine_storage g_storage;
static bm_engine        *g_engine;
static bm_config         g_config;
static char              g_text[BM_MAX_TEXT];
static float             g_out[BM_WASM_CHUNK];
static int               g_ready;

/* ------------------------------------------------------------------ *
 * Exports. Kept deliberately C-like and pointer-free at the boundary -
 * JavaScript sees integers and offsets into linear memory, which is the only
 * thing it can see anyway.
 * ------------------------------------------------------------------ */

int bm_wasm_init(int sample_rate)
{
    bm_config_default(&g_config);
    if (sample_rate > 0) g_config.sample_rate = (uint32_t)sample_rate;

    if (bm_engine_init(&g_storage, &g_config, &g_engine) != BM_OK) {
        g_ready = 0;
        return 0;
    }
    g_ready = 1;
    return 1;
}

/* Offset of the text buffer in linear memory. JS writes UTF-8 bytes here. */
char *bm_wasm_text_buffer(void)   { return g_text; }
int   bm_wasm_text_capacity(void) { return (int)sizeof g_text; }

/* Offset and capacity of the sample buffer, in floats. */
float *bm_wasm_output_buffer(void)   { return g_out; }
int    bm_wasm_output_capacity(void) { return BM_WASM_CHUNK; }

int bm_wasm_set_voice(const char *name)
{
    const bm_voice *v;
    if (!g_ready || name == 0) return 0;
    v = bm_voice_preset(name);
    if (v == 0) return 0;
    return bm_engine_set_voice(g_engine, v) == BM_OK;
}

/* Applies one voice parameter by name, so a page can put sliders on the voice
 * without the module needing to know what a slider is. */
int bm_wasm_set_param(const char *key, float value)
{
    bm_voice v;
    if (!g_ready || key == 0) return 0;
    v = g_config.voice;
    if (bm_voice_set_param(&v, key, 0, value) != BM_OK) return 0;
    g_config.voice = v;
    return bm_engine_set_voice(g_engine, &v) == BM_OK;
}

int bm_wasm_set_markup(int on)
{
    g_config.markup = on ? 1 : 0;
    return 1;
}

/* Queues whatever is currently in the text buffer. Returns 1 on success, or 0
 * with nothing queued - the caller can then ask for the phonemes to find out
 * what the front end made of it. */
int bm_wasm_speak(int len)
{
    if (!g_ready || len < 0 || (unsigned)len > sizeof g_text) return 0;
    /* Re-init the config so a markup toggle since the last call takes effect. */
    if (bm_engine_set_voice(g_engine, &g_config.voice) != BM_OK) return 0;
    return bm_speak_text(g_engine, g_text, (size_t)len) == BM_OK;
}

int bm_wasm_speak_phonemes(int len)
{
    if (!g_ready || len < 0 || (unsigned)len > sizeof g_text) return 0;
    return bm_speak_phonemes(g_engine, g_text, (size_t)len) == BM_OK;
}

/* Converts the text buffer to phonemes in place. Returns the length, or -1.
 * Useful on its own: it is the whole front end without rendering a sample. */
int bm_wasm_to_phonemes(int len)
{
    char   scratch[BM_MAX_TEXT * 3];
    size_t n = 0;
    int    i;

    if (len < 0 || (unsigned)len > sizeof g_text) return -1;
    if (bm_text_to_phonemes_ex(g_text, (size_t)len, scratch, sizeof scratch, &n,
                               g_config.markup ? BM_TEXT_MARKUP : 0u) != BM_OK) {
        return -1;
    }
    if (n >= sizeof g_text) return -1;
    for (i = 0; i < (int)n; i++) g_text[i] = scratch[i];
    g_text[n] = '\0';
    return (int)n;
}

/* Renders up to `max` samples into the output buffer. Returns how many. */
int bm_wasm_read(int max)
{
    if (!g_ready || max <= 0) return 0;
    if (max > BM_WASM_CHUNK) max = BM_WASM_CHUNK;
    return (int)bm_read(g_engine, g_out, (size_t)max);
}

int bm_wasm_speaking(void)   { return g_ready && bm_is_speaking(g_engine); }
int bm_wasm_sample_rate(void) { return (int)g_config.sample_rate; }
int bm_wasm_engine_size(void) { return (int)bm_engine_size(); }
