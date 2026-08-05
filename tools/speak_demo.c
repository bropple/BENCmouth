/*
 * BENCmouth - phoneme-string demo
 *
 * Phonemes in, words out. Still no text front end - you supply ARPABET
 * directly, exactly as bm_speak_phonemes() will once the engine layer exists.
 *
 *   ./speak_demo                              render the built-in set
 *   ./speak_demo "HH AH0 L OW1" out.wav       one utterance, default voice
 *   ./speak_demo -v deep "HH AH0 L OW1" o.wav a named preset
 *   ./speak_demo -f Gravel.bmvoice "..." o.wav  a voice file
 *   ./speak_demo -l                           list presets
 */

#include "bm_frames.h"
#include "bm_synth.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 22050u
#define FRAME_HZ    100.0f
#define TAIL_MS     120

static int render_utterance(const char *phonemes, const char *path,
                            const bm_voice *voice)
{
    bm_frame_gen  gen;
    bm_synth      synth;
    bm_frame      frame;
    bm_wav_report report;
    bm_result     rc;
    float        *out;
    size_t        spf, tail, total, pos = 0;
    int           nframes;

    bm_frame_gen_init(&gen, FRAME_HZ, voice);

    rc = bm_frame_gen_set_phonemes(&gen, phonemes, 0);
    if (rc != BM_OK) {
        fprintf(stderr, "  %s: %s\n", phonemes, bm_strerror(rc));
        return -1;
    }

    spf = SAMPLE_RATE / (unsigned)FRAME_HZ;
    nframes = bm_frame_gen_length(&gen);
    tail = (size_t)TAIL_MS * SAMPLE_RATE / 1000u;
    total = (size_t)nframes * spf + tail;

    out = (float *)calloc(total, sizeof *out);
    if (out == 0) return -1;

    bm_synth_init(&synth, (float)SAMPLE_RATE);
    bm_synth_set_flutter(&synth, voice->f0_flutter);
    bm_synth_set_gain(&synth, voice->gain);

    while (bm_frame_gen_next(&gen, &frame)) {
        size_t i;
        bm_synth_set_frame(&synth, &frame);
        for (i = 0; i < spf && pos < total; i++) out[pos++] = bm_synth_tick(&synth);
    }
    /* Let the filters ring out into the tail rather than cutting them off. */
    while (pos < total) out[pos++] = bm_synth_tick(&synth);

    if (bm_wav_write(path, out, total, SAMPLE_RATE, &report) != 0) {
        fprintf(stderr, "  failed to write %s\n", path);
        free(out);
        return -1;
    }

    printf("  %-34s %5.2f s  peak %.3f%s\n",
           path, (double)total / (double)SAMPLE_RATE,
           (double)report.peak, report.limited ? "  LIMITED" : "");

    free(out);
    return 0;
}

static void list_presets(void)
{
    int i;

    printf("\npresets:\n");
    for (i = 0; i < bm_voice_preset_count(); i++) {
        const bm_voice *v = bm_voice_preset_at(i);
        printf("  %-20s f0 %5.1f  throat %.2f  mouth %.2f  tilt %.1f"
               "  gain %.2f  coart %.2f\n",
               v->name, (double)v->f0_base, (double)v->throat,
               (double)v->mouth, (double)v->tilt, (double)v->gain,
               (double)v->coarticulation);
    }
    printf("\n");
}

static int render_default_set(void)
{
    static const struct { const char *phonemes; const char *path; } SET[] = {
        { "HH AH0 L OW1",                         "render/hello.wav" },
        { "HH AH0 L OW1 SIL W ER1 L D",           "render/hello-world.wav" },
        { "B EH1 N K M AW1 TH",                   "render/bencmouth.wav" },
        { "S IH1 K S T IY0 F AO1 R",              "render/sixty-four.wav" },
        { "DH AH0 K W IH1 K B R AW1 N F AA1 K S", "render/quick-brown-fox.wav" },
        { "AY1 AE1 M S AO1 R IY0 D EY1 V",        "render/sorry-dave.wav" }
    };
    /* Long enough that voice differences are obvious rather than subtle. */
    static const char *COMPARE =
        "DH AH0 K W IH1 K B R AW1 N F AA1 K S SIL "
        "JH AH1 M P S OW1 V ER0 DH AH0 L EY1 Z IY0 D AO1 G";

    bm_voice voice;
    char     path[128];
    int      i, bad = 0;

    bm_voice_default(&voice);

    printf("\nBENCmouth phoneme demo (voice: %s)\n\n", voice.name);
    for (i = 0; i < (int)(sizeof SET / sizeof SET[0]); i++) {
        if (render_utterance(SET[i].phonemes, SET[i].path, &voice) != 0) bad++;
    }

    printf("\nvoice comparison\n\n");
    for (i = 0; i < bm_voice_preset_count(); i++) {
        const bm_voice *v = bm_voice_preset_at(i);
        const char *n = v->name;
        char  slug[64];
        int   j = 0;

        /* "BENCmouth Retro" -> "bencmouth-retro" */
        while (*n != '\0' && j < (int)sizeof slug - 1) {
            char c = *n++;
            if (c == ' ') c = '-';
            else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            slug[j++] = c;
        }
        slug[j] = '\0';

        snprintf(path, sizeof path, "render/voice-%s.wav", slug);
        if (render_utterance(COMPARE, path, v) != 0) bad++;
    }

    /* Coarticulation is a parameter, not a policy - this pair is the audible
     * proof that Retro can keep its edges however natural the rest gets. */
    printf("\ncoarticulation, on the same voice\n\n");
    voice.coarticulation = 0.0f;
    if (render_utterance(COMPARE, "render/coart-off.wav", &voice) != 0) bad++;
    voice.coarticulation = 0.9f;
    if (render_utterance(COMPARE, "render/coart-on.wav", &voice) != 0) bad++;

    printf("\n");
    return bad;
}

int main(int argc, char **argv)
{
    bm_voice   voice;
    /* Accepted and round-tripped, but not applied - this demo renders through
     * the frame generator and synthesizer directly rather than through the
     * engine, and the effects stage lives with the engine's output. Present so
     * that -f on a voice file carrying an effects block does not fail. */
    bm_effects effects;
    char     name_buf[64];
    char     err[160];
    int      i;
    const char *phonemes = 0, *path = "render/speak.wav";

    bm_voice_default(&voice);
    bm_effects_default(&effects);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            list_presets();
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            const bm_voice *p = bm_voice_preset(argv[++i]);
            if (p == 0) {
                fprintf(stderr, "unknown voice \"%s\"; -l lists presets\n", argv[i]);
                return 1;
            }
            voice = *p;
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            /* Dump the current voice, so a preset or a tuned voice can be
             * captured to a file and kept. */
            if (bm_voicefile_save(argv[++i], &voice, &effects) != 0) {
                fprintf(stderr, "cannot write %s\n", argv[i]);
                return 1;
            }
            printf("wrote %s\n", argv[i]);
            return 0;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            if (bm_voicefile_load(argv[++i], &voice, &effects,
                                  name_buf, sizeof name_buf,
                                  err, sizeof err) != 0) {
                fprintf(stderr, "%s: %s\n", argv[i], err);
                return 1;
            }
        } else if (phonemes == 0) {
            phonemes = argv[i];
        } else {
            path = argv[i];
        }
    }

    if (phonemes == 0) return render_default_set() ? 1 : 0;
    return render_utterance(phonemes, path, &voice) == 0 ? 0 : 1;
}
