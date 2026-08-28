# Research Log (Part A)

Format: assumption stated *before* checking primary docs, then whether it
was confirmed or changed, with the evidence. Tested facts vs. documentation
claims vs. inferences are labeled explicitly.

## 1. `avcodec_send_packet`/`avcodec_receive_frame` is the current decode API

- **Pre-research assumption:** modern FFmpeg uses the send/receive
  packet/frame API rather than the older single-call `avcodec_decode_video2`
  (deprecated).
- **Status: confirmed.** [DOC] FFmpeg's own Doxygen for `avcodec_decode_video2`
  marks it deprecated in favor of `avcodec_send_packet`/`avcodec_receive_frame`.
  [TESTED] Compiling against the installed FFmpeg 4.4.2 headers using only
  the send/receive API succeeded with `-Wall -Wextra` and no deprecation
  warnings, which would have fired had a deprecated call been used.
  Source: https://ffmpeg.org/doxygen/4.4/group__lavc__decoding.html
  (accessed during this session, 2026-08-25).

## 2. `frame->best_effort_timestamp` is the right PTS to use for selection

- **Pre-research assumption:** `frame->pts` alone can be `AV_NOPTS_VALUE`
  for some streams/containers, and FFmpeg provides
  `best_effort_timestamp` specifically to give a "best guess" presentation
  timestamp that falls back to DTS-derived heuristics when PTS is missing.
- **Status: confirmed.** [DOC] `AVFrame.best_effort_timestamp` is documented
  in `libavutil/frame.h` as "frame timestamp estimated using various
  heuristics, in stream time base... Code outside libavcodec should access
  this field using: av_frame_get_best_effort_timestamp(frame)" (the
  accessor function is now just the struct field directly in recent
  versions). [INFERENCE] Using it instead of raw `pts` is safer for
  robustness across containers, at the cost of being one layer removed from
  "the literal PTS field." This is used in `media_inspector.cpp` with a
  fallback to `frame->pts` if `best_effort_timestamp` is also unset, for
  belt-and-suspenders.
  Source: FFmpeg `libavutil/frame.h` header comments, local installed
  version 56.70.100 (accessed during this session).

## 3. `AVSEEK_FLAG_BACKWARD` seeks to the nearest keyframe at or before the target

- **Pre-research assumption:** `av_seek_frame` with `AVSEEK_FLAG_BACKWARD`
  guarantees landing on or before the requested timestamp, which is what
  frame-accurate seek-then-decode-forward requires (seeking forward past
  the target would make correct frame selection impossible without
  rewinding again).
- **Status: confirmed, with a caveat.** [DOC] `libavformat/avformat.h`:
  "AVSEEK_FLAG_BACKWARD: seek backward." combined with the general seek
  semantics that, absent the BYTE or FRAME flags, `av_seek_frame` seeks by
  timestamp and (per common FFmpeg usage documented in numerous FFmpeg
  wiki/API discussions) lands at or before the target when BACKWARD is
  set. [TESTED] Empirically confirmed via the frame-request evidence in
  `EVIDENCE.md` §4: requesting `T=2.4999` after seeking to keyframe
  `pts_s=2.0` correctly returned a frame with PTS `2.4667` (before target),
  never a frame after it before the "next" frame was found — consistent
  with backward-biased seeking.
  Source: FFmpeg `libavformat/avformat.h` header comments, local installed
  version 58.76.100 (accessed during this session); cross-referenced
  against general community documentation at
  https://ffmpeg.org/doxygen/4.4/group__lavf__decoding.html (accessed
  2026-08-25).

## 4. `AV_PKT_FLAG_KEY` reliably identifies keyframes for MP4/H.264

- **Pre-research assumption:** the demuxer-level `AV_PKT_FLAG_KEY` flag on
  an `AVPacket` is a reliable proxy for "this is a random-access point" for
  common containers (MP4) and codecs (H.264), even though it isn't a
  guarantee for every container/codec combination.
- **Status: confirmed for the tested case, documented as a limitation for
  the general case.** [TESTED] Cross-checked the keyframe index our tool
  produced against `ffprobe -show_frames -show_entries frame=key_frame`
  for `cfr_bframes.mp4`: both report keyframes exactly at t=0,1,2,3,4s
  (5 keyframes, matching the 30-frame GOP at 30fps). [INFERENCE] This does
  not by itself prove the flag is reliable for *all* containers/codecs;
  documented as a known limitation in `DECISIONS.md` rather than presented
  as a general guarantee.
  Source: direct comparison against `ffprobe` output (comparison oracle,
  per assessment rules), this session, 2026-08-25.

## 5. FFmpeg build used here is GPL-encumbered (licensing flag, not legal advice)

- **Pre-research assumption:** the system `ffmpeg`/`libav*` packages on
  Ubuntu 22.04 are typically built with `--enable-gpl` and `libx264`
  enabled, which has redistribution implications distinct from a
  LGPL-only FFmpeg build.
- **Status: confirmed.** [TESTED] `ffmpeg -version` output (captured during
  this session) shows `--enable-gpl ... --enable-libx264` in the reported
  configuration string of the installed build.
  [INFERENCE, explicitly flagged as non-legal] this likely means
  redistributing a binary linked against this exact system build would
  carry GPL obligations; a production decision would need actual legal
  review of the specific FFmpeg build used, which this document does not
  attempt to provide. Flagged in `README.md`'s "Licensing note."
  Source: `ffmpeg -version` output, local build, this session, 2026-08-25.
