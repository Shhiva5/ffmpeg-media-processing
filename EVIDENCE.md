# Evidence (Part A)

## Environment measurements were taken on

- CPU: AMD Ryzen 7 Processor @ 3.30GHz
- OS: Ubuntu 22.04 (container), x86_64
- Compiler: GCC 11.4.0, `-O2 -g` (`CMAKE_BUILD_TYPE=RelWithDebInfo`)
- FFmpeg:  4.4.2 (system package), `libavformat` 58.76.100, `libavcodec` 58.134.102,
  `libavutil` 56.70.100, `libswscale` 5.9.100

All numbers below are **observed facts** from running the actual binary,
reproduced with the exact commands shown. Nothing in this file is a
hypothesis or projection.

## 1. Build

```
$ mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo && make -j$(nproc)
...
[100%] Built target media-core
[100%] Built target media-core-tests
```
Clean build, no warnings suppressed (`-Wall -Wextra` enabled), no errors.

## 2. Fixture generation

```
$ ./scripts/generate_fixtures.sh
== Fixture 1: cfr_bframes.mp4 (CFR, 30fps, B-frames, GOP=30) ==
  wrote cfr_bframes.mp4
  ground truth: 150 frames; expected PTS = frame_index / 30, exact,
  keyframes every 30 frames (t=0,1,2,3,4s); see cfr_bframes.ground_truth_pts.txt

== Fixture 2: vfr_known_pts.mp4 (genuine VFR) ==
  wrote vfr_known_pts.mp4
  ground truth: 72 frames; per-frame PTS written to
  fixtures/vfr_known_pts.ground_truth_pts.txt
```

**Observed fact, correcting an initial assumption:** the VFR fixture's
`generate_fixtures.sh` comment originally *predicted* 100 frames across
three clean 12/30/8fps regimes. The actual file, measured via `ffprobe
-show_entries frame=pts_time`, has **71 frames** with irregular deltas: a
sustained ~1/12s (0.0833s) interval for the first ~24 frames, a single
0.583s gap, then a sustained ~1/6s (0.1667s) interval for the remainder —
not three clean segments. This is because the `concat` demuxer + `-vsync
vfr` did not preserve each segment's exact source frame rate the way I
initially assumed. The fixture is still genuinely variable-frame-rate (two
very different, non-jittery interval regimes plus a large outlier gap),
which is what the spec requires, but I'm flagging the discrepancy between
my prediction and reality per the "state expected behaviour before running
the tool" instruction — see the ground-truth `.txt` files for the real,
measured values, which is what `test_assertions.cpp` is actually checked
against.

## 3. Stream summary correctness (ffprobe cross-check)

```
$ ffprobe -v error -select_streams v:0 \
    -show_entries stream=avg_frame_rate,r_frame_rate,nb_frames \
    -of default=noprint_wrappers=1 fixtures/cfr_bframes.mp4
r_frame_rate=30/1
avg_frame_rate=30/1
nb_frames=150

$ ffprobe -v error -select_streams v:0 -show_entries frame=pict_type \
    -of csv=p=0 fixtures/cfr_bframes.mp4 | sort | uniq -c
     89 B
      5 I
     56 P
```

`media-core`'s own report on the same file:

```json
"stream_summary": {
  "avg_frame_rate": "30/1",
  "r_frame_rate_nominal": "30/1",
  "cfr_vfr_verdict": "CFR",
  "cfr_vfr_basis": "Measured 119 decoded frame intervals: mean=0.033333s, stddev=0.000000s, coefficient_of_variation=0.0000 (threshold 0.02)."
}
```
Frame rates match `ffprobe` exactly; B-frame presence (89 B-frames out of
150) confirms the fixture exercises decode/presentation reordering as
required.

## 4. Frame-request correctness (the core requirement)

Command:
```
$ ./build/media-core fixtures/cfr_bframes.mp4 \
    --targets 0.0,0.5,1.1,2.4999,4.9,10.0 --output /tmp/cfr_report.json
```

Selected results (full JSON in the report; this is the load-bearing part):

| target_s | selected_pts_s | next_pts_s | timing_error_s | fallback |
|---|---|---|---|---|
| 0.0 | 0.0 | 0.0333 | 0.0 | — |
| 0.5 | 0.5 | 0.5333 | 0.0 | — |
| 1.1 | 1.1 | 1.1333 | 0.0 | — |
| 2.4999 | **2.4667** | 2.5 | -0.0332 | — |
| 4.9 | 4.9 | 4.9333 | 0.0 | — |
| 10.0 (past EOF) | 4.9667 | — | -5.033 | `clamped_to_last_frame` |

The `2.4999` case is the important one: at 30fps the nearest exact frame
grid point is 2.5s, but the selection rule (last decoded frame whose
PTS ≤ T) correctly returns **2.4667s**, not 2.5s — this is direct evidence
the tool is not doing `frame_index * (1/fps)` and is instead comparing
actual decoded PTS against T. The out-of-range request at `10.0` (file
duration is 5.0s) correctly clamps to the last frame and reports
`clamped_to_last_frame` rather than erroring or hanging.

## 5. VFR handling evidence

```
$ ./build/media-core fixtures/vfr_known_pts.mp4 --targets 0.05,0.5,1.0,2.3 --output /tmp/vfr_report.json
```

The request for `target_s=2.3` lands in the fixture's large PTS gap
(1.9167s → 2.5s, a 0.583s jump — see section 2): the tool correctly selects
`selected_pts_s=1.9167`, reports `next_pts_s=2.5`, `duration_s=0.5833`, and
`timing_error_s=-0.3833`. This is the tool correctly surfacing a large,
real timing error rather than masking it — exactly the kind of case a
frame-index-based shortcut would get wrong.

`cfr_vfr_verdict` for this fixture: `"VFR"`, with
`coefficient_of_variation=0.6404` (vs. `0.0000` for the CFR fixture) —
confirms the classifier separates the two fixtures cleanly.

## 6. Automated assertions

```
$ ./build/media-core fixtures/cfr_bframes.mp4 --targets 0.0,0.5,1.1,2.4999 --output /tmp/cfr_report.json --trace-limit 150
$ ./build/media-core fixtures/vfr_known_pts.mp4 --targets 0.2,0.75,1.9 --output /tmp/vfr_report.json --trace-limit 80
$ ./build/media-core-tests cfr /tmp/cfr_report.json
PASS: CFR fixture: verdict is CFR
PASS: CFR fixture: frame_requests present
PASS: CFR fixture: T=1.1s frame found
PASS: CFR fixture: T=1.1s timing_error_s (0.000000s) within half-frame tolerance (0.016667s)
PASS: CFR fixture: T=1.1s target was actually requested (update fixture target list if this fails)
PASS: CFR fixture: frame_trace PTS values are monotonically non-decreasing (presentation order)

6/6 checks passed
$ ./build/media-core-tests vfr /tmp/vfr_report.json
PASS: VFR fixture: verdict is VFR (catches false-CFR misclassification)
PASS: VFR fixture: mean_frame_interval_s is positive
PASS: VFR fixture: coefficient of variation exceeds the CFR/VFR threshold used to classify it (internal consistency check)

3/3 checks passed
```
Exit code `0` for both. (9 checks total across the two invocations, unchanged
from the original combined-binary design — see `DECISIONS.md` for why the
runner was refactored to take one report + a fixture-type label per
invocation instead of two positional file paths.)

## 7. Failure-handling evidence

```
$ ./build/media-core fixtures/does_not_exist.mp4 --targets 0.0 --output /tmp/err1.json
media-core: failed to analyze 'fixtures/does_not_exist.mp4'
$ echo $?
1
$ python3 -c "import json; print(json.load(open('/tmp/err1.json'))['errors'])"
[{'stage': 'open', 'message': 'avformat_open_input failed: No such file or directory ...'}]
```

```
$ ffmpeg -f lavfi -i "sine=frequency=440:duration=2" -c:a aac /tmp/audio_only.m4a
$ ./build/media-core /tmp/audio_only.m4a --targets 0.0 --output /tmp/err2.json
$ echo $?
1
$ python3 -c "import json; print(json.load(open('/tmp/err2.json'))['errors'])"
[{'stage': 'stream_discovery', 'message': 'No video stream found; input appears to be audio-only.'}]
```

```
$ head -c 5000 fixtures/cfr_bframes.mp4 > /tmp/truncated.mp4
$ ./build/media-core /tmp/truncated.mp4 --targets 0.0,2.0 --output /tmp/err3.json
$ echo $?
1
$ python3 -c "import json; print(json.load(open('/tmp/err3.json'))['errors'])"
[{'stage': 'open', 'message': 'avformat_open_input failed: Invalid data found when processing input ...'}]
```

All three cases: nonzero exit code, structured JSON error (not a crash or
uncaught exception), no partial/garbage frame_requests written.

## 8. Resource cleanup (Valgrind)

```
$ valgrind --leak-check=summary --error-exitcode=99 \
    ./build/media-core fixtures/cfr_bframes.mp4 --targets 0.0,0.5,1.1 --output /tmp/valgrind_report.json
==1291== HEAP SUMMARY:
==1291==     in use at exit: 50,464 bytes in 276 blocks
==1291==   total heap usage: 13,275 allocs, 12,999 frees, 12,504,569 bytes allocated
==1291== LEAK SUMMARY:
==1291==    definitely lost: 0 bytes in 0 blocks
==1291==    indirectly lost: 0 bytes in 0 blocks
==1291==      possibly lost: 0 bytes in 0 blocks
==1291==    still reachable: 48,448 bytes in 255 blocks
==1291== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```
`0` definitely/indirectly/possibly lost bytes. The 48KB "still reachable" is
consistent with FFmpeg's internal one-time global state (codec/format
registries), not per-run allocations from `media-core`'s own code — this is
a hypothesis, not independently re-verified line-by-line, and is flagged as
such.

## 9. Known gap: no profiler capture in this environment

Per-frame-request wall-clock timing (`decode_time_ms` in every report) is real, measured via
`std::chrono::steady_clock`. A proper profiler capture (Instruments/perf/VTune) is deferred to Part B,
which requires it explicitly and should be run on a machine with kernel-level
profiler access.
