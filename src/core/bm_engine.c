/*
 * BENCmouth - engine
 *
 * Implements the public API in bencmouth.h. Mostly plumbing: the frame
 * generator and the synthesizer already do the work, and this holds them
 * together and exposes a pull interface.
 *
 * No allocation anywhere. The caller supplies a bm_engine_storage, this places
 * the real struct inside it, and the static assertion below fails the build if
 * it ever stops fitting - which is a much better failure than a silent overrun
 * on a microcontroller.
 */

#include "bencmouth.h"
#include "bm_frames.h"
#include "bm_synth.h"
#include "bm_text.h"

#include <stddef.h>

/* Silence rendered after the last frame so the filters ring out instead of
 * being cut off mid-decay. Enough for the resonators, which are done in a few
 * tens of milliseconds; the effects chain can want far more and says so - see
 * bm_effects_tail_ms. */
#define BM_TAIL_MS 100u

struct bm_engine {
    bm_config    config;
    bm_frame_gen gen;
    bm_synth     synth;

    size_t samples_per_frame;
    size_t samples_left;   /* remaining in the current frame */
    size_t tail_left;
    int    active;

    /* Scratch for text -> phonemes. Held here rather than on the stack: a
     * multi-kilobyte stack frame is fine on a desktop and not fine on a
     * microcontroller, and the engine's footprint is already accounted for. */
    char phonemes[BM_MAX_TEXT * 3];
};

/* If this fails to compile, raise BM_ENGINE_RESERVED in bencmouth.h. It is
 * deliberately a build error: the alternative is discovering the overrun at
 * runtime on the target with the least memory. */
typedef char bm_engine_fits_in_storage[
    (sizeof(struct bm_engine) <= BM_ENGINE_RESERVED) ? 1 : -1];

size_t bm_engine_size(void)
{
    return sizeof(struct bm_engine);
}

/* ------------------------------------------------------------------ */

static bm_sample to_sample(float x)
{
#if BM_SAMPLE_FLOAT
    return x;
#else
    /* Hard clip at the boundary. The host layer's soft limiter is the better
     * answer, but an integer build has to land somewhere finite. */
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    return (bm_sample)(x * 32767.0f);
#endif
}

static void recompute_rates(struct bm_engine *e)
{
    uint32_t fr = (e->config.frame_rate > 0u) ? e->config.frame_rate : 100u;
    uint32_t sr = (e->config.sample_rate > 0u) ? e->config.sample_rate : 22050u;

    e->samples_per_frame = (size_t)(sr / fr);
    if (e->samples_per_frame == 0) e->samples_per_frame = 1;
}

bm_result bm_engine_init(bm_engine_storage *storage, const bm_config *config,
                         bm_engine **out)
{
    struct bm_engine *e;
    bm_config         cfg;

    if (storage == 0 || out == 0) return BM_ERR_ARG;

    if (config != 0) {
        cfg = *config;
    } else {
        bm_config_default(&cfg);
    }
    if (cfg.sample_rate == 0u || cfg.frame_rate == 0u) return BM_ERR_ARG;
    if (cfg.frame_rate > cfg.sample_rate) return BM_ERR_ARG;

    e = (struct bm_engine *)(void *)storage;
    e->config = cfg;

    recompute_rates(e);
    bm_synth_init(&e->synth, (float)cfg.sample_rate);
    bm_synth_set_flutter(&e->synth, cfg.voice.f0_flutter);
    bm_synth_set_vibrato(&e->synth, cfg.voice.vibrato, cfg.voice.vibrato_rate);
    bm_synth_set_source(&e->synth, cfg.voice.source);
    bm_synth_set_gain(&e->synth, cfg.voice.gain);
    bm_synth_set_effects(&e->synth, &cfg.effects);
    bm_frame_gen_init(&e->gen, (float)cfg.frame_rate, &cfg.voice);

    e->samples_left = 0;
    e->tail_left = 0;
    e->active = 0;

    *out = e;
    return BM_OK;
}

void bm_engine_reset(bm_engine *engine)
{
    if (engine == 0) return;

    bm_synth_reset(&engine->synth);
    bm_frame_gen_reset(&engine->gen);
    engine->samples_left = 0;
    engine->tail_left = 0;
    engine->active = 0;
}

bm_result bm_engine_set_voice(bm_engine *engine, const bm_voice *voice)
{
    if (engine == 0 || voice == 0) return BM_ERR_ARG;

    engine->config.voice = *voice;
    bm_synth_set_flutter(&engine->synth, voice->f0_flutter);
    bm_synth_set_vibrato(&engine->synth, voice->vibrato, voice->vibrato_rate);
    bm_synth_set_source(&engine->synth, voice->source);
    bm_synth_set_gain(&engine->synth, voice->gain);

    /* The frame generator holds its own copy, so hand it the new one. Anything
     * already queued keeps its timing; the change lands on the next frame,
     * which is a boundary and therefore will not click. */
    engine->gen.voice = *voice;
    return BM_OK;
}

/* ------------------------------------------------------------------ */

static void begin_utterance(struct bm_engine *e)
{
    bm_synth_reset(&e->synth);
    e->samples_left = 0;
    {
        /* Whichever is longer: the fixed ring-out for the vocal tract, or what
         * the effects chain says it needs. Without this an echo was cut off
         * inside its first repeat and a rendered file was exactly as long with
         * the effect as without it, which is the kind of bug that reads as the
         * effect not working. */
        unsigned ms = BM_TAIL_MS;
        unsigned fx = bm_effects_tail_ms(&e->config.effects);
        if (fx > ms) ms = fx;
        e->tail_left = (size_t)ms * e->config.sample_rate / 1000u;
    }
    e->active = 1;
}

bm_result bm_speak_phonemes(bm_engine *engine, const char *phonemes, size_t len)
{
    bm_result rc;

    if (engine == 0 || phonemes == 0) return BM_ERR_ARG;

    rc = bm_frame_gen_set_phonemes(&engine->gen, phonemes, len);
    if (rc != BM_OK) return rc;

    begin_utterance(engine);
    return BM_OK;
}

bm_result bm_engine_set_effects(bm_engine *engine, const bm_effects *effects)
{
    if (engine == 0 || effects == 0) return BM_ERR_ARG;

    engine->config.effects = *effects;
    /* No reset: the delay line and the carrier phase carry on. Clearing them
     * would make every slider movement a click, and the whole point of the
     * pull interface is that a control moved mid-sentence is audible in that
     * sentence. */
    bm_synth_set_effects(&engine->synth, effects);
    return BM_OK;
}

bm_result bm_engine_set_dictionary(bm_engine *engine, int enabled)
{
    if (engine == 0) return BM_ERR_ARG;
    engine->config.use_dict = enabled ? 1 : 0;
    return BM_OK;
}

bm_result bm_speak_text(bm_engine *engine, const char *text, size_t len)
{
    size_t    n = 0;
    bm_result rc;

    if (engine == 0 || text == 0) return BM_ERR_ARG;

    rc = bm_text_to_phonemes_ex(text, len, engine->phonemes,
                                sizeof engine->phonemes, &n,
                                (engine->config.markup ? BM_TEXT_MARKUP : 0u) |
                                (engine->config.use_dict ? 0u : BM_TEXT_NO_DICT));
    if (rc != BM_OK) return rc;
    if (n == 0) return BM_ERR_ARG;

    return bm_speak_phonemes(engine, engine->phonemes, n);
}

size_t bm_read(bm_engine *engine, bm_sample *out, size_t max_samples)
{
    size_t written = 0;

    if (engine == 0 || out == 0) return 0;
    if (!engine->active) return 0;

    while (written < max_samples) {
        if (engine->samples_left == 0) {
            bm_frame f;

            if (bm_frame_gen_next(&engine->gen, &f)) {
                bm_synth_set_frame(&engine->synth, &f);
                engine->samples_left = engine->samples_per_frame;
            } else if (engine->tail_left > 0) {
                engine->samples_left = engine->tail_left;
                engine->tail_left = 0;
            } else {
                engine->active = 0;
                break;
            }
        }

        out[written++] = to_sample(bm_synth_tick(&engine->synth));
        engine->samples_left--;
    }

    return written;
}

int bm_is_speaking(const bm_engine *engine)
{
    return (engine != 0) && engine->active;
}

size_t bm_render_frame(bm_engine *engine, const bm_frame *frame,
                       bm_sample *out, size_t max_samples)
{
    size_t n, i;

    if (engine == 0 || frame == 0 || out == 0) return 0;

    bm_synth_set_frame(&engine->synth, frame);

    n = engine->samples_per_frame;
    if (n > max_samples) n = max_samples;

    for (i = 0; i < n; i++) out[i] = to_sample(bm_synth_tick(&engine->synth));
    return n;
}
