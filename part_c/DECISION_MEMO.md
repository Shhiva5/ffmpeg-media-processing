# Desktop Media & Timeline Architecture — Decision Memo

**Context:** the browser-based nonlinear editor remains the primary
product. This memo evaluates an incremental desktop effort for stronger
native codec support, hardware acceleration, and demanding timelines,
without requiring an immediate rewrite of the browser product. Sources for
factual claims below are in `RESEARCH_LOG_PART_C.md`.

## Options compared

| Criterion | C++-first | Rust-first | Hybrid (recommended) |
|---|---|---|---|
| Demux/decode, hw decode/encode (Win/macOS; Linux noted) | FFmpeg, DXVA/D3D11VA, VideoToolbox, VAAPI are all C/C++-native. Zero FFI tax; already proven in Parts A/B. | No mature pure-Rust H.264/HEVC decoder exists; `ffmpeg-next`/`rusty_ffmpeg` are `unsafe` FFI to the same C library — no safety gain, just a second calling convention on the riskiest code. | C++ owns codec/platform integration (reuses Parts A/B unmodified); Rust never touches raw decode — the shape RustDesk's `hwcodec` already ships for hardware codec access. |
| GPU compositing, surface transfer w/o avoidable copies | Native D3D12/Metal/Vulkan; full control, manual thread/lifetime bookkeeping. | `wgpu` is production-grade — backs Firefox/Servo/Deno's WebGPU natively (not just wasm) across Vulkan/Metal/D3D12. | Rust compositor via `wgpu`, receiving frame handles from C++ decode via an explicit ownership contract (§ below). |
| Audio mixing, master clock, A/V drift, multi-track scheduling | Hand-written scheduler; correctness is manual discipline. | Ownership model directly targets shared clock/buffer state across threads. | Scheduler/clock in Rust — concurrency-heavy, safety-sensitive, no raw bitstreams touched. |
| Timeline model, UI/thread boundary | Manual discipline to keep heavy work off the UI thread. | `Send`/`Sync` make "must not touch UI thread" compile-time-checkable. | Timeline + UI-thread isolation in Rust, compile-time guarantee. |
| Memory ownership, cancellation, backpressure, crash isolation | Fully manual (Part A's RAII discipline, verified via Valgrind, 0 leaks). | Compiler-enforced within Rust code; crash isolation still needs process/thread boundaries either way. | Compiler-enforced for orchestration; the FFI boundary is the one unenforced seam — Risk #1. |
| FFI/ABI stability, build/packaging, team learning cost | One toolchain, lowest cost; extends this project's existing CMake setup. | One toolchain only if no C++ is needed — but decode/hw-accel above shows that's not realistic. | Two toolchains (Cargo+CMake), one FFI boundary via `cxx` (v1.0.198, MIT/Apache-2.0, 71M+ downloads, mature). Real cost: two build systems, two packaging paths, dual hiring. |
| Codec/library licensing (flag for specialist review) | Same FFmpeg/libx264 GPL exposure already flagged in Part A's README. | Same underlying C libraries via FFI — licensing attaches to the linked library, not the calling language. | Same as both; orthogonal to the C++/Rust split. |
| Timing-rule/fixture consistency across browser, desktop, export | Same C++ core (`MediaInspector`) can back browser (via WASM, `HANDOFF_CONTRACT.md` §10), desktop, and export. | Requires re-deriving the selection rule in Rust (duplication risk) or FFI-wrapping the C++ core anyway — i.e. Hybrid. | Reuses the exact selection rule, fixtures, and JSON schema validated in Parts A/B across all three surfaces. |

## Recommendation

**Hybrid: C++ for codec/platform/GPU-surface integration, Rust for
orchestration (timeline model, scheduler/clock, compositing via `wgpu`,
cancellation/backpressure), with `cxx` as the FFI boundary.**

**Most important reason:** every row where "Rust-first" looked attractive
(compositing, scheduling, timeline, UI-thread isolation) is a
concurrency-heavy *orchestration* problem — exactly what Rust's ownership
model is best at. Every row where "C++-first" is inevitable (demux/decode,
hardware codec paths, platform GPU APIs) is already built and evidenced in
Parts A/B. Hybrid keeps both strengths without asking Rust to re-solve a
problem C++ already solved, or C++ to hand-roll the concurrency safety Rust
gives for free.

**Strongest rejected alternative:** Rust-first. Rejected because research
(`RESEARCH_LOG_PART_C.md` #2) found no mature pure-Rust H.264/HEVC decoder
— a "Rust-first" core would still FFI into FFmpeg for code this project
already has working in C++, at more cost for no safety benefit on the part
that matters most. **Evidence that would flip this:** a mature, audited,
pure-Rust hardware-accelerated H.264/HEVC decoder with real production
adoption — Rust-first's safety argument would then extend all the way down
the stack instead of stopping at an FFI wall.

**Component boundary:**
- **C++:** demux/decode (Part A's `MediaInspector`, unmodified), hardware
  codec integration (DXVA/D3D11VA, VideoToolbox, VAAPI), platform
  GPU-surface acquisition.
- **Rust:** frame cache, scheduler/master clock, audio mixing, GPU
  compositor (via `wgpu`), timeline model, cancellation/backpressure
  (per `part_b/HANDOFF_CONTRACT.md` §3/§7), UI/shell.
- **`cxx` FFI boundary:** sits between decode (C++) and frame cache (Rust)
  — the single seam where a decoded frame handle crosses languages, and
  therefore the one place ownership must be manually verified rather than
  compiler-enforced (Risk #1).

**Browser-owned vs. shared during the desktop spike:** the browser product
keeps its own WASM-compiled build of the C++ demux/decode core (per
`HANDOFF_CONTRACT.md` §10) entirely browser-owned — no Rust orchestration
layer ships to the browser in this phase. What's shared is the C++
demux/decode source itself, the selection rule, and the conformance
fixtures/JSON report schema (Parts A/B) — both browser and desktop consume
the identical core logic, just through different host bindings (WASM vs.
native `cxx`).

**Top 3 unresolved risks + smallest experiment:**
1. *The `cxx` boundary is the one place safety isn't compiler-enforced —
   GStreamer's own Rust bindings show even mature projects can leak real
   thread-safety bugs through a "safe" binding* (`RESEARCH_LOG_PART_C.md`
   #4) — smallest experiment: prototype the frame-handle ownership/release
   contract from `HANDOFF_CONTRACT.md` §6 across a real `cxx` bridge and
   stress-test under ThreadSanitizer/Miri.
2. *Two build systems (CMake + Cargo) may cost more in packaging/CI than
   estimated* — smallest experiment: get one cross-platform (Win + macOS)
   CI pipeline building both sides into a single installer before
   committing further engineering time.
3. *`wgpu`'s recent D3D11 backend drop could matter on older Windows
   hardware* (`RESEARCH_LOG_PART_C.md` #3) — smallest experiment: run
   `wgpu`'s ANGLE-on-D3D11 fallback on the actual minimum-spec Windows
   hardware this product targets and measure compositing performance.

