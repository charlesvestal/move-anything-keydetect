/*
 * test_keydetect.cpp - Accuracy test for key detection using GiantSteps dataset
 *
 * Reads WAV files, feeds them through the kd_* wrapper API,
 * and compares detected key with ground truth annotations.
 */

#include "../src/dsp/keyfinder_wrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

/* ---- Simple WAV reader (PCM16 stereo only) ---- */

struct WavFile {
    int16_t *data;
    int frames;
    int channels;
    int sample_rate;
};

static bool read_wav(const char *path, WavFile *wav) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    /* RIFF header */
    char riff[4];
    uint32_t file_size, fmt_tag;
    fread(riff, 1, 4, f);
    fread(&file_size, 4, 1, f);
    fread(&fmt_tag, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) != 0) { fclose(f); return false; }

    /* Find fmt chunk */
    while (1) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4) { fclose(f); return false; }
        if (fread(&chunk_size, 4, 1, f) != 1) { fclose(f); return false; }

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_fmt, num_channels;
            uint32_t sample_rate, byte_rate;
            uint16_t block_align, bits_per_sample;
            fread(&audio_fmt, 2, 1, f);
            fread(&num_channels, 2, 1, f);
            fread(&sample_rate, 4, 1, f);
            fread(&byte_rate, 4, 1, f);
            fread(&block_align, 2, 1, f);
            fread(&bits_per_sample, 2, 1, f);

            if (audio_fmt != 1 || bits_per_sample != 16) {
                fprintf(stderr, "  Unsupported WAV format: fmt=%d bits=%d\n",
                        audio_fmt, bits_per_sample);
                fclose(f);
                return false;
            }
            wav->channels = num_channels;
            wav->sample_rate = sample_rate;

            /* Skip any extra fmt bytes */
            if (chunk_size > 16)
                fseek(f, chunk_size - 16, SEEK_CUR);
            break;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    /* Find data chunk */
    while (1) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (fread(chunk_id, 1, 4, f) != 4) { fclose(f); return false; }
        if (fread(&chunk_size, 4, 1, f) != 1) { fclose(f); return false; }

        if (memcmp(chunk_id, "data", 4) == 0) {
            int total_samples = chunk_size / 2;
            wav->frames = total_samples / wav->channels;
            wav->data = (int16_t *)malloc(chunk_size);
            fread(wav->data, 1, chunk_size, f);
            fclose(f);
            return true;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    fclose(f);
    return false;
}

/* ---- Key name normalization ---- */

/* Convert GiantSteps format "C minor" to wrapper format "C min" */
static std::string normalize_key(const std::string &key) {
    /* Map of note name equivalences for enharmonic comparison */
    std::string k = key;

    /* Replace "major" -> "maj", "minor" -> "min" */
    size_t pos;
    if ((pos = k.find("major")) != std::string::npos)
        k.replace(pos, 5, "maj");
    if ((pos = k.find("minor")) != std::string::npos)
        k.replace(pos, 5, "min");

    /* Remove extra spaces */
    while ((pos = k.find("  ")) != std::string::npos)
        k.replace(pos, 2, " ");

    /* Trim */
    while (!k.empty() && k.back() == ' ') k.pop_back();
    while (!k.empty() && k.front() == ' ') k.erase(k.begin());

    return k;
}

/* Check if two keys are the same, considering enharmonic equivalents */
static bool keys_match_exact(const std::string &a, const std::string &b) {
    return a == b;
}

/* Check if keys are "close" - same root note but wrong mode,
 * or relative major/minor */
static bool keys_are_relative(const std::string &a, const std::string &b) {
    /* Relative major/minor pairs (key, relative) */
    static const char* relatives[][2] = {
        {"C maj", "A min"}, {"Db maj", "Bb min"}, {"D maj", "B min"},
        {"Eb maj", "C min"}, {"E maj", "Db min"}, {"F maj", "D min"},
        {"Gb maj", "Eb min"}, {"G maj", "E min"}, {"Ab maj", "F min"},
        {"A maj", "Gb min"}, {"Bb maj", "G min"}, {"B maj", "Ab min"},
    };

    for (auto &pair : relatives) {
        if ((a == pair[0] && b == pair[1]) || (a == pair[1] && b == pair[0]))
            return true;
    }
    return false;
}

/* Check if keys are off by a perfect fifth (dominant relationship) */
static bool keys_fifth_related(const std::string &a, const std::string &b) {
    /* Extract root + mode */
    auto parse = [](const std::string &k) -> std::pair<int, bool> {
        static const char* notes[] = {
            "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
        };
        for (int i = 0; i < 12; i++) {
            if (k.find(notes[i]) == 0) {
                bool is_major = k.find("maj") != std::string::npos;
                return {i, is_major};
            }
        }
        return {-1, false};
    };

    auto [ra, ma] = parse(a);
    auto [rb, mb] = parse(b);
    if (ra < 0 || rb < 0) return false;

    int diff = (rb - ra + 12) % 12;
    /* Perfect fifth = 7 semitones, perfect fourth = 5 semitones */
    return (diff == 7 || diff == 5) && ma == mb;
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    const char *test_list = "test/test_files.txt";
    const char *audio_dir = "test/audio";
    float window_seconds = 4.0f;  /* default window */
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            window_seconds = atof(argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        }
    }

    printf("=== Key Detection Accuracy Test ===\n");
    printf("Window: %.1f seconds\n\n", window_seconds);

    /* Read test file list */
    std::ifstream list(test_list);
    if (!list.is_open()) {
        fprintf(stderr, "Cannot open %s\n", test_list);
        return 1;
    }

    struct TestCase {
        std::string base;
        std::string expected_key;
    };
    std::vector<TestCase> tests;
    std::string line;
    while (std::getline(list, line)) {
        size_t sep = line.find('|');
        if (sep == std::string::npos) continue;
        tests.push_back({line.substr(0, sep), line.substr(sep + 1)});
    }

    int total = 0, exact = 0, relative = 0, fifth = 0, wrong = 0;
    std::vector<std::string> wrong_list;

    for (auto &tc : tests) {
        std::string wav_path = std::string(audio_dir) + "/" + tc.base + ".wav";
        std::string expected = normalize_key(tc.expected_key);

        /* Read WAV */
        WavFile wav = {};
        if (!read_wav(wav_path.c_str(), &wav)) {
            fprintf(stderr, "  SKIP %s (cannot read WAV)\n", tc.base.c_str());
            continue;
        }

        /* Ensure stereo */
        int16_t *stereo = wav.data;
        int frames = wav.frames;
        bool free_stereo = false;

        if (wav.channels == 1) {
            /* Duplicate mono to stereo */
            stereo = (int16_t *)malloc(frames * 2 * sizeof(int16_t));
            free_stereo = true;
            for (int i = 0; i < frames; i++) {
                stereo[i * 2] = wav.data[i];
                stereo[i * 2 + 1] = wav.data[i];
            }
        }

        /* Create key detector */
        void *kd = kd_create(wav.sample_rate);
        kd_set_window(kd, window_seconds);

        /* Feed audio in 128-frame blocks (matching Move's block size).
         * To get multiple analyses, feed the whole file. */
        int block_size = 128;
        int fed = 0;
        int analyses_possible = 0;
        int window_frames = (int)(window_seconds * wav.sample_rate);

        for (int pos = 0; pos < frames; pos += block_size) {
            int n = std::min(block_size, frames - pos);
            kd_feed(kd, stereo + pos * 2, n);
            fed += n;

            /* Count how many windows we've completed */
            if (fed >= window_frames) {
                analyses_possible++;
            }
        }

        /* Wait for analysis thread to finish processing */
        usleep(500000);  /* 500ms should be plenty */

        /* Read result */
        char detected[64] = {};
        kd_get_key(kd, detected, sizeof(detected));

        /* Compare */
        std::string det(detected);
        total++;

        char status;
        if (keys_match_exact(det, expected)) {
            exact++;
            status = '=';
        } else if (keys_are_relative(det, expected)) {
            relative++;
            status = '~';
        } else if (keys_fifth_related(det, expected)) {
            fifth++;
            status = '5';
        } else {
            wrong++;
            status = 'X';
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s: expected [%s] got [%s]",
                     tc.base.c_str(), expected.c_str(), det.c_str());
            wrong_list.push_back(buf);
        }

        if (verbose || status != '=') {
            printf("[%c] %-20s expected: %-8s  detected: %-8s  (%.1fs audio, %d windows)\n",
                   status, tc.base.c_str(), expected.c_str(), det.c_str(),
                   (float)frames / wav.sample_rate,
                   frames / window_frames);
        }

        kd_destroy(kd);
        free(wav.data);
        if (free_stereo) free(stereo);
    }

    printf("\n=== Results (window=%.1fs, n=%d) ===\n", window_seconds, total);
    printf("Exact match:        %3d / %d  (%.1f%%)\n", exact, total, 100.0 * exact / total);
    printf("Relative maj/min:   %3d / %d  (%.1f%%)\n", relative, total, 100.0 * relative / total);
    printf("Fifth-related:      %3d / %d  (%.1f%%)\n", fifth, total, 100.0 * fifth / total);
    printf("Correct (exact+rel):%3d / %d  (%.1f%%)\n", exact + relative, total,
           100.0 * (exact + relative) / total);
    printf("Wrong:              %3d / %d  (%.1f%%)\n", wrong, total, 100.0 * wrong / total);

    if (!wrong_list.empty()) {
        printf("\nWrong detections:\n");
        for (auto &s : wrong_list)
            printf("%s\n", s.c_str());
    }

    return 0;
}
