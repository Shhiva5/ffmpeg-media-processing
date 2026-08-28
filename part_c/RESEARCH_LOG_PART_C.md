# Research Log (Part C)

Same format as the main `RESEARCH_LOG.md`: pre-research assumption, then
confirmed/changed status with evidence. All sources accessed 2026-08-27
during this session.

## 1. Safe Rust/C++ FFI tooling (`cxx`) is mature enough for a real FFI boundary

- **Pre-research assumption:** the `cxx` crate is the standard way to do
  safer-than-raw-`extern "C"` Rust/C++ interop, and is mature/maintained,
  not experimental.
- **Status: confirmed.** [DOC/TESTED via package registry] `cxx` is at
  v1.0.198 on crates.io, MIT OR Apache-2.0, with 71.8M all-time downloads
  and 245 published versions — an actively maintained, widely-used crate,
  not a proof-of-concept. It generates a bridge with compile-time static
  assertions on both sides of the boundary, giving "zero or negligible
  overhead... no copying, no serialization" per its own documentation.
  Source: https://crates.io/crates/cxx, https://cxx.rs/ (accessed
  2026-08-27).

## 2. Rust's codec ecosystem still depends on unsafe FFI to FFmpeg for real codec work

- **Pre-research assumption:** there might be a mature, pure-Rust H.264/HEVC
  decoder by now that a Rust-first architecture could use instead of
  wrapping FFmpeg — this would materially change the Rust-first option's
  viability.
- **Status: changed (my assumption was too optimistic).** No mature
  pure-Rust H.264/HEVC *decoder* was found. What exists are FFI bindings to
  FFmpeg itself (`ffmpeg-next`/`ffmpeg-sys-next`, `rusty_ffmpeg`), which are
  low-level `unsafe` wrappers around the same C library this project
  already uses in Part A/B — not a safety improvement over the existing
  C++ core, just a different calling convention on top of the same
  `unsafe` surface. (Pure-Rust codec work does exist for some formats,
  e.g. `symphonia` for audio and `rav1e` for AV1 *encoding*, but nothing
  found covers H.264/HEVC *decoding*, which is what this project's
  fixtures and proxies actually use.)
  Source: https://github.com/zmwangx/rust-ffmpeg-sys,
  https://crates.io/crates/ffmpeg-sys-next,
  https://www.ffmpeg-micro.com/blog/how-to-use-ffmpeg-with-rust (accessed
  2026-08-27).

## 3. `wgpu` is production-viable for cross-platform GPU compositing from Rust

- **Pre-research assumption:** `wgpu` (Rust's WebGPU implementation) is
  still mostly a browser/wasm-focused project and may not be a serious
  option for a native desktop compositor.
- **Status: changed.** `wgpu` runs natively on Vulkan, Metal, D3D12, and
  OpenGL (not just wasm/WebGPU), and is used as the WebGPU backend inside
  Firefox, Servo, and Deno — i.e. it is already shipping in production,
  non-experimental software, including native (non-browser) contexts.
  [INFERENCE, not independently benchmarked here] this makes it a
  credible native GPU-compositing layer for a Rust-first or Rust-side
  hybrid option, which is a stronger position than I assumed going in.
  One caveat found: the D3D11 backend was recently dropped in favor of
  ANGLE-on-D3D11 as a fallback, and `rust-gpu` (a related but separate
  shader-focused project) had its Embark-maintained repository archived
  in October 2025 — flagging that "the Rust GPU story" is not uniformly
  stable across every sub-project, only `wgpu` itself specifically.
  Source: https://wgpu.rs/, https://docs.rs/wgpu/,
  https://www.blog.brightcoding.dev/2025/09/30/cross-platform-rust-graphics-with-wgpu-one-api-to-rule-vulkan-metal-d3d12-opengl-webgpu
  (accessed 2026-08-27).

## 4. GStreamer's Rust bindings are a viable alternative orchestration layer, with a documented caveat

- **Pre-research assumption:** GStreamer (a C media framework, alternative
  to raw FFmpeg) has Rust bindings that could be an alternative Hybrid
  substrate to a hand-rolled FFmpeg FFI layer.
- **Status: confirmed, with an important caveat found.**
  `gstreamer-editing-services` (GES, GStreamer's own nonlinear-editing
  library) has Rust bindings, but its own documentation explicitly warns:
  "The GStreamer Editing Services API is not Thread Safe and before the
  1.16 release this was not properly expressed in the code, leading to
  possible data unsafety even in the rust bindings." [INFERENCE] this is
  a concrete, documented example of exactly the risk this memo flags for
  any hybrid/wrapped-C-library approach: a safe-looking Rust binding can
  still expose an underlying library's real thread-safety violations if
  the binding doesn't (or can't) enforce them at compile time.
  Source: https://lib.rs/crates/gstreamer-editing-services (accessed
  2026-08-27).

## 5. Real-world precedent: even a Rust-first application uses a C++ FFI backend for hardware codec access

- **Pre-research assumption:** none stated in advance; this was found
  opportunistically while researching hardware-decode API access from
  Rust and is included because it directly supports (or undermines) the
  Hybrid recommendation.
- **Status: found, supports the Hybrid recommendation.** `hwcodec`
  (used by RustDesk, a remote-desktop application) integrates hardware
  video codec acceleration (Intel Quick Sync/QSV via oneVPL, plus
  NVIDIA/AMD paths) by implementing the actual codec integration in C++
  (`cpp/mfx/mfx_decode.cpp`, `mfx_ffi.h`) and exposing it to Rust
  (`src/vram/mfx.rs`) through an FFI layer. [INFERENCE] this is a live,
  shipping example of the exact Hybrid shape this memo recommends —
  hardware/platform codec integration in C++, orchestration/application
  logic in Rust — rather than a purely theoretical option.
  Source: https://deepwiki.com/rustdesk-org/hwcodec/5.3-intel-support
  (accessed 2026-08-27).

## 6. Current FFmpeg/libavcodec versions (for currency, not used directly in Part C)

- **Status: confirmed, noted for context.** FFmpeg's current stable release
  is 8.1.1 (4 May 2026); `libavcodec` standalone is at 62.11.100 (20
  November 2025). This is newer than the FFmpeg 4.4.2 / libavcodec
  58.134.102 available via `apt` in this sandbox and used for Parts A/B —
  flagged here because any real desktop-architecture decision should
  re-verify hardware-decode and licensing behavior against whatever
  FFmpeg version is actually vendored, not assume Part A/B's sandbox
  version is current.
  Source: https://en.wikipedia.org/wiki/FFmpeg,
  https://en.wikipedia.org/wiki/Libavcodec (accessed 2026-08-27).
