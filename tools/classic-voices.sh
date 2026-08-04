#!/bin/sh
#
# Render every classic-set preset through the same sentence, for comparison by
# ear. The tuning behind these voices was reasoned from published acoustics and
# checked against spectra and levels - which is not the same as having heard
# them, and this is how you close that gap.
#
#   make bm && sh tools/classic-voices.sh [output directory]
#
# See CLASSIC-VOICES.md for what each voice is reaching for.

set -eu

OUT="${1:-render}"
LINE="Hello. I am BENCmouth, a formant speech synthesizer. Testing one, two, three."

if [ ! -x ./bm ]; then
    echo "build the CLI first:  make bm" >&2
    exit 1
fi

mkdir -p "$OUT"

for v in Compact Announcer Operator Cadet Whisper Rattled; do
    ./bm -v "$v" "$LINE" -o "$OUT/classic-$v.wav"
done

# The originals alongside them, so the comparison has a reference point rather
# than only having the new voices to compare against each other.
for v in "BENCmouth Retro" "BENCmouth"; do
    ./bm -v "$v" "$LINE" -o "$OUT/classic-$(echo "$v" | tr ' ' '-').wav"
done

ls -l "$OUT"/classic-*.wav
