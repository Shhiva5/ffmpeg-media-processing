# Decisions (Part A)

## Assumptions

1. "One video stream" (per the assessment) means the tool operates on a
   single video track per invocation, selected via FFmpeg's own
   `av_find_best_stream` heuristic. Multi-video-track selection UI/logic is
   out of scope for this slice.
2. "Timeline time T" is relative to the stream's `start_time`, not to raw
   PTS=0. This matters for any file with a non-zero start time (not one of
   the two required fixtures, but called out as an optional bonus case) and
   keeps the frame-request API meaningful to a caller who thinks in
   "seconds into this clip," not "raw container ticks."
3. A "bounded sample" for the packet/frame trace means capped at
   `--trace-limit` entries (default 64) for what's *written to the report*,
   while the keyframe index and CFR/VFR verdict always scan the full file —
   because those two need whole-file information to be meaningful, while
   the packet/frame trace's job (per the spec) is just to *demonstrate*
   reordering, which a bounded prefix does perfectly well.

## Chosen approach

- **Direct FFmpeg C API, not a wrapper library.** The spec explicitly warns
  that "a complete player/editor SDK may not implement the core of the
  assessment for you." I used only `libavformat`/`libavcodec`/`libavutil`
  (plus `libswscale`, linked but currently unused) — no `libavplayer`-style
  helper, no ffms2, nothing that would do seeking/frame-selection for me.
- **One long-lived `AVFormatContext`/`AVCodecContext` pair per
  `MediaInspector`, reused across seeks.** This models how a real player
  would use the core (open once, seek/decode many times), rather than
  reopening per frame request, which would hide the actual cost of seeking
  vs. decoding in the timing numbers.
- **Selection rule based on measured decoded PTS, never
  `frame_index * (1/fps)`.** The spec explicitly forbids that shortcut. The
  rule (verbatim in `MediaInspector::selectionRuleText()`): seek to the
  latest keyframe ≤ T, decode forward, and the selected frame is the *last
  decoded frame whose PTS ≤ T*; PTS of the next frame (if any) is reported
  as `next_pts_s`/`duration_s`. Two explicit boundary flags —
  `clamped_to_first_frame` and `clamped_to_last_frame` — cover T before the
  first frame or beyond the last.
- **CFR/VFR verdict is measured, not trusted from container metadata.**
  `avg_frame_rate` can claim a constant rate for content that isn't. I
  decode a sample of frames (at least `max(trace_limit, 120)`), compute the
  presentation-order PTS deltas, and classify as CFR if the coefficient of
  variation of those deltas is under 2%. The 2% threshold is a pragmatic
  choice — real CFR streams show sub-percent jitter from rational-timebase
  rounding; genuine VFR jumps in testing were 60%+ CoV — but it is a fixed
  threshold, not a statistically derived one; see "Known limitations."
- **Keyframe detection via `AV_PKT_FLAG_KEY` on the demuxed packet**, not an
  independent bitstream parse of IDR NAL units. This is the standard,
  portable way to do it across containers/codecs without codec-specific
  parsing code, and FFmpeg's demuxers populate it reliably for the
  containers tested (MP4/H.264). Documented as a limitation because it
  would not catch a demuxer that fails to set the flag correctly for some
  other container/codec combination.
- **Vendored `nlohmann/json` (single header, MIT) rather than hand-rolled
  JSON.** The spec allows "small, permissively licensed helpers"; JSON
  serialization isn't the part of the assessment being evaluated, so using
  a well-known, dependency-free single header keeps the codec/timing logic
  the sole hand-written component.
- **The tool always writes a report, even on failure**, with a top-level
  `errors` array. This was a deliberate choice over throwing/crashing: it
  makes the JSON output a *consistent artifact* a caller can always parse,
  and matches the "return useful errors... and clean up all allocated
  resources" requirement more directly than a stack trace would.

## Discarded options

- **Reopening the format/codec context per frame request.** Simpler code,
  but it would conflate "cost of opening a file" with "cost of a seek,"
  which is exactly the kind of confound the assessment (Part B) asks me to
  be careful about. Rejected in favor of one persistent context + explicit
  seek/flush per request.
- **Parsing H.264 NAL units directly to identify IDR frames** instead of
  trusting `AV_PKT_FLAG_KEY`. More "correct" in principle, but it's
  codec-specific work that doesn't generalize, and would meaningfully
  expand scope for Part A without changing behavior on the fixtures I
  actually have. Documented as a known limitation instead.
- **A single global default frame-rate-based CFR/VFR threshold derived
  analytically** (e.g., from FFmpeg's own internal jitter tolerance) rather
  than a fixed 2%. I didn't have time to find and verify FFmpeg's own
  internal constants for this within the assessment window, so I picked an
  empirically-checked fixed threshold and documented it as a judgment call
  rather than presenting it as principled.

## Known limitations

1. Keyframe index correctness depends on demuxer-reported
   `AV_PKT_FLAG_KEY`, not an independent parse (see above).
2. CFR/VFR threshold (2% coefficient of variation) is a fixed heuristic.
3. `libswscale` is linked (part of the same pkg-config module set used for
   Part B's proxy work) but not exercised by any Part A code path yet.
4. No handling for edit lists / B-frame-heavy sparse-keyframe streams
   beyond what naturally falls out of the general seek+decode-forward loop
   — these were treated as the "optional third fixture" per the spec and
   not built out.

## Next two engineering steps

1. Add an independent H.264 IDU-level keyframe check (at least for the
   H.264 case, since that's what the required fixtures use) to validate the
   `AV_PKT_FLAG_KEY`-based keyframe index against ground truth, closing
   limitation (1).
2. Make the CFR/VFR threshold configurable and add a second detection
   signal (e.g., comparing `avg_frame_rate` to `r_frame_rate` divergence)
   so the verdict isn't solely dependent on one fixed statistical
   threshold, closing limitation (2).
