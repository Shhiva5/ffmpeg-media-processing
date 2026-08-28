# AI Use Disclosure (Part A)

## Tools/models used

- **Claude (Anthropic)**, used interactively in an agentic coding
  environment with shell/file-editing tool access, for the entirety of Part
  A: architecture, C++ implementation, build configuration, fixture
  generation scripting, running the build/tests/Valgrind/ffprobe checks,
  and drafting this documentation set.

## Important prompts (condensed)

1. "Explain this assignment and give starting guidance" — produced a
   breakdown of the assessment and a time budget; no code yet.
2. "What does video stream in Part A mean" — clarifying question about
   demuxing/multi-stream containers, answered conversationally.
3. "What all I need to know for completion of Part-A" — produced the
   technical checklist (timestamps, DTS/PTS, keyframes, selection rule,
   JSON schema, error handling, fixtures) that became the actual
   implementation plan.
4. "Generate Part-A code template" — the actual build: CMake project, vendoring
   nlohmann/json, `media_inspector.{h,cpp}`, `json_report.{h,cpp}`,
   `main.cpp`, `scripts/generate_fixtures.sh`, `tests/test_assertions.cpp`,
   then iteratively: installed FFmpeg dev headers, fixed two build errors
   (nested-class default-argument ordering issue), generated fixtures, ran
   the tool against both, cross-checked with `ffprobe`, ran Valgrind, ran
   the assertion suite, and wrote README/DECISIONS/EVIDENCE from the actual
   observed output.
5. Timestamp accuracy at T=1.1s — why "half a frame interval" is the natural
   tolerance for nearest-frame seeking, and what  bugs it catches.
6. Boundary-vs-interior awareness — a follow-up fix where the half-frame-tolerance
   rule was found to give false failures near boundaries (e.g. target 2.4999 near
   the last frame), since the "correct" answer there can legitimately be a full frame away.
   Fixed via --expected-pts for exact ground-truth checks on boundary targets.
7. Generate templates for Part B and Part C, then iterate on the exercise by modifying
   those templates.

## Files/decisions materially influenced by AI

All of `src/`, `tests/`, `scripts/generate_fixtures.sh`, `CMakeLists.txt`,
and the documentation files were authored by the AI within this session, in
a single continuous back-and-forth rather than a hidden single-shot
generation.

## How AI-generated claims/code were verified

- The project was actually compiled (`cmake` + `make`) and all build
  errors were fixed by rebuilding until clean, not left unverified.
- Both fixtures were actually generated with `ffmpeg` and independently
  cross-checked with `ffprobe` (used only as a comparison oracle, per the
  assessment rules) for frame count, frame rate, and picture-type
  distribution.
- The frame-request selection rule was validated against a case designed
  to catch the specific bug the spec warns about
  (`frame_index * (1/fps)`): requesting `T=2.4999` on a 30fps stream. The
  tool returned `2.4667`, not `2.5`, confirming it is not taking that
  shortcut (see `EVIDENCE.md` §4).
- Memory safety was checked with Valgrind (`--leak-check=summary`), not
  assumed from "the code looks like it frees everything."
- The full automated assertion suite (`media-core-tests`) was actually run
  against real generated reports, not hand-verified only by inspection.
- Error-handling paths (missing file, audio-only input, truncated file)
  were exercised with real generated bad inputs, not just reasoned about.

