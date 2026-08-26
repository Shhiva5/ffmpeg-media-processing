// media_inspector.h
//
// Thin wrapper around FFmpeg's libavformat/libavcodec that implements the
// "first slice of a reusable media core" described in Part A of the
// assessment: stream inspection, a bounded packet/frame trace, a keyframe
// index, and timestamp-aware frame requests with an explicit selection rule.
//
// Design notes (see DECISIONS.md for the full rationale):
//  - We keep exactly one AVFormatContext + one AVCodecContext alive for the
//    lifetime of a MediaInspector and reuse them across seeks (flushing the
//    decoder between seeks), rather than reopening per request. This models
//    how a real player would use the core: open once, seek many times.
//  - All timestamps in the public API are expressed in *seconds relative to
//    the stream's start_time* ("timeline time"), never raw AVStream ticks.
//    Conversion happens at the boundary (tsToSeconds / secondsToTs).
//  - Only the video stream selected by av_find_best_stream is inspected.
//    Audio/subtitle streams are ignored for analysis but their presence (or
//    the absence of any video stream) is used for the "audio-only input"
//    failure case.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace mediacore {

// ---------------------------------------------------------------------------
// Report data structures
// ---------------------------------------------------------------------------

struct StreamSummary {
    std::string container_format_name;   // e.g. "mov,mp4,m4a,3gp,3g2,mj2"
    std::string codec_name;              // e.g. "h264"
    int width = 0;
    int height = 0;
    std::string pixel_format;            // e.g. "yuv420p"
    std::string stream_time_base;        // e.g. "1/15360"
    double start_time_s = 0.0;
    double duration_s = 0.0;
    std::string avg_frame_rate;          // as reported by the container, e.g. "30/1"
    std::string r_frame_rate;            // nominal/"guessed" frame rate
    bool has_avg_frame_rate = false;
    bool has_r_frame_rate = false;

    // CFR/VFR verdict, derived (see cfr_vfr_basis for method).
    std::string cfr_vfr_verdict;         // "CFR" | "VFR" | "UNKNOWN"
    std::string cfr_vfr_basis;           // human-readable explanation
    double mean_frame_interval_s = 0.0;
    double stddev_frame_interval_s = 0.0;
    int frames_sampled_for_cfr_vfr = 0;
};

struct PacketTraceEntry {
    int index = 0;
    bool has_dts = false;
    double dts_s = 0.0;
    bool has_pts = false;
    double pts_s = 0.0;
    bool keyframe = false;
    int size_bytes = 0;
};

struct FrameTraceEntry {
    int presentation_index = 0;   // order in which frames were emitted by the decoder
    double pts_s = 0.0;
    std::string pict_type;        // "I", "P", "B", "?"
};

struct KeyframeIndexEntry {
    int index = 0;
    double pts_s = 0.0;
    // Time to the *next* keyframe, seconds. Negative/absent for the last one.
    std::optional<double> gap_to_next_s;
};

struct FrameRequestResult {
    double target_s = 0.0;

    // Random-access point actually used for the seek, in the same relative
    // timeline as target_s (seconds since stream start).
    double seek_keyframe_pts_s = 0.0;

    int packets_processed = 0;
    int frames_processed = 0;

    bool found = false;           // false only on hard failure (see error)
    double selected_pts_s = 0.0;
    bool has_next_pts = false;
    double next_pts_s = 0.0;      // only valid if has_next_pts
    bool has_duration = false;
    double duration_s = 0.0;      // next_pts_s - selected_pts_s, if known

    double timing_error_s = 0.0;  // selected_pts_s - target_s (signed)
    double decode_time_ms = 0.0;  // wall clock: seek start -> selection made

    // "" if no fallback was needed. Otherwise one of:
    //   "clamped_to_first_frame" | "clamped_to_last_frame"
    std::string fallback;
    std::string error;            // non-empty only on hard failure
};

struct AnalysisError {
    std::string stage;    // e.g. "open", "stream_discovery", "decode"
    std::string message;
};

// ---------------------------------------------------------------------------
// MediaInspector
// ---------------------------------------------------------------------------

class MediaInspector {
public:
    struct Options {
        // Cap on how many packets/frames are kept in the *reported* trace.
        // Analysis (keyframe index, CFR/VFR verdict) still scans the whole
        // file regardless of this value; it only bounds what gets written
        // to packet_trace/frame_trace in the JSON report.
        //   >= 0  -> keep at most that many entries (0 means "trace empty,
        //            but still note it ran").
        //   -1    -> unlimited: keep every packet/frame from the start of
        //            the file. Note this means decodeBoundedTraceAndCfrVfrVerdict
        //            decodes the entire file, which is fine for short
        //            fixtures but can be slow/memory-heavy on long inputs.
        int trace_limit = 64;
    };

    MediaInspector();
    explicit MediaInspector(Options opts);
    ~MediaInspector();

    MediaInspector(const MediaInspector&) = delete;
    MediaInspector& operator=(const MediaInspector&) = delete;

    // Opens the file and locates the video stream. Returns false and records
    // an AnalysisError on any failure (missing file, no video stream,
    // unsupported codec, etc.) rather than throwing or crashing.
    bool open(const std::string& path);

    // Runs the full-file analysis pass: stream summary, CFR/VFR verdict,
    // keyframe index, and a bounded packet/frame trace from the start of
    // the file. Must be called once after open() and before requestFrame().
    bool analyze();

    // Timestamp-aware frame request. See the selection rule documented in
    // media_inspector.cpp (selectFrameForTarget) for exact semantics.
    FrameRequestResult requestFrame(double target_seconds);

    const StreamSummary& streamSummary() const { return summary_; }
    const std::vector<PacketTraceEntry>& packetTrace() const { return packet_trace_; }
    const std::vector<FrameTraceEntry>& frameTrace() const { return frame_trace_; }
    const std::vector<KeyframeIndexEntry>& keyframeIndex() const { return keyframe_index_; }
    const std::vector<AnalysisError>& errors() const { return errors_; }

    bool hasVideoStream() const { return video_stream_idx_ >= 0; }

    static std::string selectionRuleText();

private:
    Options opts_;

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* dec_ctx_ = nullptr;
    int video_stream_idx_ = -1;
    AVRational stream_time_base_{1, 1};
    int64_t start_time_ticks_ = 0;

    StreamSummary summary_;
    std::vector<PacketTraceEntry> packet_trace_;
    std::vector<FrameTraceEntry> frame_trace_;
    std::vector<KeyframeIndexEntry> keyframe_index_;
    std::vector<AnalysisError> errors_;

    double tsToSeconds(int64_t ts) const;
    int64_t secondsToTs(double seconds) const;

    void recordError(const std::string& stage, const std::string& message);

    bool buildStreamSummary();
    bool buildKeyframeIndexAndPacketTrace();   // demux-only pass
    bool decodeBoundedTraceAndCfrVfrVerdict(); // full decode pass, from file start
    bool rewindToStart();

    double findSeekKeyframeFor(double target_seconds) const;

    // True if another entry may still be appended to a trace vector of the
    // given current size, given opts_.trace_limit (-1 == unlimited).
    bool underTraceLimit(size_t current_size) const;

    void closeCodecAndFormat();
};

}  // namespace mediacore
