#!/usr/bin/env bash
# Generates the two required conformance fixtures using synthetic FFmpeg
# sources (testsrc2 pattern), so no external/copyrighted media is needed.
#
# Fixture 1: cfr_bframes.mp4
#   - Constant frame rate, 30fps, 5s, H.264 with B-frames (bf=2) and a GOP
#     of 30 (one keyframe per second). Exercises: DTS/PTS reordering,
#     multi-second-spaced keyframes.
#
# Fixture 2: vfr_known_pts.mp4
#   - Genuinely variable frame rate. Built by concatenating three segments
#     at different frame rates (12fps, 30fps, 8fps) via `ffmpeg concat`
#     demuxer, then remuxing with -vsync vfr so the container keeps the
#     real, irregular presentation timestamps instead of forcing them onto
#     a constant grid. Expected ground truth for frame count/timing is
#     printed at the end and should be recorded before running media-core
#     against it (see EVIDENCE.md).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/../fixtures"
mkdir -p "$FIXTURES_DIR"
cd "$FIXTURES_DIR"

echo "== Fixture 1: cfr_bframes.mp4 (CFR, 30fps, B-frames, GOP=30) =="
ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "testsrc2=duration=5:size=640x360:rate=30" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p \
    -bf 2 -g 30 -sc_threshold 0 \
    cfr_bframes.mp4
echo "  wrote cfr_bframes.mp4"
ffprobe -v error -select_streams v:0 -show_entries frame=pts_time,pkt_dts_time \
    -of csv=p=0 cfr_bframes.mp4 > cfr_bframes.ground_truth_pts.txt
CFR_FRAME_COUNT=$(wc -l < cfr_bframes.ground_truth_pts.txt | tr -d ' ')
echo "  ground truth: $CFR_FRAME_COUNT frames; expected PTS = frame_index / 30, exact,"
echo "  keyframes every 30 frames (t=0,1,2,3,4s); see $FIXTURES_DIR/cfr_bframes.ground_truth_pts.txt"

echo
echo "== Fixture 2: vfr_known_pts.mp4 (genuine VFR) =="

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Three segments with distinct, deliberately different frame rates.
ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "testsrc2=duration=2:size=640x360:rate=12" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 12 "$TMP/seg_12fps.mp4"

ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "testsrc2=duration=2:size=640x360:rate=30" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 30 "$TMP/seg_30fps.mp4"

ffmpeg -y -hide_banner -loglevel error \
    -f lavfi -i "testsrc2=duration=2:size=640x360:rate=8" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -g 8 "$TMP/seg_8fps.mp4"

cat > "$TMP/concat_list.txt" <<EOF
file 'seg_12fps.mp4'
file 'seg_30fps.mp4'
file 'seg_8fps.mp4'
EOF

# -vsync vfr (passthrough) preserves each segment's real, differing frame
# rate in the output's presentation timestamps instead of resampling to a
# single container-wide rate.
ffmpeg -y -hide_banner -loglevel error \
    -f concat -safe 0 -i "$TMP/concat_list.txt" \
    -c:v libx264 -preset veryfast -pix_fmt yuv420p -vsync vfr \
    vfr_known_pts.mp4
echo "  wrote vfr_known_pts.mp4"

# Ground truth for VFR content should never be hand-predicted and trusted --
# the concat demuxer's actual PTS behavior depends on the FFmpeg build and
# does not always match the source segment rates exactly (confirmed during
# development of this fixture: it did not). We dump the real, measured
# per-frame PTS via ffprobe (used here strictly as a comparison oracle, per
# the assessment rules) immediately after generation so ground truth is
# always derived from the actual file, not from a comment.
ffprobe -v error -select_streams v:0 -show_entries frame=pts_time,pkt_dts_time \
    -of csv=p=0 vfr_known_pts.mp4 > vfr_known_pts.ground_truth_pts.txt
FRAME_COUNT=$(wc -l < vfr_known_pts.ground_truth_pts.txt | tr -d ' ')
echo "  ground truth: $FRAME_COUNT frames; per-frame PTS written to"
echo "  fixtures/vfr_known_pts.ground_truth_pts.txt (one PTS-seconds value per line,"
echo "  in presentation order). Regenerate this file whenever the fixture changes."

echo
echo "Done. Fixtures are in: $FIXTURES_DIR"
