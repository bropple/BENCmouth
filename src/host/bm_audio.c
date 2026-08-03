/*
 * BENCmouth - live audio output
 * See bm_audio.h for the interface and why the write is blocking.
 *
 * Three backends in one file rather than three files, because each is small
 * and the alternative is a Makefile that has to know which one to exclude.
 * Exactly one is selected at compile time; the rest are not even parsed.
 */

#include "bm_audio.h"
#include "bm_wav.h"      /* bm_pcm_sample - the same limiting the file gets */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Samples converted per write to the device. Small enough that a Ctrl-C during
 * a long utterance stops promptly, large enough not to syscall constantly. */
#define CHUNK 1024

static void say(char *err, size_t cap, const char *msg)
{
    if (err != 0 && cap > 0) {
        strncpy(err, msg, cap - 1);
        err[cap - 1] = '\0';
    }
}

/* ================================================================== *
 * ALSA
 * ================================================================== */
#if defined(BM_AUDIO_ALSA)

#include <alsa/asoundlib.h>

struct bm_audio { snd_pcm_t *pcm; };

int bm_audio_available(void)     { return 1; }
const char *bm_audio_backend(void) { return "ALSA"; }

int bm_audio_open(bm_audio **out, uint32_t sample_rate, char *err, size_t cap)
{
    bm_audio *a;
    int rc;

    if (out == 0) return -1;

    a = (bm_audio *)calloc(1, sizeof *a);
    if (a == 0) { say(err, cap, "out of memory"); return -1; }

    rc = snd_pcm_open(&a->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        say(err, cap, snd_strerror(rc));
        free(a);
        return -1;
    }

    /* Negotiated with hw_params rather than snd_pcm_set_params.
     *
     * The convenience call takes a latency in microseconds and fails outright
     * if the device cannot land on a period size that matches - which is what
     * happened here first: "Unable to get period size for PLAYBACK: Invalid
     * argument" on a device that plays 22050 Hz mono perfectly well. Asking for
     * a period and buffer *near* what we want lets the driver pick something it
     * can actually do, which is the difference between working on one machine
     * and working on machines generally. */
    {
        snd_pcm_hw_params_t *hw = 0;
        unsigned rate = sample_rate;
        snd_pcm_uframes_t period = CHUNK;
        snd_pcm_uframes_t buffer = CHUNK * 4u;

        /* _malloc rather than the usual _alloca: alloca is not declared under
         * strict C99, and this is not a hot path. */
        if (snd_pcm_hw_params_malloc(&hw) < 0) {
            say(err, cap, "out of memory");
            snd_pcm_close(a->pcm);
            free(a);
            return -1;
        }

        rc = snd_pcm_hw_params_any(a->pcm, hw);
        if (rc >= 0) rc = snd_pcm_hw_params_set_access(a->pcm, hw,
                                                       SND_PCM_ACCESS_RW_INTERLEAVED);
        if (rc >= 0) rc = snd_pcm_hw_params_set_format(a->pcm, hw,
                                                       SND_PCM_FORMAT_S16_LE);
        if (rc >= 0) rc = snd_pcm_hw_params_set_channels(a->pcm, hw, 1);
        if (rc >= 0) rc = snd_pcm_hw_params_set_rate_near(a->pcm, hw, &rate, 0);
        if (rc >= 0) rc = snd_pcm_hw_params_set_period_size_near(a->pcm, hw, &period, 0);
        if (rc >= 0) rc = snd_pcm_hw_params_set_buffer_size_near(a->pcm, hw, &buffer);
        if (rc >= 0) rc = snd_pcm_hw_params(a->pcm, hw);

        snd_pcm_hw_params_free(hw);

        if (rc < 0) {
            say(err, cap, snd_strerror(rc));
            snd_pcm_close(a->pcm);
            free(a);
            return -1;
        }

        /* The device may have chosen a different rate. Saying so is better than
         * quietly playing everything at the wrong pitch. */
        if (rate != sample_rate) {
            fprintf(stderr, "bm: device substituted %u Hz for %u Hz\n",
                    rate, sample_rate);
        }
    }

    *out = a;
    return 0;
}

int bm_audio_write(bm_audio *a, const float *samples, size_t count)
{
    int16_t buf[CHUNK];
    size_t  i = 0;

    if (a == 0 || samples == 0) return -1;

    while (i < count) {
        size_t n = count - i, k;
        snd_pcm_sframes_t wrote;

        if (n > CHUNK) n = CHUNK;
        for (k = 0; k < n; k++) buf[k] = bm_pcm_sample(samples[i + k], 0);

        wrote = snd_pcm_writei(a->pcm, buf, (snd_pcm_uframes_t)n);
        if (wrote < 0) {
            /* Underrun or a suspended device. Recovering and carrying on beats
             * aborting the sentence - the listener would rather hear a glitch
             * than nothing. */
            wrote = snd_pcm_recover(a->pcm, (int)wrote, 1);
            if (wrote < 0) return -1;
            continue;
        }
        i += (size_t)wrote;
    }
    return 0;
}

int bm_audio_drain(bm_audio *a)
{
    if (a == 0) return -1;
    return (snd_pcm_drain(a->pcm) < 0) ? -1 : 0;
}

void bm_audio_close(bm_audio *a)
{
    if (a == 0) return;
    snd_pcm_close(a->pcm);
    free(a);
}

/* ================================================================== *
 * Windows waveOut
 * ================================================================== */
#elif defined(BM_AUDIO_WINMM)

#include <windows.h>
#include <mmsystem.h>

#define NBUF 4

struct bm_audio {
    HWAVEOUT  hwo;
    WAVEHDR   hdr[NBUF];
    int16_t   buf[NBUF][CHUNK];
    int       next;
};

int bm_audio_available(void)     { return 1; }
const char *bm_audio_backend(void) { return "WinMM"; }

int bm_audio_open(bm_audio **out, uint32_t sample_rate, char *err, size_t cap)
{
    bm_audio     *a;
    WAVEFORMATEX  wfx;
    int           i;

    if (out == 0) return -1;

    a = (bm_audio *)calloc(1, sizeof *a);
    if (a == 0) { say(err, cap, "out of memory"); return -1; }

    memset(&wfx, 0, sizeof wfx);
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sample_rate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = sample_rate * 2u;

    if (waveOutOpen(&a->hwo, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        say(err, cap, "cannot open the default audio device");
        free(a);
        return -1;
    }

    for (i = 0; i < NBUF; i++) {
        a->hdr[i].lpData = (LPSTR)a->buf[i];
        a->hdr[i].dwBufferLength = CHUNK * 2u;
        waveOutPrepareHeader(a->hwo, &a->hdr[i], sizeof a->hdr[i]);
        /* Mark as already played, so the first pass finds every buffer free. */
        a->hdr[i].dwFlags |= WHDR_DONE;
    }

    *out = a;
    return 0;
}

int bm_audio_write(bm_audio *a, const float *samples, size_t count)
{
    size_t i = 0;

    if (a == 0 || samples == 0) return -1;

    while (i < count) {
        WAVEHDR *h = &a->hdr[a->next];
        size_t   n = count - i, k;

        while ((h->dwFlags & WHDR_DONE) == 0) Sleep(1);

        if (n > CHUNK) n = CHUNK;
        for (k = 0; k < n; k++) a->buf[a->next][k] = bm_pcm_sample(samples[i + k], 0);

        h->dwBufferLength = (DWORD)(n * 2u);
        h->dwFlags &= ~WHDR_DONE;
        if (waveOutWrite(a->hwo, h, sizeof *h) != MMSYSERR_NOERROR) return -1;

        a->next = (a->next + 1) % NBUF;
        i += n;
    }
    return 0;
}

int bm_audio_drain(bm_audio *a)
{
    int i;
    if (a == 0) return -1;
    for (i = 0; i < NBUF; i++) {
        while ((a->hdr[i].dwFlags & WHDR_DONE) == 0) Sleep(1);
    }
    return 0;
}

void bm_audio_close(bm_audio *a)
{
    int i;
    if (a == 0) return;
    for (i = 0; i < NBUF; i++) waveOutUnprepareHeader(a->hwo, &a->hdr[i], sizeof a->hdr[i]);
    waveOutClose(a->hwo);
    free(a);
}

/* ================================================================== *
 * macOS AudioQueue
 * ================================================================== */
#elif defined(BM_AUDIO_COREAUDIO)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#define NBUF 4

struct bm_audio {
    AudioQueueRef       queue;
    AudioQueueBufferRef buf[NBUF];
    volatile int        free_flag[NBUF];
    int                 next;
    int                 started;
};

int bm_audio_available(void)     { return 1; }
const char *bm_audio_backend(void) { return "CoreAudio"; }

/* AudioQueue hands a buffer back here when it has finished with it. The writer
 * pumps the run loop to make sure these actually fire. */
static void on_buffer_done(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
    bm_audio *a = (bm_audio *)user;
    int i;
    (void)q;
    for (i = 0; i < NBUF; i++) {
        if (a->buf[i] == b) { a->free_flag[i] = 1; return; }
    }
}

int bm_audio_open(bm_audio **out, uint32_t sample_rate, char *err, size_t cap)
{
    bm_audio                 *a;
    AudioStreamBasicDescription fmt;
    int i;

    if (out == 0) return -1;

    a = (bm_audio *)calloc(1, sizeof *a);
    if (a == 0) { say(err, cap, "out of memory"); return -1; }

    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate = (Float64)sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                       kLinearPCMFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 16;
    fmt.mBytesPerFrame = 2;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = 2;

    if (AudioQueueNewOutput(&fmt, on_buffer_done, a, NULL, NULL, 0, &a->queue) != noErr) {
        say(err, cap, "cannot create the output audio queue");
        free(a);
        return -1;
    }

    for (i = 0; i < NBUF; i++) {
        if (AudioQueueAllocateBuffer(a->queue, CHUNK * 2u, &a->buf[i]) != noErr) {
            say(err, cap, "cannot allocate audio buffers");
            AudioQueueDispose(a->queue, true);
            free(a);
            return -1;
        }
        a->free_flag[i] = 1;
    }

    *out = a;
    return 0;
}

int bm_audio_write(bm_audio *a, const float *samples, size_t count)
{
    size_t i = 0;

    if (a == 0 || samples == 0) return -1;

    while (i < count) {
        AudioQueueBufferRef b;
        int16_t *dst;
        size_t   n = count - i, k;

        /* Service completion callbacks, then wait for a free buffer. */
        while (!a->free_flag[a->next]) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
        }

        b = a->buf[a->next];
        dst = (int16_t *)b->mAudioData;
        if (n > CHUNK) n = CHUNK;
        for (k = 0; k < n; k++) dst[k] = bm_pcm_sample(samples[i + k], 0);

        b->mAudioDataByteSize = (UInt32)(n * 2u);
        a->free_flag[a->next] = 0;
        if (AudioQueueEnqueueBuffer(a->queue, b, 0, NULL) != noErr) return -1;

        if (!a->started) {
            if (AudioQueueStart(a->queue, NULL) != noErr) return -1;
            a->started = 1;
        }

        a->next = (a->next + 1) % NBUF;
        i += n;
    }
    return 0;
}

int bm_audio_drain(bm_audio *a)
{
    if (a == 0) return -1;
    if (!a->started) return 0;
    /* Blocking stop: waits for everything enqueued to actually play. */
    return (AudioQueueStop(a->queue, true) == noErr) ? 0 : -1;
}

void bm_audio_close(bm_audio *a)
{
    if (a == 0) return;
    AudioQueueDispose(a->queue, true);
    free(a);
}

/* ================================================================== *
 * No backend
 * ================================================================== */
#else

struct bm_audio { int unused; };

int bm_audio_available(void)     { return 0; }
const char *bm_audio_backend(void) { return "none"; }

int bm_audio_open(bm_audio **out, uint32_t sample_rate, char *err, size_t cap)
{
    (void)out; (void)sample_rate;
    say(err, cap, "built without audio output; rebuild with `make audio`");
    return -1;
}

int bm_audio_write(bm_audio *a, const float *samples, size_t count)
{
    (void)a; (void)samples; (void)count;
    return -1;
}

int bm_audio_drain(bm_audio *a) { (void)a; return -1; }
void bm_audio_close(bm_audio *a) { (void)a; }

#endif
