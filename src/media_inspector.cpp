#include "media_inspector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

namespace mediacore {

namespace {

std::string pictTypeToString(int pict_type) {
    switch (pict_type) {
        case AV_PICTURE_TYPE_I: return "I";
        case AV_PICTURE_TYPE_P: return "P";
        case AV_PICTURE_TYPE_B: return "B";
        default: return "?";
    }
}

std::string rationalToString(AVRational r) {
    return std::to_string(r.num) + "/" + std::to_string(r.den);
}

}  // namespace

MediaInspector::MediaInspector() : opts_(Options()) {}

MediaInspector::MediaInspector(Options opts) : opts_(opts) {}

MediaInspector::~MediaInspector() { closeCodecAndFormat(); }

void MediaInspector::closeCodecAndFormat() {
    if (dec_ctx_) {
        avcodec_free_context(&dec_ctx_);
        dec_ctx_ = nullptr;
    }
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
}

void MediaInspector::recordError(const std::string& stage, const std::string& message) {
    errors_.push_back(AnalysisError{stage, message});
}

double MediaInspector::tsToSeconds(int64_t ts) const {
    return (static_cast<double>(ts) - static_cast<double>(start_time_ticks_)) *
           av_q2d(stream_time_base_);
}

int64_t MediaInspector::secondsToTs(double seconds) const {
    return start_time_ticks_ +
           static_cast<int64_t>(std::llround(seconds / av_q2d(stream_time_base_)));
}

// ---------------------------------------------------------------------------
// open()
// ---------------------------------------------------------------------------

bool MediaInspector::open(const std::string& path) {
    AVFormatContext* fmt_ctx = nullptr;
    int ret = avformat_open_input(&fmt_ctx, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        recordError("open", std::string("avformat_open_input failed: ") + errbuf +
                                 " (input may be missing, truncated, or an unsupported "
                                 "container)");
        return false;
    }
    fmt_ctx_ = fmt_ctx;

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        recordError("stream_discovery",
                     std::string("avformat_find_stream_info failed: ") + errbuf);
        closeCodecAndFormat();
        return false;
    }

    video_stream_idx_ =
        av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx_ < 0) {
        // This is the required "audio-only / no video stream" failure path.
        bool has_audio = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1,
                                              nullptr, 0) >= 0;
        recordError("stream_discovery",
                     has_audio
                         ? "No video stream found; input appears to be audio-only."
                         : "No video stream found and no audio stream found either; "
                           "input may be malformed or an unsupported media type.");
        closeCodecAndFormat();
        return false;
    }

    AVStream* stream = fmt_ctx_->streams[video_stream_idx_];
    stream_time_base_ = stream->time_base;
    start_time_ticks_ = (stream->start_time != AV_NOPTS_VALUE) ? stream->start_time : 0;

    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        recordError("decoder_lookup",
                     "No decoder available for codec id " +
                         std::to_string(stream->codecpar->codec_id));
        closeCodecAndFormat();
        return false;
    }

    dec_ctx_ = avcodec_alloc_context3(decoder);
    if (!dec_ctx_) {
        recordError("decoder_alloc", "avcodec_alloc_context3 returned null");
        closeCodecAndFormat();
        return false;
    }

    ret = avcodec_parameters_to_context(dec_ctx_, stream->codecpar);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        recordError("decoder_setup",
                     std::string("avcodec_parameters_to_context failed: ") + errbuf);
        closeCodecAndFormat();
        return false;
    }

    ret = avcodec_open2(dec_ctx_, decoder, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        recordError("decoder_open", std::string("avcodec_open2 failed: ") + errbuf);
        closeCodecAndFormat();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// analyze()
// ---------------------------------------------------------------------------

bool MediaInspector::analyze() {
    if (!hasVideoStream()) {
        recordError("analyze", "analyze() called without a valid video stream");
        return false;
    }
    if (!buildStreamSummary()) return false;
    if (!buildKeyframeIndexAndPacketTrace()) return false;
    if (!rewindToStart()) return false;
    if (!decodeBoundedTraceAndCfrVfrVerdict()) return false;
    if (!rewindToStart()) return false;  // leave the stream ready for requestFrame()
    return true;
}

bool MediaInspector::buildStreamSummary() {
    AVStream* stream = fmt_ctx_->streams[video_stream_idx_];
    const AVCodecParameters* par = stream->codecpar;

    summary_.container_format_name =
        fmt_ctx_->iformat && fmt_ctx_->iformat->name ? fmt_ctx_->iformat->name : "unknown";

    const AVCodec* decoder = avcodec_find_decoder(par->codec_id);
    summary_.codec_name = decoder ? decoder->name : avcodec_get_name(par->codec_id);

    summary_.width = par->width;
    summary_.height = par->height;

    const char* pix_fmt_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format));
    summary_.pixel_format = pix_fmt_name ? pix_fmt_name : "unknown";

    summary_.stream_time_base = rationalToString(stream->time_base);

    summary_.start_time_s =
        (stream->start_time != AV_NOPTS_VALUE) ? stream->start_time * av_q2d(stream->time_base)
                                                : 0.0;

    if (stream->duration != AV_NOPTS_VALUE) {
        summary_.duration_s = stream->duration * av_q2d(stream->time_base);
    } else if (fmt_ctx_->duration != AV_NOPTS_VALUE) {
        summary_.duration_s = fmt_ctx_->duration / static_cast<double>(AV_TIME_BASE);
    } else {
        summary_.duration_s = 0.0;
        recordError("stream_summary",
                     "Duration unavailable from container/stream metadata; reported as 0.0. "
                     "A full-file scan would be needed to derive it empirically.");
    }

    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        summary_.has_avg_frame_rate = true;
        summary_.avg_frame_rate = rationalToString(stream->avg_frame_rate);
    }
    if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
        summary_.has_r_frame_rate = true;
        summary_.r_frame_rate = rationalToString(stream->r_frame_rate);
    }

    // CFR/VFR verdict and its numeric basis are filled in by
    // decodeBoundedTraceAndCfrVfrVerdict(), which measures actual decoded
    // frame PTS deltas rather than trusting container metadata alone.
    return true;
}

bool MediaInspector::buildKeyframeIndexAndPacketTrace() {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        recordError("keyframe_index", "av_packet_alloc failed");
        return false;
    }

    int packet_index = 0;
    while (true) {
        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0) break;  // EOF or error; either way we stop the scan

        if (pkt->stream_index != video_stream_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        if (static_cast<int>(packet_trace_.size()) < opts_.trace_limit) {
            PacketTraceEntry entry;
            entry.index = packet_index;
            entry.has_dts = pkt->dts != AV_NOPTS_VALUE;
            entry.dts_s = entry.has_dts ? tsToSeconds(pkt->dts) : 0.0;
            entry.has_pts = pkt->pts != AV_NOPTS_VALUE;
            entry.pts_s = entry.has_pts ? tsToSeconds(pkt->pts) : 0.0;
            entry.keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            entry.size_bytes = pkt->size;
            packet_trace_.push_back(entry);
        }

        if ((pkt->flags & AV_PKT_FLAG_KEY) != 0 && pkt->pts != AV_NOPTS_VALUE) {
            KeyframeIndexEntry kf;
            kf.index = static_cast<int>(keyframe_index_.size());
            kf.pts_s = tsToSeconds(pkt->pts);
            keyframe_index_.push_back(kf);
        }

        packet_index++;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    // Fill in gap_to_next_s now that we know every keyframe's timestamp.
    // Limitation (documented for the report): we identify keyframes purely
    // from AV_PKT_FLAG_KEY on the *demuxed* packet, which is set by the
    // demuxer from container-level flags. For containers/codecs where the
    // demuxer does not populate this reliably, this index would under- or
    // over-report random-access points; we do not independently parse
    // codec-level IDR markers (e.g. H.264 NAL unit types) in this slice.
    for (size_t i = 0; i < keyframe_index_.size(); ++i) {
        if (i + 1 < keyframe_index_.size()) {
            keyframe_index_[i].gap_to_next_s =
                keyframe_index_[i + 1].pts_s - keyframe_index_[i].pts_s;
        } else {
            keyframe_index_[i].gap_to_next_s = std::nullopt;
        }
    }

    if (keyframe_index_.empty()) {
        recordError("keyframe_index",
                     "No keyframes found in the video stream; frame requests will not be "
                     "able to seek and will fail.");
    }

    return true;
}

bool MediaInspector::rewindToStart() {
    int ret = av_seek_frame(fmt_ctx_, video_stream_idx_, 0, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        // Some formats (e.g. piped/non-seekable input) can't seek to 0 this
        // way; fall back to a byte-based rewind attempt before giving up.
        ret = av_seek_frame(fmt_ctx_, video_stream_idx_, 0,
                             AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE);
    }
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        recordError("seek", std::string("Failed to rewind to start of stream: ") + errbuf);
        return false;
    }
    avcodec_flush_buffers(dec_ctx_);
    return true;
}

bool MediaInspector::decodeBoundedTraceAndCfrVfrVerdict() {
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        recordError("decode", "Failed to allocate AVPacket/AVFrame");
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        return false;
    }

    std::vector<double> pts_seconds;
    // Sample enough frames for a meaningful CFR/VFR verdict even when the
    // reported trace_limit is small; independent of what gets *written* to
    // the report.
    const int cfr_vfr_sample_target = std::max(opts_.trace_limit, 120);

    bool decoding = true;
    while (decoding) {
        int read_ret = av_read_frame(fmt_ctx_, pkt);
        bool eof = read_ret < 0;

        if (!eof && pkt->stream_index != video_stream_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        int send_ret = avcodec_send_packet(dec_ctx_, eof ? nullptr : pkt);
        if (!eof) av_packet_unref(pkt);

        if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(send_ret, errbuf, sizeof(errbuf));
            recordError("decode", std::string("avcodec_send_packet failed: ") + errbuf +
                                       " (input may contain a corrupt frame; continuing)");
        }

        while (true) {
            int recv_ret = avcodec_receive_frame(dec_ctx_, frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(recv_ret, errbuf, sizeof(errbuf));
                recordError("decode", std::string("avcodec_receive_frame failed: ") + errbuf);
                break;
            }

            int64_t best_ts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                   ? frame->best_effort_timestamp
                                   : frame->pts;
            if (best_ts != AV_NOPTS_VALUE) {
                double pts_s = tsToSeconds(best_ts);
                pts_seconds.push_back(pts_s);

                if (static_cast<int>(frame_trace_.size()) < opts_.trace_limit) {
                    FrameTraceEntry fe;
                    fe.presentation_index = static_cast<int>(frame_trace_.size());
                    fe.pts_s = pts_s;
                    fe.pict_type = pictTypeToString(frame->pict_type);
                    frame_trace_.push_back(fe);
                }
            }
            av_frame_unref(frame);

            if (static_cast<int>(pts_seconds.size()) >= cfr_vfr_sample_target) {
                decoding = false;
            }
        }

        if (eof) decoding = false;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    // Derive the CFR/VFR verdict from measured presentation-order PTS
    // deltas, not from container metadata alone (avg_frame_rate can claim
    // a constant rate for content that is not actually constant).
    std::sort(pts_seconds.begin(), pts_seconds.end());
    std::vector<double> deltas;
    deltas.reserve(pts_seconds.size());
    for (size_t i = 1; i < pts_seconds.size(); ++i) {
        deltas.push_back(pts_seconds[i] - pts_seconds[i - 1]);
    }

    summary_.frames_sampled_for_cfr_vfr = static_cast<int>(pts_seconds.size());

    if (deltas.size() < 2) {
        summary_.cfr_vfr_verdict = "UNKNOWN";
        summary_.cfr_vfr_basis =
            "Fewer than 2 decodable frame intervals were available to measure.";
        return true;
    }

    double mean = 0.0;
    for (double d : deltas) mean += d;
    mean /= static_cast<double>(deltas.size());

    double variance = 0.0;
    for (double d : deltas) variance += (d - mean) * (d - mean);
    variance /= static_cast<double>(deltas.size());
    double stddev = std::sqrt(variance);

    summary_.mean_frame_interval_s = mean;
    summary_.stddev_frame_interval_s = stddev;

    // Threshold: if the coefficient of variation of frame intervals is
    // under 2%, treat the stream as CFR. This is a pragmatic threshold
    // (documented, not a formal spec) chosen to tolerate normal rounding
    // noise in rational timestamps while still catching genuine VFR
    // content, where interval jumps are typically >10-20%.
    double coefficient_of_variation = (mean > 0.0) ? (stddev / mean) : 0.0;
    bool looks_cfr = coefficient_of_variation < 0.02;

    char basis_buf[256];
    std::snprintf(basis_buf, sizeof(basis_buf),
                  "Measured %d decoded frame intervals: mean=%.6fs, stddev=%.6fs, "
                  "coefficient_of_variation=%.4f (threshold 0.02). Container "
                  "avg_frame_rate=%s, r_frame_rate=%s were NOT used as the sole basis.",
                  static_cast<int>(deltas.size()), mean, stddev, coefficient_of_variation,
                  summary_.has_avg_frame_rate ? summary_.avg_frame_rate.c_str() : "n/a",
                  summary_.has_r_frame_rate ? summary_.r_frame_rate.c_str() : "n/a");
    summary_.cfr_vfr_basis = basis_buf;
    summary_.cfr_vfr_verdict = looks_cfr ? "CFR" : "VFR";

    return true;
}

// ---------------------------------------------------------------------------
// requestFrame() and the selection rule
// ---------------------------------------------------------------------------

std::string MediaInspector::selectionRuleText() {
    return
        "For an arbitrary timeline time T (seconds, relative to stream start): "
        "seek to the latest keyframe with presentation timestamp <= T (or the "
        "first keyframe in the file if T is before it), flush the decoder, then "
        "decode forward in presentation order. The SELECTED FRAME is the last "
        "decoded frame whose PTS <= T. If a subsequently decoded frame has "
        "PTS > T, its timestamp is reported as next_pts_s, and "
        "duration_s = next_pts_s - selected_pts_s approximates the display "
        "duration of the selected frame. Boundary handling: (1) if T is before "
        "the PTS of the very first frame in the file, the first frame is "
        "selected and fallback=clamped_to_first_frame; (2) if T is at or after "
        "the last decodable frame's PTS, that last frame is selected and "
        "fallback=clamped_to_last_frame, with next_pts_s unavailable. We do "
        "NOT assume frame_index = floor(T * nominal_fps); the selection is "
        "always based on decoded PTS values, which is what makes it correct "
        "for VFR content and for streams with a non-zero start_time.";
}

double MediaInspector::findSeekKeyframeFor(double target_seconds) const {
    if (keyframe_index_.empty()) return 0.0;
    double chosen = keyframe_index_.front().pts_s;
    for (const auto& kf : keyframe_index_) {
        if (kf.pts_s <= target_seconds) {
            chosen = kf.pts_s;
        } else {
            break;  // keyframe_index_ is in ascending PTS order
        }
    }
    return chosen;
}

FrameRequestResult MediaInspector::requestFrame(double target_seconds) {
    FrameRequestResult result;
    result.target_s = target_seconds;

    if (!hasVideoStream()) {
        result.error = "requestFrame() called without a valid video stream";
        return result;
    }
    if (keyframe_index_.empty()) {
        result.error = "No keyframes available; cannot seek";
        return result;
    }

    auto wall_start = std::chrono::steady_clock::now();

    double seek_target_relative_s = findSeekKeyframeFor(target_seconds);
    result.seek_keyframe_pts_s = seek_target_relative_s;

    int64_t seek_ts = secondsToTs(seek_target_relative_s);
    int seek_ret =
        av_seek_frame(fmt_ctx_, video_stream_idx_, seek_ts, AVSEEK_FLAG_BACKWARD);
    if (seek_ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(seek_ret, errbuf, sizeof(errbuf));
        result.error = std::string("av_seek_frame failed: ") + errbuf;
        return result;
    }
    avcodec_flush_buffers(dec_ctx_);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        result.error = "Failed to allocate AVPacket/AVFrame";
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        return result;
    }

    bool have_best = false;
    double best_pts_s = 0.0;
    bool have_next = false;
    double next_pts_s = 0.0;

    bool decoding = true;
    while (decoding) {
        int read_ret = av_read_frame(fmt_ctx_, pkt);
        bool eof = read_ret < 0;

        if (!eof && pkt->stream_index != video_stream_idx_) {
            av_packet_unref(pkt);
            continue;
        }

        if (!eof) result.packets_processed++;

        int send_ret = avcodec_send_packet(dec_ctx_, eof ? nullptr : pkt);
        if (!eof) av_packet_unref(pkt);
        if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            // Non-fatal for the request as a whole: skip this packet and
            // keep going, but the caller's report should surface that a
            // decode error occurred if it ends up with no frame at all.
        }

        while (true) {
            int recv_ret = avcodec_receive_frame(dec_ctx_, frame);
            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) break;
            if (recv_ret < 0) break;

            result.frames_processed++;
            int64_t best_ts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                   ? frame->best_effort_timestamp
                                   : frame->pts;
            if (best_ts != AV_NOPTS_VALUE) {
                double pts_s = tsToSeconds(best_ts);
                if (pts_s <= target_seconds) {
                    have_best = true;
                    best_pts_s = pts_s;  // keep the *last* one <= target
                } else if (!have_next) {
                    have_next = true;
                    next_pts_s = pts_s;
                    decoding = false;  // selection rule satisfied; stop
                }
            }
            av_frame_unref(frame);
            if (!decoding) break;
        }

        if (eof) decoding = false;
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);

    auto wall_end = std::chrono::steady_clock::now();
    result.decode_time_ms =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    if (have_best) {
        result.found = true;
        result.selected_pts_s = best_pts_s;
        if (have_next) {
            result.has_next_pts = true;
            result.next_pts_s = next_pts_s;
            result.has_duration = true;
            result.duration_s = next_pts_s - best_pts_s;
        } else {
            result.fallback = "clamped_to_last_frame";
        }
    } else if (have_next) {
        // Target was before the first available frame's PTS.
        result.found = true;
        result.selected_pts_s = next_pts_s;
        result.fallback = "clamped_to_first_frame";
    } else {
        result.found = false;
        result.error = "No frames could be decoded from the chosen random-access point";
    }

    result.timing_error_s = result.selected_pts_s - target_seconds;
    return result;
}

}  // namespace mediacore
