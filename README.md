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

## Dependencies

| Dependency | Purpose | License |
|---|---|---|
| `libavformat`, `libavcodec`, `libavutil`, `libswscale` (system FFmpeg dev packages) | Demuxing, decoding, stream/codec metadata, pixel format lookups. `libswscale` is linked but currently unused by Part A (pulled in via the same pkg-config module set); kept for parity with a follow-on scaling/proxy step. | LGPL/GPL depending on build configuration — see "Licensing note" below |
| [`nlohmann/json`](https://github.com/nlohmann/json) v3.11.3, single header, vendored at `third_party/json.hpp` | JSON report serialization | MIT |

No player/editor SDK is used anywhere; all demux/decode/seek/timestamp logic
in `src/media_inspector.cpp` is hand-written against the raw FFmpeg C API.

**Licensing note (flagged for specialist review, not legal advice):** the
system FFmpeg build used here was compiled with `--enable-gpl` and includes
`libx264`, so redistributing binaries linked against it would carry GPL
obligations. For an assessment/demo this is fine; a production build would
need a licensing decision (e.g. a non-GPL FFmpeg build without `libx264`, or
accepting GPL for the whole binary).

## Setup

```bash
# 1. Install system dependencies (Ubuntu/Debian shown; adjust for other distros)
apt-get update
apt-get install -y libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
                    pkg-config cmake build-essential ffmpeg

# 2. Configure and build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j"$(nproc)"
cd ..
```

This produces two binaries in `build/`:

- `media-core` — the CLI tool
- `media-core-tests` — the automated assertion runner (see "Testing" below)

## Running

```bash
./build/media-core <input> --targets 0.0,0.5,1.1,4.75 --output report.json [--trace-limit N]
```

- `<input>` — path to a media file (required, positional)
- `--targets` — comma-separated list of timeline times in seconds to request
  frames for (required)
- `--output` — path to write the JSON report to (default: `report.json`)
- `--trace-limit` — max number of entries kept in `packet_trace` and
  `frame_trace` in the report; keyframe index and CFR/VFR analysis still
  scan the whole file regardless of this value (default: 64)

Example:

```bash
./build/media-core fixtures/cfr_bframes.mp4 \
    --targets 0.0,0.5,1.1,2.4999,4.9 \
    --output report.json
```

The tool always writes a report, even on failure — check the top-level
`errors` array in the JSON to see what went wrong (missing file, no video
stream, decode error, etc.). Exit code is `0` on full success, `1` on any
analysis failure, `2` on a CLI usage error.

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
  preserved. Ground truth (measured via `ffprobe`, not hand-predicted — see
  `DECISIONS.md`): `fixtures/vfr_known_pts.ground_truth_pts.txt`.

A third "difficult case" fixture (non-zero start time / sparse keyframes /
truncated file) was **not** submitted; per the assessment this is optional
bonus evidence.

## Testing

```bash
./build/media-core fixtures/cfr_bframes.mp4 \
    --targets 0.0,0.5,1.1,2.4999 --output /tmp/cfr_report.json --trace-limit 150

./build/media-core fixtures/vfr_known_pts.mp4 \
    --targets 0.2,0.75,1.9 --output /tmp/vfr_report.json --trace-limit 80

./build/media-core-tests /tmp/cfr_report.json /tmp/vfr_report.json
```

`media-core-tests` is a small dependency-free assertion runner (no gtest, to
keep the dependency list short) that checks the JSON *reports* against known
ground truth — see `tests/test_assertions.cpp` for what each of the 9
assertions catches.

## Feature list

- Opens arbitrary containers via `avformat_open_input` / finds the video
  stream via `av_find_best_stream`
- Stream summary: container, codec, dimensions, pixel format, time base,
  start time, duration, reported frame rates, and a measured (not
  metadata-trusted) CFR/VFR verdict with numeric basis
- Bounded packet trace (DTS/PTS/keyframe flag/size) and bounded
  presentation-order frame trace (PTS + picture type), demonstrating
  decode/presentation-order reordering under B-frames
- Full-file keyframe index with inter-keyframe time gaps
- Timestamp-aware frame requests against an explicit, documented selection
  rule (see `MediaInspector::selectionRuleText()`), including both boundary
  cases (`clamped_to_first_frame`, `clamped_to_last_frame`)
- Structured error handling for missing/unsupported/truncated/audio-only
  inputs; the tool never crashes on these inputs (see `EVIDENCE.md`) and
  frees all FFmpeg resources on every exit path (verified with Valgrind, 0
  definitely/indirectly lost bytes — see `EVIDENCE.md`)

## Known limitations

- Keyframe detection relies on `AV_PKT_FLAG_KEY` as set by the demuxer, not
  an independent parse of codec-level IDR markers. Documented in code
  (`buildKeyframeIndexAndPacketTrace`) and in `DECISIONS.md`.
- The CFR/VFR verdict uses a fixed 2% coefficient-of-variation threshold on
  measured frame intervals. It's a pragmatic threshold, not a formal spec;
  see `DECISIONS.md` for the reasoning and how it could be made adaptive.
- `libswscale` is linked but not yet used (no pixel format conversion or
  scaling implemented in this slice).
- Single video-stream only; multi-track video files use FFmpeg's default
  "best stream" heuristic rather than custom track selection.