/*
 * BENCmouth - command line interface
 *
 * Drives the public engine API exactly as any other caller would: queue an
 * utterance, then pull PCM with bm_read() until it stops producing. Nothing
 * here reaches past bencmouth.h into the internals, which keeps the CLI an
 * honest test of whether that API is usable.
 */

#include "bencmouth.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 4096

static void usage(void)
{
    printf(
"BENCmouth - a formant speech synthesizer\n"
"\n"
"usage: bm [options] \"text to speak\"\n"
"\n"
"  -o FILE      write WAV here (default out.wav; - for stdout)\n"
"  -v NAME      use a voice preset\n"
"  -f FILE      load a voice file\n"
"  -s SPEED     speech rate; 1.0 nominal, 2.0 twice as fast\n"
"  -p PITCH     base pitch in Hz\n"
"  -P           input is ARPABET phonemes, not text\n"
"  -m           enable inline markup: [pitch N] [speed X] [pause N] [reset]\n"
"  -t           print phonemes and exit; render nothing\n"
"  -w FILE      write the resolved voice to a voice file and exit\n"
"  -l           list voice presets\n"
"  -r RATE      sample rate (default 22050)\n"
"  -h           this help\n"
"\n"
"examples:\n"
"  bm \"hello world\" -o hello.wav\n"
"  bm -v retro -s 0.8 \"I am sorry Dave\"\n"
"  bm -P \"HH AH0 L OW1\" -o hello.wav\n"
"  bm -t \"the quick brown fox\"\n"
"  bm -m \"normal. [pitch 70][speed 0.8] and now slow.\"\n");
}

int main(int argc, char **argv)
{
    bm_engine_storage storage;
    bm_engine        *engine = 0;
    bm_config         config;
    bm_wav_report     report;
    bm_voice          voice;
    char              name_buf[64];
    char              err[192];

    const char *input = 0;
    const char *outpath = "out.wav";
    int         as_phonemes = 0, text_only = 0;
    int         i;

    float  *audio = 0;
    size_t  cap = 0, len = 0;

    bm_config_default(&config);
    voice = config.voice;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(); return 0; }
        else if (strcmp(a, "-l") == 0) {
            int k;
            for (k = 0; k < bm_voice_preset_count(); k++) {
                const bm_voice *v = bm_voice_preset_at(k);
                printf("  %-20s f0 %5.1f +-%.1fst  throat %.2f  mouth %.2f"
                       "  speed %.2f  gain %.2f  coart %.2f  prosody %.2f\n",
                       v->name, (double)v->f0_base, (double)v->f0_range,
                       (double)v->throat, (double)v->mouth, (double)v->speed,
                       (double)v->gain, (double)v->coarticulation,
                       (double)v->prosody);
            }
            return 0;
        }
        else if (strcmp(a, "-P") == 0) as_phonemes = 1;
        else if (strcmp(a, "-m") == 0) config.markup = 1;
        else if (strcmp(a, "-t") == 0) text_only = 1;
        else if (strcmp(a, "-o") == 0 && i + 1 < argc) outpath = argv[++i];
        else if (strcmp(a, "-r") == 0 && i + 1 < argc)
            config.sample_rate = (uint32_t)strtoul(argv[++i], 0, 10);
        else if (strcmp(a, "-s") == 0 && i + 1 < argc)
            voice.speed = (float)atof(argv[++i]);
        else if (strcmp(a, "-p") == 0 && i + 1 < argc)
            voice.f0_base = (float)atof(argv[++i]);
        else if (strcmp(a, "-v") == 0 && i + 1 < argc) {
            const bm_voice *p = bm_voice_preset(argv[++i]);
            if (p == 0) {
                fprintf(stderr, "bm: unknown voice \"%s\"; -l lists presets\n", argv[i]);
                return 1;
            }
            voice = *p;
        }
        else if (strcmp(a, "-f") == 0 && i + 1 < argc) {
            if (bm_voicefile_load(argv[++i], &voice, name_buf, sizeof name_buf,
                                  err, sizeof err) != 0) {
                fprintf(stderr, "bm: %s\n", err);
                return 1;
            }
        }
        else if (strcmp(a, "-w") == 0 && i + 1 < argc) {
            if (bm_voicefile_save(argv[++i], &voice) != 0) {
                fprintf(stderr, "bm: cannot write %s\n", argv[i]);
                return 1;
            }
            return 0;
        }
        else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "bm: unknown option %s (-h for help)\n", a);
            return 1;
        }
        else input = a;
    }

    if (input == 0) { usage(); return 1; }

    if (text_only) {
        char   phonemes[8192];
        size_t n = 0;
        bm_result rc = bm_text_to_phonemes_ex(input, 0, phonemes, sizeof phonemes,
                                              &n, config.markup ? BM_TEXT_MARKUP : 0u);
        if (rc != BM_OK) { fprintf(stderr, "bm: %s\n", bm_strerror(rc)); return 1; }
        printf("%s\n", phonemes);
        return 0;
    }

    config.voice = voice;
    if (bm_engine_init(&storage, &config, &engine) != BM_OK) {
        fprintf(stderr, "bm: cannot initialize engine\n");
        return 1;
    }

    {
        bm_result rc = as_phonemes ? bm_speak_phonemes(engine, input, 0)
                                   : bm_speak_text(engine, input, 0);
        if (rc != BM_OK) {
            fprintf(stderr, "bm: %s\n", bm_strerror(rc));
            return 1;
        }
    }

    /* Pull until the engine stops producing. Growing the buffer here rather
     * than precomputing a length keeps the CLI honest about the streaming
     * interface - a real caller feeding an audio device never knows the total
     * in advance either. */
    while (bm_is_speaking(engine)) {
        size_t got;

        if (len + CHUNK > cap) {
            size_t want = (cap == 0) ? CHUNK * 8 : cap * 2;
            float *grown = (float *)realloc(audio, want * sizeof *grown);
            if (grown == 0) { fprintf(stderr, "bm: out of memory\n"); free(audio); return 1; }
            audio = grown;
            cap = want;
        }

        got = bm_read(engine, audio + len, CHUNK);
        if (got == 0) break;
        len += got;
    }

    if (len == 0) { fprintf(stderr, "bm: nothing to say\n"); free(audio); return 1; }

    if (bm_wav_write(outpath, audio, len, config.sample_rate, &report) != 0) {
        fprintf(stderr, "bm: cannot write %s\n", outpath);
        free(audio);
        return 1;
    }

    if (strcmp(outpath, "-") != 0) {
        fprintf(stderr, "%s  %.2f s  peak %.3f%s  [%s]\n",
                outpath, (double)len / (double)config.sample_rate,
                (double)report.peak, report.limited ? "  LIMITED" : "",
                voice.name ? voice.name : "voice");
    }

    free(audio);
    return 0;
}
