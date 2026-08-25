# ffmpeg-media-processing

# media-core (Part A)

A small C++17 command-line tool built directly on FFmpeg's `libavformat` /
`libavcodec` APIs. It inspects one video stream, performs timestamp-aware
frame requests against arbitrary timeline times, and writes a
machine-readable JSON report. `ffprobe` is used only as a comparison oracle
(to generate ground truth for fixtures and to sanity-check reports), never
as part of the tool's own implementation.

This is Part A of a larger assessment; Parts B (browser codec support) and C
(desktop architecture memo) are tracked separately.

## Supported environment

Developed and tested on:

- Ubuntu 22.04 (container), x86_64
- GCC 11.4.0, C++17
- CMake 3.22.1
- FFmpeg 4.4.2 (`libavformat` 58.76.100, `libavcodec` 58.134.102, `libavutil`
  56.70.100, `libswscale` 5.9.100), installed via `apt`
  (`libavformat-dev libavcodec-dev libavutil-dev libswscale-dev`)

Other FFmpeg 5.x/6.x builds should work unmodified since the code only uses
long-stable public API (`avformat_open_input`, `av_find_best_stream`,
`avcodec_send_packet`/`avcodec_receive_frame`, etc.), but this has not been
verified against other versions.

## Test media (conformance fixtures)

```bash
./scripts/generate_fixtures.sh
```

Generates two required fixtures from synthetic FFmpeg sources (no external
or copyrighted media) into `fixtures/`:

- `cfr_bframes.mp4` — 30fps constant frame rate, H.264, 2 B-frames, GOP=30
  (keyframe every second). Ground truth: `fixtures/cfr_bframes.ground_truth_pts.txt`.
- `vfr_known_pts.mp4` — genuinely variable frame rate, built by concatenating
  three segments encoded at different source frame rates (12/30/8fps) and
  remuxing with `-vsync vfr` so real per-frame PTS irregularity is
  preserved. Ground truth (measured via `ffprobe`, not hand-predicted): `fixtures/vfr_known_pts.ground_truth_pts.txt`.

A third "difficult case" fixture (non-zero start time / sparse keyframes /
truncated file) was **not** submitted; per the assessment this is optional
bonus evidence.