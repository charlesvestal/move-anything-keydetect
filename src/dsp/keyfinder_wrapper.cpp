/*
 * keyfinder_wrapper.cpp - C++ wrapper around libkeyfinder
 *
 * Implements the plain C API defined in keyfinder_wrapper.h using
 * libkeyfinder's progressive analysis pipeline.
 */

#include "keyfinder_wrapper.h"

#include <keyfinder/keyfinder.h>
#include <keyfinder/audiodata.h>
#include <keyfinder/constants.h>

#include <cstring>
#include <cstdlib>
#include <vector>

/* Key enum to display string mapping */
static const char* key_names[] = {
    "A maj",   /* A_MAJOR = 0 */
    "A min",   /* A_MINOR */
    "Bb maj",  /* B_FLAT_MAJOR */
    "Bb min",  /* B_FLAT_MINOR */
    "B maj",   /* B_MAJOR */
    "B min",   /* B_MINOR = 5 */
    "C maj",   /* C_MAJOR */
    "C min",   /* C_MINOR */
    "Db maj",  /* D_FLAT_MAJOR */
    "Db min",  /* D_FLAT_MINOR */
    "D maj",   /* D_MAJOR = 10 */
    "D min",   /* D_MINOR */
    "Eb maj",  /* E_FLAT_MAJOR */
    "Eb min",  /* E_FLAT_MINOR */
    "E maj",   /* E_MAJOR */
    "E min",   /* E_MINOR = 15 */
    "F maj",   /* F_MAJOR */
    "F min",   /* F_MINOR */
    "Gb maj",  /* G_FLAT_MAJOR */
    "Gb min",  /* G_FLAT_MINOR */
    "G maj",   /* G_MAJOR = 20 */
    "G min",   /* G_MINOR */
    "Ab maj",  /* A_FLAT_MAJOR */
    "Ab min",  /* A_FLAT_MINOR */
    "---"      /* SILENCE = 24 */
};

struct kd_context {
    KeyFinder::KeyFinder keyfinder;
    KeyFinder::Workspace workspace;
    int sample_rate;
    float window_seconds;
    int window_samples;         /* window_seconds * sample_rate */
    std::vector<double> buffer; /* mono audio accumulation buffer */
    char detected_key[16];      /* current key string */
};

extern "C" {

void* kd_create(int sample_rate) {
    kd_context *ctx = new (std::nothrow) kd_context();
    if (!ctx) return NULL;

    ctx->sample_rate = sample_rate;
    ctx->window_seconds = 2.0f;
    ctx->window_samples = (int)(ctx->window_seconds * sample_rate);
    ctx->buffer.reserve(ctx->window_samples);
    std::strcpy(ctx->detected_key, "---");

    return ctx;
}

void kd_destroy(void *ptr) {
    kd_context *ctx = (kd_context*)ptr;
    delete ctx;
}

void kd_feed(void *ptr, const int16_t *stereo_audio, int frames) {
    kd_context *ctx = (kd_context*)ptr;
    if (!ctx || !stereo_audio || frames <= 0) return;

    /* Downmix stereo int16 to mono double, append to buffer */
    for (int i = 0; i < frames; i++) {
        double left  = stereo_audio[i * 2]     / 32768.0;
        double right = stereo_audio[i * 2 + 1] / 32768.0;
        ctx->buffer.push_back((left + right) * 0.5);
    }

    /* Once we have enough audio, run analysis */
    if ((int)ctx->buffer.size() >= ctx->window_samples) {
        /* Build AudioData from buffer */
        KeyFinder::AudioData audio;
        audio.setChannels(1);
        audio.setFrameRate(ctx->sample_rate);
        audio.addToSampleCount(ctx->buffer.size());

        for (size_t i = 0; i < ctx->buffer.size(); i++) {
            audio.setSample(i, ctx->buffer[i]);
        }

        /* Run full analysis on the window */
        KeyFinder::key_t key = ctx->keyfinder.keyOfAudio(audio);

        /* Map enum to string */
        if (key >= 0 && key <= KeyFinder::SILENCE) {
            std::strncpy(ctx->detected_key, key_names[key],
                         sizeof(ctx->detected_key) - 1);
            ctx->detected_key[sizeof(ctx->detected_key) - 1] = '\0';
        }

        /* Clear buffer for next window */
        ctx->buffer.clear();
    }
}

int kd_get_key(void *ptr, char *buf, int buf_len) {
    kd_context *ctx = (kd_context*)ptr;
    if (!ctx || !buf || buf_len <= 0) return 0;

    int len = std::strlen(ctx->detected_key);
    if (len >= buf_len) len = buf_len - 1;
    std::memcpy(buf, ctx->detected_key, len);
    buf[len] = '\0';
    return len;
}

void kd_set_window(void *ptr, float seconds) {
    kd_context *ctx = (kd_context*)ptr;
    if (!ctx) return;

    if (seconds < 1.0f) seconds = 1.0f;
    if (seconds > 8.0f) seconds = 8.0f;

    ctx->window_seconds = seconds;
    ctx->window_samples = (int)(seconds * ctx->sample_rate);

    /* Clear buffer since window size changed */
    ctx->buffer.clear();
    std::strcpy(ctx->detected_key, "---");
}

float kd_get_window(void *ptr) {
    kd_context *ctx = (kd_context*)ptr;
    if (!ctx) return 2.0f;
    return ctx->window_seconds;
}

} /* extern "C" */
