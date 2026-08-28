# Part B Evidence

All numbers below are observed facts from commands actually run in this
session (see exact commands under each section). Machine/build info matches
`EVIDENCE.md` (Part A): AMD Ryzen 7 Processor @ 3.30GHz, Ubuntu 22.04 container,
GCC 11.4.0, FFmpeg 4.4.2 (libavformat 58.76.100 / libavcodec 58.134.102).

## 1. Source and proxy generation

```
$ ./scripts/generate_proxies.sh
```

- `source_master.mp4`: 30s, 1280x720, 30fps, H.264 (libx264 veryfast, CRF 20),
  Mandelbrot zoom render (`start_scale=3.0:end_scale=0.02`) — chosen over a
  flat test pattern specifically because it has continuous, high-frequency
  motion detail, which exercises inter-frame prediction realistically.
  900 frames total, ~22.6 Mbps average bitrate, 84.9 MB.

- `proxy_allintra.mp4`: 720p, `-g 1 -bf 0` (every frame is a keyframe).
  **113,521,022 bytes (113.5 MB)**, average bitrate **30.27 Mbps**.
  Confirmed via `ffprobe -show_entries frame=pict_type proxy_allintra.mp4`:
  900 I-frames, 0 P/B-frames.

- `proxy_shortgop.mp4`: 720p, `-g 60 -bf 2` (keyframe every 2s at 30fps).
  **79,759,406 bytes (79.8 MB)**, average bitrate **21.27 Mbps**.
  Confirmed: 15 I-frames (matches 900 frames / 60 = 15 exactly), 300
  P-frames, 585 B-frames.

**Size/bitrate delta:** all-intra is **42.3% larger** than short-GOP
(113,521,022 / 79,759,406 = 1.423). This is smaller than the 3-5x gap often
quoted for all-intra vs. long-GOP encodes of natural video — the likely
reason (flagged as an inference, not independently re-verified) is that the
Mandelbrot source's fine, chaotic fractal detail is closer to spatial noise
than typical camera footage, which limits how much redundancy short-GOP's
inter-frame prediction can actually exploit between frames. A natural-video
source would likely show a larger gap. This is called out explicitly as a
confounder in §4.

## 2. Seeded target list

Fixed, hand-picked (not randomly regenerated per run) 10 target times,
chosen to cover a deliberate spread relative to the short-GOP proxy's
2-second GOP boundaries (keyframes at t=0,2,4,...,28s):

```
0.0, 0.1, 1.0, 1.9, 5.5, 10.0, 13.75, 21.333, 27.9, 29.9
```

Rationale: `0.0`/`10.0` land exactly on keyframes (cheapest case for
short-GOP); `0.1` is just after a keyframe (near-cheapest); `1.9`/`13.75`/
`27.9` sit just before the *next* keyframe (near-worst-case, close to a full
GOP of decode work); `5.5`/`21.333` are arbitrary mid-GOP points; `29.9` is
near end-of-file (900 frames / 30fps = 29.9\overline{6}s duration), testing
the boundary/clamping path near EOF.

## 3. Per-target results (measured via `media-core`, not ffprobe)

```
$ ./build/media-core part_b/proxy_allintra.mp4 --targets 0.0,0.1,1.0,1.9,5.5,10.0,13.75,21.333,27.9,29.9 --output part_b/allintra_report.json --trace-limit 10
$ ./build/media-core part_b/proxy_shortgop.mp4 --targets 0.0,0.1,1.0,1.9,5.5,10.0,13.75,21.333,27.9,29.9 --output part_b/shortgop_report.json --trace-limit 10
$ ./scripts/extract_frame_requests_data.py part_b/allintra_report.json
$ ./scripts/extract_frame_requests_data.py part_b/shortgop_report.json
```

| target_s | AI frames_processed | AI decode_time_ms | SG frames_processed | SG decode_time_ms | SG seek_keyframe_pts_s |
|---:|---:|---:|---:|---:|---:|
| 0.0    | 2 |  7.269 | 2  |   7.286 | 0.0  |
| 0.1    | 2 |  6.689 | 5  |  12.624 | 0.0  |
| 1.0    | 2 |  8.322 | 32 |  71.759 | 0.0  |
| 1.9    | 2 |  9.816 | 59 | 147.751 | 0.0  |
| 5.5    | 2 |  7.573 | 47 | 126.905 | 4.0  |
| 10.0   | 2 | 10.742 | 2  |  19.791 | 10.0 |
| 13.75  | 2 | 14.454 | 54 | 257.678 | 12.0 |
| 21.333 | 2 | 18.008 | 41 | 264.478 | 20.0 |
| 27.9   | 2 | 15.939 | 59 | 482.456 | 26.0 |
| 29.9   | 2 | 22.086 | 59 | 486.264 | 28.0 |

**Median / worst decode time:**

| Variant | Median (ms) | Worst (ms) |
|---|---:|---:|
| All-intra | 10.279 | 22.086 |
| Short-GOP | 137.328 | 486.264 |

**Frames processed per request:** all-intra is a flat **2** for every
target (1 keyframe decode + the loop's one-frame lookahead to determine
`next_pts_s`, per the selection rule). Short-GOP ranges from **2 to 59**,
median **44** — scaling with distance from the preceding keyframe, exactly
as expected from GOP structure. (59 is the practical ceiling here — some
GOPs decode slightly fewer frames before crossing the target depending on
B-frame reorder buffering, so it's not a clean multiple of the naive
"frames since keyframe" count.)

**Correctness spot-check:** at `target_s=27.9`, `seek_keyframe_pts_s=26.0`
— confirms the seek chose the correct GOP boundary (26s is the latest
keyframe ≤ 27.9s in a 2s-GOP stream), consistent with the Part A selection
rule.

## 4. Caching and confounders

**Caching:** this container has no permission to drop the OS page cache
(`echo 3 > /proc/sys/vm/drop_caches` → `Permission denied`, verified), so a
true cold-cache measurement wasn't possible here. As a substitute, the
short-GOP benchmark was run twice in immediate succession:

| target_s | Run 1 (ms) | Run 2 (ms) |
|---:|---:|---:|
| 0.0    | 7.280   | 7.291   |
| 1.9    | 146.762 | 143.733 |
| 13.75  | 253.676 | 247.652 |
| 27.9   | 478.348 | 477.465 |
| 29.9   | 474.246 | 472.134 |

The two runs are within ~1-3% of each other. **Interpretation (a hypothesis,
not a proven cause):** this suggests decode cost here is CPU-bound rather
than I/O-bound — re-reading an already-cached ~80MB file costs very little
either way, so seeing near-identical timings on repeat runs is *consistent
with* I/O/caching not being the dominant factor, but doesn't rule out that
both runs simply hit a warm cache equally (see §5 for corroborating
evidence from `/usr/bin/time -v`, which shows near-zero system time and
zero major page faults on the timed runs — i.e., no blocking disk I/O was
observed in either).

**Confounders, named explicitly (per the assessment's requirement not to
overclaim from one short benchmark):**

- **Hardware decode not used.** All numbers are software decode via
  `libavcodec`'s built-in H.264 decoder. A browser using platform hardware
  decode (VideoToolbox/DXVA/VA-API) would likely show a *much smaller* gap
  between all-intra and short-GOP, since hardware decoders often have
  dedicated reference-frame handling that doesn't scale linearly with GOP
  depth the way software decode's motion-compensation loop does here.
- **Thread count.** `dec_ctx_->thread_count` was never explicitly set, so
  FFmpeg's default threading heuristic applied.
  Multi-core hardware would likely narrow the gap for short-GOP, since
  frame-level parallelism helps most when there's a run of dependent
  P/B-frames to decode.
- **Source complexity.** The Mandelbrot source's noise-like fractal detail
  (see §1) is unusually hard to predict inter-frame, which may *understate*
  short-GOP's typical relative disadvantage vs. real footage with more
  spatial/temporal redundancy (real footage would likely show all-intra's
  size penalty being *larger*, since short-GOP would compress more
  effectively).
- **Single CPU core, containerized/virtualized environment.** Absolute
  timings here should not be treated as representative of a browser
  playback lead's actual deployment hardware; only the *relative* pattern
  (short-GOP costs more, scaling with GOP depth) should be treated as
  robust.
- **Codec build.** This is the stock Ubuntu 22.04 `libavcodec`/`libx264`
  build; no custom SIMD/asm tuning was verified.

## 5. Performance-counter evidence

Two alternatives were used instead, both real, both run against the actual
binary:

**a) `gprof` (application-level call graph):**
```
$ cmake .. -DCMAKE_CXX_FLAGS="-pg" -DCMAKE_EXE_LINKER_FLAGS="-pg"
$ make
$ ./build/media-core part_b/proxy_shortgop.mp4 --targets 27.9,29.9,13.75,21.333,1.9 \
    --output /tmp/profile_report.json --trace-limit 5
$ gprof ./media-core gmon.out
Flat profile:
  no time accumulated
```
This "no time accumulated" result is itself the finding, not a failure:
`-pg` only instruments object files compiled with that flag (our own
sources). `libavformat`/`libavcodec` are precompiled system shared
libraries, not instrumented. The fact that gprof attributes essentially
zero self-time to any of `media-core`'s own functions (`requestFrame`,
`buildKeyframeIndexAndPacketTrace`, JSON serialization, etc.) is evidence
that virtually all wall-clock time is spent *inside FFmpeg's decode path*,
not in this project's own orchestration code.

**b) `/usr/bin/time -v` (OS-level resource counters), corroborating (a):**

```
$ /usr/bin/time -v ./build/media-core part_b/proxy_shortgop.mp4 \
    --targets 0.0,0.1,1.0,1.9,5.5,10.0,13.75,21.333,27.9,29.9 \
    --output /tmp/time_shortgop.json --trace-limit 5
```
| Metric | Short-GOP (10 targets + analyze pass) | All-intra (10 targets + analyze pass) |
|---|---:|---:|
| User time | 2.35s | 0.68s |
| System time | 0.10s | 0.06s |
| CPU utilization | 99% | 98% |
| Elapsed (wall) | 2.54s | 0.7s |
| Major page faults | 0 | 0 |
| Max RSS | 44.8 MB | 41.3 MB |

**Dominant measured cost:** user-mode CPU time inside the H.264 decode path
(entropy decoding + motion compensation across the run of P/B-frames between
each seek point and its target), not I/O, not memory, not this project's
own orchestration/JSON code. Evidence: (1) system time is very small and flat in
both runs — no meaningful syscall/I/O overhead; (2) zero major page faults —
no blocking disk reads; (3) CPU utilization ~99% — the process is
compute-bound, not waiting on anything; (4) short-GOP's ~3.5x higher user
time (2.35s vs. 0.68s) tracks its much higher `frames_processed` per
request (median 44 vs. 2) far more than it tracks any I/O metric, which is
essentially flat between the two runs.

**Correctness check used for this profiling run:** the same JSON report
mechanism validated in Part A (`media-core-tests`, 9/9 passing) — the
profiling runs used the identical binary and produced structurally valid
reports (`found: true`, plausible `timing_error_s`) for all requested
targets, confirming the profiled run was doing real, correct decode work
and not failing silently.

**Build flags for profiled run:** `-O2 -g -pg` (RelWithDebInfo + `-pg`);
note this differs from the `-O2 -g` (no `-pg`) build used for the actual
timing numbers in §3, since instrumentation overhead from `-pg` would bias
wall-clock measurements. `-pg` was used only for the gprof call-graph run,
never for the numbers in the table in §3.

## 6. Recommendation to a browser playback lead

**Recommendation: short-GOP for linear/streamed playback delivery,
all-intra (or a hybrid short-GOP with a *denser* keyframe interval, e.g.
0.5-1s instead of 2s) specifically for a timeline scrubbing/preview UI.**

**Most important reason:** the measured seek cost gap is large and
monotonic with GOP depth (median 137ms vs. 10ms, worst-case 486ms vs. 22ms
in this test) purely in software decode on a single core — a scrubber
firing several requests per second against short-GOP content on comparable
hardware would visibly stutter, while all-intra stays under ~50ms even in
the worst case tested. Bandwidth/storage cost (42% larger files here) is
usually the more tolerable trade-off for a scrub-preview stream specifically
(which is often a separate, lower-resolution proxy anyway) than a
sluggish-feeling scrubber.

**Strongest rejected alternative:** ship short-GOP everywhere (simpler
pipeline, smaller files, matches how the *primary* delivery stream would be
encoded anyway) and rely on hardware-accelerated decode to close the gap.

**Evidence that would change this recommendation:** a repeat of this exact
benchmark using the target browser's actual hardware-accelerated decode
path (not software `libavcodec`) on realistic (non-fractal) source footage.
If hardware decode collapses the frames-processed-to-time relationship
(i.e., decoding 40+ frames from a keyframe costs close to the same as
decoding 2), the size/bandwidth cost of all-intra would no longer be
justified for scrubbing either, and short-GOP alone would be the right call
platform-wide.

**Top unresolved risks and the smallest experiment for each:**
1. *Hardware decode may erase the gap this benchmark measured entirely* —
   smallest experiment: repeat §3's exact 10-target benchmark using the
   browser's `VideoDecoder` (WebCodecs API) against both proxies on target
   hardware, comparing wall-clock time per request from JS, not just
   software `media-core`.
2. *Real footage may show a much larger size penalty for all-intra than the
   42% measured here* (see §1's noise-source caveat) — smallest experiment:
   re-run the exact same proxy-generation + benchmark pipeline against one
   real, high-motion clip (e.g., a licensed/self-shot sample) and compare
   the size delta.
3. *Multi-core scaling could change the relative decode-time gap* — smallest
   experiment: re-run §3 on multi-core hardware with `dec_ctx_->thread_count`
   explicitly set (e.g., 4), since this container's single visible vCPU
   makes today's numbers a worst-case for short-GOP specifically.
