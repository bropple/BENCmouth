/*
 * BENCmouth - command line interface
 *
 * Drives the public engine API exactly as any other caller would: queue an
 * utterance, then pull PCM with bm_read() until it stops producing. Nothing
 * here reaches past bencmouth.h into the internals, which keeps the CLI an
 * honest test of whether that API is usable.
 */

#include "bencmouth.h"
#include "bm_audio.h"
#include "bm_songfile.h"
#include "bm_voicefile.h"
#include "bm_wav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 4096

/* Which pronunciation path this build has. Without the dictionary every word
 * goes through the letter-to-sound rules, which get "robot" as R AA B AA T -
 * correct for the rules and wrong for the word. Worth saying out loud rather
 * than leaving someone to deduce it from what they hear. */
static const char *dict_line(void)
{
    static char buf[64];
    int n = bm_dict_count();
    if (n <= 0) return "letter-to-sound rules only (build with `make dict`)";
    snprintf(buf, sizeof buf, "%d-word dictionary, then the rules", n);
    return buf;
}

static void usage(void)
{
    printf(
"BENCmouth - a formant speech synthesizer\n"
"\n"
"usage: bm [options] \"text to speak\"\n"
"\n"
"  -a           play through the speakers instead of writing a file\n"
"  -o FILE      write WAV here (default out.wav; - for stdout)\n"
"  -v NAME      use a voice preset\n"
"  -e NAME      use an effects preset (none, metal, sentinel, enforcer, ...)\n"
"  -f FILE      load a voice file\n"
"  -R SEED      generate a random voice from SEED\n"
"  -s SPEED     speech rate; 1.0 nominal, 2.0 twice as fast\n"
"  -p PITCH     base pitch in Hz\n"
"  -P           input is ARPABET phonemes, not text\n"
"  -S FILE      sing a .bmsong: its score is the input, its voice the voice\n"
"  -m           enable inline markup: [pitch N] [speed X] [pause N] [reset]\n"
"               and, for singing, [note NAME] [hold MS]\n"
"  -t           print phonemes and exit; render nothing\n"
"  -w FILE      write the resolved voice to a voice file and exit\n"
"  -l           list voice and effects presets\n"
"  -r RATE      sample rate (default 22050)\n"
"  -h           this help\n"
"\n"
"audio backend: %s\n"
"pronunciation: %s\n"
"\n"
"examples:\n"
"  bm \"hello world\" -o hello.wav\n"
"  bm -v retro -s 0.8 \"I am sorry Dave\"\n"
"  bm -P \"HH AH0 L OW1\" -o hello.wav\n"
"  bm -t \"the quick brown fox\"\n"
"  bm -m \"normal. [pitch 70][speed 0.8] and now slow.\"\n"
"  bm -a \"straight out of the speakers\"\n"
"  bm -v deep -e enforcer \"you have thirty seconds to comply\"\n"
"  bm -S songs/daisy.bmsong -a\n"
"  bm -R 4242 -w found.bmvoice  # keep a random voice you liked\n",
        bm_audio_backend(), dict_line());
}

int main(int argc, char **argv)
{
    bm_engine_storage storage;
    bm_engine        *engine = 0;
    bm_config         config;
    bm_wav_report     report;
    bm_voice          voice;
    bm_effects        effects;
    char              name_buf[64];
    char              err[192];

    const char *input = 0;
    const char *outpath = "out.wav";
    int         as_phonemes = 0, text_only = 0, play = 0;
    int         i;

    /* A song's score is the input, so it needs to outlive the parse. Static
     * rather than automatic: 16 KB is a lot of stack on the smaller hosts this
     * CLI is expected to build for. */
    static char song_score[BM_SONG_SCORE_MAX];
    bm_song     song;

    float  *audio = 0;
    size_t  cap = 0, len = 0;

    bm_config_default(&config);
    voice = config.voice;
    effects = config.effects;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(); return 0; }
        else if (strcmp(a, "-l") == 0) {
            int k;
            for (k = 0; k < bm_effects_preset_count(); k++) {
                const bm_effects *x = bm_effects_preset_at(k);
                printf("  -e %-12s ring %.2f  comb %.2f  chorus %.2f"
                       "  drive %.2f  crush %.2f  echo %.2f  reverb %.2f\n",
                       x->name, (double)x->ring, (double)x->comb,
                       (double)x->chorus,
                       (double)x->drive, (double)x->crush,
                       (double)x->echo, (double)x->reverb);
            }
            printf("\n");
            for (k = 0; k < bm_voice_preset_count(); k++) {
                const bm_voice *v = bm_voice_preset_at(k);
                const char *chain = bm_voice_chain(v);
                printf("  %-20s f0 %5.1f +-%.1fst  throat %.2f  mouth %.2f"
                       "  speed %.2f  gain %.2f  coart %.2f  prosody %.2f",
                       v->name, (double)v->f0_base, (double)v->f0_range,
                       (double)v->throat, (double)v->mouth, (double)v->speed,
                       (double)v->gain, (double)v->coarticulation,
                       (double)v->prosody);
                /* Worth saying: -v Sentry quietly brings an effects chain with
                 * it, and a listing that did not mention that would make the
                 * -e column look like it had been ignored. */
                if (chain != 0) printf("  + %s", chain);
                printf("\n");
            }
            return 0;
        }
        else if (strcmp(a, "-P") == 0) as_phonemes = 1;
        else if (strcmp(a, "-m") == 0) config.markup = 1;
        else if (strcmp(a, "-a") == 0) play = 1;
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
            const char     *chain;
            if (p == 0) {
                fprintf(stderr, "bm: unknown voice \"%s\"; -l lists presets\n", argv[i]);
                return 1;
            }
            voice = *p;
            /* Some voices are not the voice without their chain - see
             * bm_voice_chain. A later -e overrides this, which is the ordinary
             * reading of a command line: the last thing you said wins. */
            chain = bm_voice_chain(&voice);
            if (chain != 0) {
                const bm_effects *x = bm_effects_preset(chain);
                if (x != 0) effects = *x;
            }
        }
        else if (strcmp(a, "-e") == 0 && i + 1 < argc) {
            const bm_effects *x = bm_effects_preset(argv[++i]);
            if (x == 0) {
                fprintf(stderr, "bm: unknown effect \"%s\"; -l lists presets\n",
                        argv[i]);
                return 1;
            }
            effects = *x;
        }
        else if (strcmp(a, "-R") == 0 && i + 1 < argc) {
            bm_voice_random(&voice, (uint32_t)strtoul(argv[++i], 0, 10));
        }
        else if (strcmp(a, "-S") == 0 && i + 1 < argc) {
            if (bm_song_load(argv[++i], &song, song_score, sizeof song_score,
                             err, sizeof err) != 0) {
                fprintf(stderr, "bm: %s\n", err);
                return 1;
            }
            /* A song carries its own voice, and singing it in somebody else's
             * is not what was asked for. Later -v or -f still wins, because
             * options later on a command line beat earlier ones everywhere
             * else here too. */
            voice = song.voice;
            effects = song.effects;
            input = song_score;
            as_phonemes = 1;
            /* Markup is the whole mechanism a score is written in - [note] and
             * [hold] are not decoration - so a song turns it on rather than
             * failing obscurely on the first bracket. */
            config.markup = 1;
        }
        else if (strcmp(a, "-f") == 0 && i + 1 < argc) {
            if (bm_voicefile_load(argv[++i], &voice, &effects,
                                  name_buf, sizeof name_buf,
                                  err, sizeof err) != 0) {
                fprintf(stderr, "bm: %s\n", err);
                return 1;
            }
        }
        else if (strcmp(a, "-w") == 0 && i + 1 < argc) {
            if (bm_voicefile_save(argv[++i], &voice, &effects) != 0) {
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
    config.effects = effects;
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

    /* Live playback streams straight from bm_read() to the device, with no
     * buffer of the whole utterance in between. That is the interface working
     * as designed rather than as a convenience: nothing here knows how long the
     * utterance is, and nothing needs to. */
    if (play) {
        bm_audio *dev = 0;
        char      aerr[192];
        float     chunk[CHUNK];
        int       bad = 0;

        if (bm_audio_open(&dev, config.sample_rate, aerr, sizeof aerr) != 0) {
            fprintf(stderr, "bm: %s\n", aerr);
            return 1;
        }
        while (bm_is_speaking(engine)) {
            size_t got = bm_read(engine, chunk, CHUNK);
            if (got == 0) break;
            if (bm_audio_write(dev, chunk, got) != 0) { bad = 1; break; }
        }
        if (!bad) bm_audio_drain(dev);
        bm_audio_close(dev);
        if (bad) { fprintf(stderr, "bm: audio device error\n"); return 1; }
        return 0;
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
        /* Peak as a fraction and loudness in decibels, which is not an
         * inconsistency: peak is read against the limiter, so the useful form
         * is "how close am I to 1", while loudness is only ever compared with
         * another loudness and that comparison is done in dB. Rendering a
         * dozen voices through the same sentence and reading down the column
         * is what this is for - and it is how the effects presets were found
         * to be level-matched while looking four to one apart on peak. */
        fprintf(stderr, "%s  %.2f s  peak %.3f  rms %.1f dB%s  [%s]\n",
                outpath, (double)len / (double)config.sample_rate,
                (double)report.peak,
                report.rms > 0.0f
                    ? 20.0 * log10((double)report.rms) : -99.9,
                report.limited ? "  LIMITED" : "",
                voice.name ? voice.name : "voice");
    }

    free(audio);
    return 0;
}
