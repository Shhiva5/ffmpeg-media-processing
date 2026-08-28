#!/usr/bin/env bash
# Part B: generates one 30s synthetic "master" source with real motion
# complexity (a zooming Mandelbrot render, not a flat test pattern -- a
# static/flat pattern compresses trivially and wouldn't meaningfully
# exercise inter-frame prediction, defeating the point of an all-intra vs
# short-GOP comparison), then derives two 720p H.264 proxy variants from it:
# all-intra and short-GOP.
#
# All media here is synthetic (lavfi-generated), per the assessment's
# "public, synthetic or self-owned media only" rule.
#
# The settings below (30s duration, shallower end_scale=0.02, 
# native 720p master so no extra scale filter is
# needed for the proxies) were chosen empirically to keep total
# encode time bounded (~30s per proxy) while still producing
# genuinely detailed, high-motion synthetic footage.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$SCRIPT_DIR/../part_b"
mkdir -p "$OUT_DIR"
cd "$OUT_DIR"

DURATION=30
FPS=30
SIZE=1280x720
SHORT_GOP_FRAMES=$((FPS * 2))   # keyframe every 2s

echo "== Master source: source_master.mp4 (${DURATION}s, ${SIZE}, ${FPS}fps) =="
ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "mandelbrot=size=${SIZE}:rate=${FPS}:start_scale=3.0:end_scale=0.02" \
    -t "$DURATION" \
    -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \
    -movflags +faststart \
    source_master.mp4
echo "  wrote source_master.mp4"
ffprobe -v error -show_entries format=size,duration,bit_rate \
    -of default=noprint_wrappers=1 source_master.mp4

echo
echo "== Proxy 1: proxy_allintra.mp4 (720p, all-intra, -g 1 -bf 0) =="
# Master is already 720p, so proxies re-encode without a scale filter;
# in a real pipeline the master would typically be higher resolution and
# this step would add -vf scale=1280:720.
ffmpeg -y -hide_banner -loglevel error \
    -i source_master.mp4 \
    -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \
    -g 1 -bf 0 -sc_threshold 0 -an \
    proxy_allintra.mp4
echo "  wrote proxy_allintra.mp4"

echo
echo "== Proxy 2: proxy_shortgop.mp4 (720p, short-GOP, -g ${SHORT_GOP_FRAMES} -bf 2) =="
ffmpeg -y -hide_banner -loglevel error \
    -i source_master.mp4 \
    -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p \
    -g "$SHORT_GOP_FRAMES" -bf 2 -sc_threshold 0 -an \
    proxy_shortgop.mp4
echo "  wrote proxy_shortgop.mp4"

echo
echo "== File sizes / bitrates =="
for f in proxy_allintra.mp4 proxy_shortgop.mp4; do
    echo "--- $f ---"
    ls -l "$f" | awk '{print "  size_bytes:", $5}'
    ffprobe -v error -show_entries format=size,bit_rate,duration \
        -of default=noprint_wrappers=1 "$f" | sed 's/^/  /'
done

echo
echo "Done. Master + proxies are in: $OUT_DIR"
