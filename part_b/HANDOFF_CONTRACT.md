# Browser/Native Handoff Contract

The smallest contract `media-core`'s underlying logic (`MediaInspector` in
Part A) would need to offer a browser playback lead today, and later a
native (desktop) renderer, without requiring either caller to know FFmpeg's
C API directly. This is pseudocode, not a compiled interface — the goal is
to nail down semantics before committing to a binding (WASM, N-API, FFI,
etc.).

## 1. Handle lifecycle

```
handle = MediaCore.open(source: BytesSource) -> Result<MediaHandle, OpenError>
MediaCore.close(handle: MediaHandle) -> void
```

- `BytesSource` is deliberately abstract at this layer: a local file path
  natively, or a `ReadableStream`/buffer-backed source in a browser/WASM
  context. The demux/decode core (Part A's `MediaInspector`) doesn't care
  which; only the I/O adapter underneath does.
- `open()` is the only call that can fail due to "this isn't a supported
  media file" — see §7 (Capability failures).
- `close()` must be idempotent and safe to call even if a request is
  in-flight (see §3, cancellation) — it should synchronously invalidate the
  handle and asynchronously release FFmpeg resources, mirroring
  `MediaInspector`'s destructor (`avcodec_free_context` +
  `avformat_close_input`), never leaving a caller holding a dangling handle.

## 2. Time-base conversion

```
StreamInfo {
  codec: string
  width, height: int
  pixel_format: string
  duration_seconds: float64
  start_time_seconds: float64   // NOT always 0 -- see below
  cfr_or_vfr: "CFR" | "VFR" | "UNKNOWN"
}

MediaCore.getStreamInfo(handle) -> StreamInfo
```

- **All timestamps crossing this boundary are float64 seconds, relative to
  the stream's own start**, never raw `AVStream` ticks and never absolute
  wall-clock time. This mirrors the choice already made in
  `MediaInspector::tsToSeconds`/`secondsToTs` in Part A — the caller should
  never need to know a stream's `time_base` rational at all.
- `start_time_seconds` is surfaced explicitly rather than silently absorbed,
  because a caller building a scrubber UI needs to know whether "the start
  of the clip" and "the start of the file" are the same instant (they
  often aren't — see the optional non-zero-start-time fixture case).

## 3. Frame-request semantics, identity, and cancellation

```
RequestId = opaque string/int, caller-generated

MediaCore.requestFrame(handle, target_seconds: float64, request_id: RequestId)
  -> Promise<FrameResult> | callback(FrameResult)

MediaCore.cancelRequest(handle, request_id: RequestId) -> void

FrameResult {
  request_id: RequestId
  target_seconds: float64
  status: "delivered" | "cancelled" | "error"
  frame: DecodedFrame | null      // present only if status == "delivered"
  selected_pts_seconds: float64
  next_pts_seconds: float64 | null
  fallback: "clamped_to_first_frame" | "clamped_to_last_frame" | null
  timing_error_seconds: float64
  error: string | null
}
```

- **Selection rule is exactly Part A's**, restated as the contract's own
  spec (verbatim from `MediaInspector::selectionRuleText()`): the selected
  frame is the last decoded frame whose PTS ≤ target, found by seeking to
  the latest keyframe ≤ target and decoding forward; boundary cases are
  explicit (`clamped_to_first_frame`/`clamped_to_last_frame`), never a
  silent frame_index × fps approximation.
- **Cancellation is caller-driven and best-effort, not guaranteed
  instantaneous.** A scrubber firing 5 requests in 100ms should call
  `cancelRequest` for stale IDs; the core checks a cancellation flag between
  decoded frames in its decode-forward loop (the same loop as
  `requestFrame()` in `media_inspector.cpp`), so cancellation latency is
  bounded by "time to decode one more frame," not instant, and that bound
  should be documented per-codec/GOP-depth rather than promised as zero.
- **Only one in-flight decode per handle** is assumed for this slice (see
  §5, backpressure) — concurrent requests on the same handle should queue,
  not race on the shared `AVCodecContext`, mirroring the single persistent
  decoder context design in `MediaInspector`.

## 4. Presentation order

- The contract **only ever returns frames in presentation order** (PTS
  order), regardless of decode order. This is already true structurally in
  Part A: frames are only surfaced to the caller after
  `avcodec_receive_frame()` + reordering via `best_effort_timestamp`, so
  the B-frame-reordering problem is fully absorbed below this boundary.
  A caller of this contract should never need to know what a B-frame is.

## 5. Colour/pixel metadata

```
DecodedFrame {
  width, height: int
  pixel_format: string        // e.g. "yuv420p" -- caller converts if needed
  color_range: "limited" | "full" | "unknown"
  color_space: string          // e.g. "bt709", "bt601" -- passthrough from AVFrame
  data: OpaqueBufferHandle      // see ownership, below
  linesize: int[]               // per-plane stride, needed for non-tight-packed buffers
}
```

- Pixel format and color metadata are passed through, not normalized or
  converted, at this layer — conversion (e.g. YUV→RGB for a `<canvas>`) is
  a renderer-side concern and belongs above this contract, not inside the
  core. This keeps the core reusable for a native renderer with different
  color-management needs than a browser `<canvas>`.

## 6. Ownership / release

- `DecodedFrame.data` is an **opaque handle owned by the core**, not a
  caller-owned buffer, mirroring `AVFrame`'s own refcounted-buffer model.
- The caller must call `MediaCore.releaseFrame(handle, frame)` when done
  (uploading to a GPU texture, copying to a canvas, etc.); the core is free
  to reuse/pool the underlying buffer only after release.
- **Across an FFI or WASM boundary specifically**, this is the single
  highest-risk part of the contract: a native caller (C++/Rust renderer)
  can hold a raw pointer, but a WASM/JS caller cannot — for WASM, `data`
  should be a typed-array view into WASM linear memory that becomes invalid
  after `releaseFrame`, and the contract must specify that using a view
  after release is undefined behavior, not a safe no-op, so this needs
  explicit documentation and ideally a debug-build bounds check.

## 7. Backpressure

- If requests arrive faster than the core can decode (e.g., a scrubber
  firing every mousemove event), the contract exposes an explicit **queue
  depth limit**, not unbounded queuing:
  ```
  MediaCore.setMaxPendingRequests(handle, n: int)
  ```
  When exceeded, the *oldest* pending, not-yet-started request is
  auto-cancelled (status `"cancelled"`) rather than silently dropped or
  blocking the caller. This gives a scrubber UI a simple policy: "the most
  recent N requests always win," without every caller having to hand-roll
  request coalescing.

## 8. Capability failures

```
OpenError = "unsupported_container" | "unsupported_codec" | "no_video_stream"
          | "corrupt_or_truncated" | "io_error"
```

- Directly maps to Part A's existing `AnalysisError` stages (`open`,
  `stream_discovery`, `decoder_lookup`, `decoder_open`) — the contract's
  job is to collapse those into a small, stable enum a UI layer can branch
  on (e.g., show "this file format isn't supported" vs. "this file appears
  damaged"), rather than exposing raw FFmpeg error strings to product code.
- A request-level decode failure mid-stream (`FrameResult.status ==
  "error"`) is distinct from an open-time failure: the handle stays valid,
  only that one request failed, mirroring Part A's non-fatal-per-packet
  error handling in `decodeBoundedTraceAndCfrVfrVerdict`.

## 9. Test fixtures as the contract's own test suite

- The two conformance fixtures from Part A (`cfr_bframes.mp4`,
  `vfr_known_pts.mp4`) and their ground-truth files are the contract's
  reference test suite too: any binding implementation (WASM, native FFI,
  whatever comes next) should be required to pass the same 9 assertions
  `media-core-tests` already checks (§ EVIDENCE.md in Part A), plus new
  binding-specific tests for cancellation latency and buffer-release safety
  (§3, §6) that don't apply to the CLI tool itself.

## 10. WebAssembly vs. platform-native split

| Component | WASM-compilable? | Reasoning |
|---|---|---|
| Demux (`libavformat` core logic, packet reading, keyframe indexing) | **Yes** | Pure computation over bytes; no OS-specific syscalls beyond basic I/O, which WASM's host environment (a `ReadableStream`/file API adapter) can satisfy |
| Software decode (`libavcodec`'s built-in decoders, as used throughout Part A/B) | **Yes**, with a bitrate/CPU caveat | This is exactly what Part A/B measured — works, but §5 of EVIDENCE_PART_B.md shows it's meaningfully CPU-bound; WASM adds further overhead vs. native, so this path is realistic for the browser today but not free |
| Hardware-accelerated decode (VideoToolbox/DXVA/VA-API) | **No** | Requires direct OS/GPU driver access; must stay platform-native. A WASM build would need to fall back to software decode or hand off to a browser-native `VideoDecoder` (WebCodecs) instead of `libavcodec` |
| GPU surface/texture handoff for rendering | **No** | Needs a native graphics API (Metal/D3D/Vulkan/WebGPU) on the far side; the core should hand back raw decoded planes (§5) and let a platform-specific renderer layer do the upload |
| JSON/report serialization, CFR/VFR statistics, keyframe index math | **Yes** | Same reasoning as demux — pure computation |

**Practical recommendation:** compile the demux + software-decode core
(everything Part A's `MediaInspector` already does) to WASM as the shared
component between browser and native, and keep hardware decode + GPU
compositing as platform-native code that sits *above* this contract on
each platform — which is exactly the boundary this contract already draws
in §5/§6 (opaque frame handles, pixel data handed back without any
rendering-specific assumptions baked in).
