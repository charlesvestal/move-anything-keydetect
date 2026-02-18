/*
 * test_direct.cpp - Direct libkeyfinder accuracy test
 *
 * Tests libkeyfinder directly (no wrapper) with full tracks and
 * windowed voting, to establish a proper accuracy baseline.
 */

#include <keyfinder/keyfinder.h>
#include <keyfinder/audiodata.h>
#include <keyfinder/constants.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <algorithm>

/* Key name mapping matching the wrapper's output */
static const char* key_names[] = {
    "A maj",  "A min",  "Bb maj", "Bb min",
    "B maj",  "B min",  "C maj",  "C min",
    "Db maj", "Db min", "D maj",  "D min",
    "Eb maj", "Eb min", "E maj",  "E min",
    "F maj",  "F min",  "Gb maj", "Gb min",
    "G maj",  "G min",  "Ab maj", "Ab min",
    "---"
};

/* ---- WAV reader ---- */

struct WavFile {
    int16_t *data;
    int frames;
    int channels;
    int sample_rate;
};

static bool read_wav(const char *path, WavFile *wav) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    char riff[4]; uint32_t file_size, fmt_tag;
    fread(riff, 1, 4, f); fread(&file_size, 4, 1, f); fread(&fmt_tag, 1, 4, f);
    if (memcmp(riff, "RIFF", 4) != 0) { fclose(f); return false; }

    while (1) {
        char id[4]; uint32_t sz;
        if (fread(id, 1, 4, f) != 4) { fclose(f); return false; }
        if (fread(&sz, 4, 1, f) != 1) { fclose(f); return false; }
        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t fmt, ch; uint32_t sr, br; uint16_t ba, bps;
            fread(&fmt, 2, 1, f); fread(&ch, 2, 1, f);
            fread(&sr, 4, 1, f); fread(&br, 4, 1, f);
            fread(&ba, 2, 1, f); fread(&bps, 2, 1, f);
            if (fmt != 1 || bps != 16) { fclose(f); return false; }
            wav->channels = ch; wav->sample_rate = sr;
            if (sz > 16) fseek(f, sz - 16, SEEK_CUR);
            break;
        } else fseek(f, sz, SEEK_CUR);
    }

    while (1) {
        char id[4]; uint32_t sz;
        if (fread(id, 1, 4, f) != 4) { fclose(f); return false; }
        if (fread(&sz, 4, 1, f) != 1) { fclose(f); return false; }
        if (memcmp(id, "data", 4) == 0) {
            wav->frames = sz / (2 * wav->channels);
            wav->data = (int16_t *)malloc(sz);
            fread(wav->data, 1, sz, f);
            fclose(f); return true;
        } else fseek(f, sz, SEEK_CUR);
    }
}

/* ---- Key comparison helpers ---- */

static std::string normalize_key(const std::string &key) {
    std::string k = key;
    size_t pos;
    if ((pos = k.find("major")) != std::string::npos) k.replace(pos, 5, "maj");
    if ((pos = k.find("minor")) != std::string::npos) k.replace(pos, 5, "min");
    while ((pos = k.find("  ")) != std::string::npos) k.replace(pos, 2, " ");
    while (!k.empty() && k.back() == ' ') k.pop_back();
    while (!k.empty() && k.front() == ' ') k.erase(k.begin());
    return k;
}

static bool keys_are_relative(const std::string &a, const std::string &b) {
    static const char* rels[][2] = {
        {"C maj", "A min"}, {"Db maj", "Bb min"}, {"D maj", "B min"},
        {"Eb maj", "C min"}, {"E maj", "Db min"}, {"F maj", "D min"},
        {"Gb maj", "Eb min"}, {"G maj", "E min"}, {"Ab maj", "F min"},
        {"A maj", "Gb min"}, {"Bb maj", "G min"}, {"B maj", "Ab min"},
    };
    for (auto &p : rels)
        if ((a == p[0] && b == p[1]) || (a == p[1] && b == p[0])) return true;
    return false;
}

static bool keys_fifth_related(const std::string &a, const std::string &b) {
    static const char* notes[] = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    auto parse = [&](const std::string &k) -> std::pair<int,bool> {
        for (int i = 0; i < 12; i++)
            if (k.find(notes[i]) == 0) return {i, k.find("maj") != std::string::npos};
        return {-1, false};
    };
    auto [ra, ma] = parse(a);
    auto [rb, mb] = parse(b);
    if (ra < 0 || rb < 0) return false;
    int diff = (rb - ra + 12) % 12;
    return (diff == 7 || diff == 5) && ma == mb;
}

/* ---- Analysis modes ---- */

static std::string detect_full_track(const double *mono, int samples, int rate) {
    KeyFinder::KeyFinder kf;
    KeyFinder::AudioData audio;
    audio.setChannels(1);
    audio.setFrameRate(rate);
    audio.addToSampleCount(samples);
    for (int i = 0; i < samples; i++)
        audio.setSample(i, mono[i]);
    KeyFinder::key_t k = kf.keyOfAudio(audio);
    if (k >= 0 && k <= KeyFinder::SILENCE) return key_names[k];
    return "---";
}

static std::string detect_voting(const double *mono, int samples, int rate,
                                  float window_sec) {
    KeyFinder::KeyFinder kf;
    int window_samples = (int)(window_sec * rate);
    std::map<std::string, int> votes;

    for (int offset = 0; offset + window_samples <= samples; offset += window_samples) {
        KeyFinder::AudioData audio;
        audio.setChannels(1);
        audio.setFrameRate(rate);
        audio.addToSampleCount(window_samples);
        for (int i = 0; i < window_samples; i++)
            audio.setSample(i, mono[offset + i]);
        KeyFinder::key_t k = kf.keyOfAudio(audio);
        if (k >= 0 && k < KeyFinder::SILENCE) {
            votes[key_names[k]]++;
        }
    }

    /* Find majority */
    std::string best = "---";
    int best_count = 0;
    for (auto &[key, count] : votes) {
        if (count > best_count) {
            best_count = count;
            best = key;
        }
    }
    return best;
}

/* ---- Score tracking ---- */

struct Scores {
    int total = 0, exact = 0, relative = 0, fifth = 0, wrong = 0;
    std::vector<std::string> wrong_list;

    void record(const std::string &base, const std::string &expected,
                const std::string &detected) {
        total++;
        if (detected == expected) exact++;
        else if (keys_are_relative(detected, expected)) relative++;
        else if (keys_fifth_related(detected, expected)) fifth++;
        else {
            wrong++;
            char buf[256];
            snprintf(buf, sizeof(buf), "  %s: expected [%s] got [%s]",
                     base.c_str(), expected.c_str(), detected.c_str());
            wrong_list.push_back(buf);
        }
    }

    void print(const char *label) {
        printf("\n--- %s (n=%d) ---\n", label, total);
        printf("Exact:   %3d / %d  (%.1f%%)\n", exact, total, 100.0*exact/total);
        printf("Relative:%3d / %d  (%.1f%%)\n", relative, total, 100.0*relative/total);
        printf("Fifth:   %3d / %d  (%.1f%%)\n", fifth, total, 100.0*fifth/total);
        printf("Correct: %3d / %d  (%.1f%%)  [exact + relative]\n",
               exact+relative, total, 100.0*(exact+relative)/total);
        printf("Wrong:   %3d / %d  (%.1f%%)\n", wrong, total, 100.0*wrong/total);
    }
};

int main() {
    const char *test_list = "test/test_files.txt";
    const char *audio_dir = "test/audio";
    const int DOWNSAMPLE = 4;
    const int EFFECTIVE_RATE = 44100 / DOWNSAMPLE;

    std::ifstream list(test_list);
    if (!list.is_open()) { fprintf(stderr, "Cannot open %s\n", test_list); return 1; }

    struct TC { std::string base, key; };
    std::vector<TC> tests;
    std::string line;
    while (std::getline(list, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos)
            tests.push_back({line.substr(0, sep), line.substr(sep+1)});
    }

    printf("=== Direct libkeyfinder Accuracy Test ===\n");
    printf("Tracks: %zu\n\n", tests.size());

    /* Test multiple modes */
    Scores full_44k, full_11k, vote_4s, vote_8s;

    for (auto &tc : tests) {
        std::string wav_path = std::string(audio_dir) + "/" + tc.base + ".wav";
        std::string expected = normalize_key(tc.key);

        WavFile wav = {};
        if (!read_wav(wav_path.c_str(), &wav)) {
            fprintf(stderr, "  SKIP %s\n", tc.base.c_str());
            continue;
        }

        /* Convert to mono double at 44100 */
        int frames = wav.frames;
        std::vector<double> mono44(frames);
        for (int i = 0; i < frames; i++) {
            if (wav.channels == 2)
                mono44[i] = (wav.data[i*2] + wav.data[i*2+1]) / 65536.0;
            else
                mono44[i] = wav.data[i] / 32768.0;
        }

        /* Downsample to 11025 (same as wrapper) */
        int ds_frames = frames / DOWNSAMPLE;
        std::vector<double> mono11(ds_frames);
        for (int i = 0; i < ds_frames; i++)
            mono11[i] = mono44[i * DOWNSAMPLE];

        printf("%-20s expected: %-8s  ", tc.base.c_str(), expected.c_str());

        /* Mode 1: Full track at 44100 Hz */
        std::string r1 = detect_full_track(mono44.data(), frames, 44100);
        full_44k.record(tc.base, expected, r1);

        /* Mode 2: Full track at 11025 Hz (same as wrapper's effective rate) */
        std::string r2 = detect_full_track(mono11.data(), ds_frames, EFFECTIVE_RATE);
        full_11k.record(tc.base, expected, r2);

        /* Mode 3: Voting with 4s windows at 11025 Hz */
        std::string r3 = detect_voting(mono11.data(), ds_frames, EFFECTIVE_RATE, 4.0);
        vote_4s.record(tc.base, expected, r3);

        /* Mode 4: Voting with 8s windows at 11025 Hz */
        std::string r4 = detect_voting(mono11.data(), ds_frames, EFFECTIVE_RATE, 8.0);
        vote_8s.record(tc.base, expected, r4);

        printf("full44k=%-8s full11k=%-8s vote4s=%-8s vote8s=%-8s\n",
               r1.c_str(), r2.c_str(), r3.c_str(), r4.c_str());

        free(wav.data);
    }

    full_44k.print("Full track @ 44100 Hz");
    full_11k.print("Full track @ 11025 Hz (downsampled)");
    vote_4s.print("4s window voting @ 11025 Hz");
    vote_8s.print("8s window voting @ 11025 Hz");

    return 0;
}
