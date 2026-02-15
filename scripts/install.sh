#!/bin/bash
# Install KeyDetect module to Move
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$REPO_ROOT"

if [ ! -d "dist/keydetect" ]; then
    echo "Error: dist/keydetect not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "=== Installing KeyDetect Module ==="

# Deploy to Move - audio_fx subdirectory
echo "Copying module to Move..."
ssh ableton@move.local "mkdir -p /data/UserData/move-anything/modules/audio_fx/keydetect"
scp -r dist/keydetect/* ableton@move.local:/data/UserData/move-anything/modules/audio_fx/keydetect/

# Set permissions so Module Store can update later
echo "Setting permissions..."
ssh ableton@move.local "chmod -R a+rw /data/UserData/move-anything/modules/audio_fx/keydetect"

echo ""
echo "=== Install Complete ==="
echo "Module installed to: /data/UserData/move-anything/modules/audio_fx/keydetect/"
echo ""
echo "Restart Move Anything to load the new module."
