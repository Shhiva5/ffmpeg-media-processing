# Decisions (Part B)

## Assumptions

1. "One 30-60 second public, generated or self-owned source" is satisfied
   by a synthetic Mandelbrot-zoom render — it's self-generated, has genuine
   continuous motion/detail (unlike a flat test pattern), and needs no
   external/copyrighted media.
2. "720p" proxies are satisfied by generating the master natively at
   1280x720 rather than at a higher resolution and downscaling — this
   simplifies the pipeline (one fewer `-vf scale` step) without changing
   what's actually being measured (GOP-structure cost, not scaling cost).
   A real pipeline would more commonly start from a higher-res master; this
   is a scope simplification, not a claim that scaling cost is irrelevant
   in general.
3. "Seeded list of 10 target times" means a fixed, reasoned, hand-picked
   list (documented in `EVIDENCE_PART_B.md` §2), not a randomly-seeded PRNG
   sequence — deterministic and reproducible either way, but hand-picking
   let the list be deliberately spread across GOP-boundary-relative
   positions (near/at/far-from keyframes), which a uniform random sample
   over 30s might not reliably cover.

## Chosen approach

- **Reused Part A's `media-core` binary as the measurement tool**, per the
  explicit instruction to report "per-target decode work... using your own
  tool. This is the direct payoff of Part A's `frames_processed` /
  `decode_time_ms` / `seek_keyframe_pts_s` fields already being in the JSON
  report schema — no new instrumentation was needed for Part B's benchmark.
- **Generating 30s video source**: 30-second duration with a shallow zoom range,
   kept short for simplicity.
- **`/usr/bin/time -v` and `gprof`**, rather than skip the "Focused performance evidence"
  requirement, two independent real tools were used and their results were
  shown to corroborate each other (§5 of `EVIDENCE_PART_B.md`).

## Discarded options

- **Using one of Part A's existing CFR/VFR fixtures as the Part B source**
  instead of generating a new one. Rejected because those fixtures are
  short (5s) and were specifically designed to test timing edge cases, not
  to produce visually/statistically realistic encode behavior at a size
  large enough for a meaningful all-intra-vs-short-GOP file-size
  comparison; a fresh, longer, higher-detail source was more honest
  evidence for Part B's actual question.
- **A true cold-cache benchmark via `drop_caches`.** Attempted, blocked by
  container permissions (`Permission denied`, see `EVIDENCE_PART_B.md` §4).
  Rather than silently omitting the caching discussion the spec explicitly
  requires, the limitation is stated directly and a weaker but real
  substitute (repeat-run consistency + zero major page faults from
  `/usr/bin/time -v`) is used instead, with its evidentiary weakness
  labeled as such.

## Known limitations

1. All decode measurements are software-only (`libavcodec`'s built-in
   decoder); no hardware-accelerated decode path was benchmarked. Flagged
   as the single largest confounder in `EVIDENCE_PART_B.md` §4 and as risk
   #1 in §6's recommendation.
2. Single visible vCPU in this container; multi-core scaling of the
   short-GOP decode cost is untested (risk #3, §6).
3. The synthetic Mandelbrot source's fractal/noise-like detail likely
   understates the real-world file-size gap between all-intra and
   short-GOP for natural video footage (risk #2, §6).
4. No true cold-cache measurement is captured. 

## Next two engineering steps

1. Re-run the exact §3 benchmark using the browser's WebCodecs
   `VideoDecoder` (hardware-accelerated where available) against both
   proxies, to test whether the software-decode gap measured here survives
   on real playback hardware — this is risk #1 from the recommendation and
   the single most important number missing from this evidence set.
2. Prototype the `requestFrame`/`cancelRequest` pair from
   `HANDOFF_CONTRACT.md` §3 as a thin wrapper around the existing
   `MediaInspector::requestFrame()`, with a cancellation flag checked
   inside the decode-forward loop, to validate the "bounded, not
   instantaneous" cancellation-latency claim empirically rather than just
   asserting it.
