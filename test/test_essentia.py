#!/usr/bin/env python3
"""Test key detection accuracy using Essentia's KeyExtractor with various profiles."""

import os
import sys
import essentia
import essentia.standard as es

AUDIO_DIR = os.path.join(os.path.dirname(__file__), "audio")
TEST_LIST = os.path.join(os.path.dirname(__file__), "test_files.txt")

# Normalize GiantSteps key names to match Essentia output
# Essentia outputs e.g. "C", "Db" for root and "major"/"minor" for scale
# GiantSteps uses e.g. "C major", "Db minor"

# Enharmonic equivalents for matching
ENHARMONIC = {
    "C#": "Db", "D#": "Eb", "F#": "Gb", "G#": "Ab", "A#": "Bb",
    "Db": "Db", "Eb": "Eb", "Gb": "Gb", "Ab": "Ab", "Bb": "Bb",
}

def normalize_essentia_key(key, scale):
    """Convert Essentia output to our standard format."""
    root = ENHARMONIC.get(key, key)
    mode = "maj" if scale == "major" else "min"
    return f"{root} {mode}"

def normalize_gt_key(key_str):
    """Convert GiantSteps annotation to our standard format."""
    parts = key_str.strip().split()
    root = parts[0]
    mode = "maj" if parts[1] == "major" else "min"
    root = ENHARMONIC.get(root, root)
    return f"{root} {mode}"

RELATIVES = [
    ("C maj", "A min"), ("Db maj", "Bb min"), ("D maj", "B min"),
    ("Eb maj", "C min"), ("E maj", "Db min"), ("F maj", "D min"),
    ("Gb maj", "Eb min"), ("G maj", "E min"), ("Ab maj", "F min"),
    ("A maj", "Gb min"), ("Bb maj", "G min"), ("B maj", "Ab min"),
]

def keys_are_relative(a, b):
    for x, y in RELATIVES:
        if (a == x and b == y) or (a == y and b == x):
            return True
    return False

NOTE_TO_SEMITONE = {
    "C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5,
    "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11,
}

def keys_fifth_related(a, b):
    pa = a.split()
    pb = b.split()
    if len(pa) != 2 or len(pb) != 2:
        return False
    ra = NOTE_TO_SEMITONE.get(pa[0], -1)
    rb = NOTE_TO_SEMITONE.get(pb[0], -1)
    if ra < 0 or rb < 0:
        return False
    diff = (rb - ra) % 12
    return (diff == 7 or diff == 5) and pa[1] == pb[1]


def test_profile(profile_name, tests):
    """Run key detection with a specific Essentia profile."""
    total = exact = relative = fifth = wrong = 0
    wrong_list = []

    for base, expected_raw in tests:
        wav_path = os.path.join(AUDIO_DIR, f"{base}.wav")
        if not os.path.exists(wav_path):
            print(f"  SKIP {base}")
            continue

        expected = normalize_gt_key(expected_raw)

        # Load audio
        loader = es.MonoLoader(filename=wav_path, sampleRate=44100)
        audio = loader()

        # Run KeyExtractor
        key_extractor = es.KeyExtractor(profileType=profile_name)
        key, scale, strength = key_extractor(audio)
        detected = normalize_essentia_key(key, scale)

        total += 1
        if detected == expected:
            exact += 1
            status = "="
        elif keys_are_relative(detected, expected):
            relative += 1
            status = "~"
        elif keys_fifth_related(detected, expected):
            fifth += 1
            status = "5"
        else:
            wrong += 1
            status = "X"
            wrong_list.append(f"  {base}: expected [{expected}] got [{detected}]")

        if status != "=" or "-v" in sys.argv:
            print(f"[{status}] {base:<20s} expected: {expected:<8s}  "
                  f"detected: {detected:<8s}  strength: {strength:.3f}")

    print(f"\n--- {profile_name} (n={total}) ---")
    print(f"Exact:    {exact:3d} / {total}  ({100*exact/total:.1f}%)")
    print(f"Relative: {relative:3d} / {total}  ({100*relative/total:.1f}%)")
    print(f"Fifth:    {fifth:3d} / {total}  ({100*fifth/total:.1f}%)")
    print(f"Correct:  {exact+relative:3d} / {total}  ({100*(exact+relative)/total:.1f}%)  [exact + relative]")
    print(f"Wrong:    {wrong:3d} / {total}  ({100*wrong/total:.1f}%)")

    if wrong_list:
        print("\nWrong detections:")
        for s in wrong_list:
            print(s)

    return exact, relative, fifth, wrong, total


def main():
    # Read test list
    tests = []
    with open(TEST_LIST) as f:
        for line in f:
            line = line.strip()
            if "|" in line:
                base, key = line.split("|", 1)
                tests.append((base, key))

    print(f"=== Essentia Key Detection Accuracy Test ===")
    print(f"Tracks: {len(tests)}\n")

    profiles = ["edma", "edmm", "krumhansl", "temperley", "shaath", "bgate"]
    results = {}

    for profile in profiles:
        print(f"\n{'='*60}")
        print(f"Testing profile: {profile}")
        print(f"{'='*60}")
        results[profile] = test_profile(profile, tests)

    # Summary table
    print(f"\n\n{'='*60}")
    print(f"SUMMARY")
    print(f"{'='*60}")
    print(f"{'Profile':<12s} {'Exact':>6s} {'Rel':>6s} {'5th':>6s} {'Corr':>6s} {'Wrong':>6s}")
    print("-" * 48)
    for profile in profiles:
        e, r, f, w, t = results[profile]
        print(f"{profile:<12s} {100*e/t:5.1f}% {100*r/t:5.1f}% {100*f/t:5.1f}% "
              f"{100*(e+r)/t:5.1f}% {100*w/t:5.1f}%")


if __name__ == "__main__":
    main()
