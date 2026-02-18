#!/usr/bin/env python3
"""Pick up to 2 files per key for a representative test subset."""
import os, glob

script_dir = os.path.dirname(os.path.abspath(__file__))
key_dir = os.path.join(script_dir, "dataset", "giantsteps-key-dataset", "annotations", "key")
out_path = os.path.join(script_dir, "test_files.txt")

counts = {}
lines = []

for f in sorted(glob.glob(os.path.join(key_dir, "*.key"))):
    key = open(f).read().strip()
    base = os.path.basename(f).replace(".key", "")
    if counts.get(key, 0) < 2:
        lines.append(f"{base}|{key}")
        counts[key] = counts.get(key, 0) + 1

with open(out_path, "w") as fh:
    fh.write("\n".join(lines) + "\n")

print(f"Selected {len(lines)} files")
for line in sorted(lines, key=lambda x: x.split("|")[1]):
    print(line)
