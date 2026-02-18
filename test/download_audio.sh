#!/usr/bin/env bash
# Download and convert test audio files to WAV
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_FILES="$SCRIPT_DIR/test_files.txt"
AUDIO_DIR="$SCRIPT_DIR/audio"
BASEURL="https://www.cp.jku.at/datasets/giantsteps/backup/"
BACKUPURL="https://geo-samples.beatport.com/lofi/"

mkdir -p "$AUDIO_DIR"

total=0
ok=0
fail=0

while IFS='|' read -r base key; do
    mp3file="$AUDIO_DIR/${base}.mp3"
    wavfile="$AUDIO_DIR/${base}.wav"
    total=$((total + 1))

    # Skip if WAV already exists
    if [ -f "$wavfile" ]; then
        echo "[$total] SKIP $base (already converted)"
        ok=$((ok + 1))
        continue
    fi

    # Download MP3 if needed
    if [ ! -f "$mp3file" ]; then
        echo -n "[$total] Downloading $base.mp3... "
        if ! curl -sL -o "$mp3file" "${BASEURL}${base}.mp3" 2>/dev/null; then
            echo -n "primary failed, trying backup... "
            if ! curl -sL -o "$mp3file" "${BACKUPURL}${base}.mp3" 2>/dev/null; then
                echo "FAILED"
                fail=$((fail + 1))
                rm -f "$mp3file"
                continue
            fi
        fi
        # Check we got actual audio (not an HTML error page)
        if [ ! -s "$mp3file" ] || file "$mp3file" | grep -qi "html\|text"; then
            echo "FAILED (not audio)"
            rm -f "$mp3file"
            fail=$((fail + 1))
            continue
        fi
        echo "ok"
    fi

    # Convert to WAV: 16-bit stereo 44100Hz
    echo -n "[$total] Converting to WAV... "
    if ffmpeg -y -i "$mp3file" -ar 44100 -ac 2 -sample_fmt s16 -f wav "$wavfile" 2>/dev/null; then
        echo "ok ($key)"
        ok=$((ok + 1))
    else
        echo "FAILED"
        fail=$((fail + 1))
        rm -f "$wavfile"
    fi
done < "$TEST_FILES"

echo ""
echo "=== Summary ==="
echo "Total: $total  OK: $ok  Failed: $fail"
