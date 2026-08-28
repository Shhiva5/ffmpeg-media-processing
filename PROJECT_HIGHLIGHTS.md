# Project Highlights

A quick tour for anyone browsing this repo — what it demonstrates and where
to look for evidence, not just claims.

## What this is

A C++17 media/codec engineering project built on FFmpeg's low-level
`libavformat`/`libavcodec` APIs (not a wrapper SDK), covering three areas:
timestamp-accurate frame seeking on real video streams, a codec-choice
benchmark for browser playback, and an architecture research memo comparing
C++/Rust/hybrid designs for a desktop media application. I built this
during a career break to keep hands-on systems engineering skills current.

## Things worth looking at directly

**1. A real, non-obvious correctness bug this project is designed to avoid**
`src/media_inspector.cpp`, `requestFrame()` — the frame-selection logic
never does `frame_index = time * fps`, which breaks on variable-frame-rate
content and on streams with a non-zero start time. Instead it seeks to the
nearest keyframe and decodes forward, tracking real presentation
timestamps. Proven with a deliberately adversarial test: requesting a
timeline time (`T=2.4999s`) placed one tick before a frame boundary at
30fps returns `2.4667s`, not `2.5s` — see `EVIDENCE.md` §4.

**2. Correctness verified, not just asserted**
- Memory safety: Valgrind, 0 definitely/indirectly/possibly lost bytes
  (`EVIDENCE.md` §8)
- Automated tests cross-checked against independently-measured ground
  truth (`ffprobe` used only as an oracle, never as implementation) —
  `tests/test_assertions.cpp`
- Failure paths (missing file, audio-only input, truncated/corrupt file)
  exercised with real bad inputs, not just reasoned about — all fail
  cleanly with structured JSON errors, never crash

**3. A benchmark that's honest about its own limits**
`part_b/EVIDENCE_PART_B.md` compares two H.264 encoding strategies
(all-intra vs. short-GOP) for browser scrubbing performance, using the
project's own tool to measure real decode cost per seek. Rather than
presenting one number as universal, it explicitly names five confounders
(no hardware decode tested, single-core sandbox, synthetic source
content, etc.) and states exactly what evidence would change the
recommendation.

**4. Research-grounded architecture reasoning, not a personal preference**
`part_c/DECISION_MEMO.md` recommends a hybrid C++/Rust architecture for a
hypothetical desktop media app, and the reasoning is backed by actually
checking current library maturity (`part_c/RESEARCH_LOG_PART_C.md`) rather
than asserting it — including finding a real production precedent
(RustDesk's `hwcodec`) for exactly the architecture shape recommended.

**5. Transparent about tooling limitations encountered along the way**
When a profiler (`gprof`) returned "no time accumulated," rather than
treating that as a dead end, `EVIDENCE_PART_B.md` §5 explains *why* — and
what that null result actually reveals (that virtually all measured cost
lives inside FFmpeg's own decode path, not in this project's code).

## How to run it

```bash
mkdir -p build && cd build && cmake .. && make -j"$(nproc)"
cd .. && ./scripts/generate_fixtures.sh
./build/media-core fixtures/cfr_bframes.mp4 --targets 0.0,0.5,1.1,2.4999 --output report.json
```
Full setup/usage in `README.md`.

## A note on AI use

I used Claude as an active coding collaborator throughout — writing code,
running builds, generating and cross-checking test fixtures, and drafting
documentation. `AI_USE.md` is a transparent, specific account of that
process: what was generated, what I verified and how.
