#include "json_report.h"

#include <ctime>

namespace mediacore {

using json = nlohmann::json;

namespace {

std::string isoTimestampNow() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

json summaryToJson(const StreamSummary& s) {
    return json{
        {"container_format_name", s.container_format_name},
        {"codec", s.codec_name},
        {"width", s.width},
        {"height", s.height},
        {"pixel_format", s.pixel_format},
        {"stream_time_base", s.stream_time_base},
        {"start_time_s", s.start_time_s},
        {"duration_s", s.duration_s},
        {"avg_frame_rate", s.has_avg_frame_rate ? json(s.avg_frame_rate) : json(nullptr)},
        {"r_frame_rate_nominal", s.has_r_frame_rate ? json(s.r_frame_rate) : json(nullptr)},
        {"cfr_vfr_verdict", s.cfr_vfr_verdict},
        {"cfr_vfr_basis", s.cfr_vfr_basis},
        {"mean_frame_interval_s", s.mean_frame_interval_s},
        {"stddev_frame_interval_s", s.stddev_frame_interval_s},
        {"frames_sampled_for_cfr_vfr", s.frames_sampled_for_cfr_vfr},
    };
}

json packetTraceToJson(const std::vector<PacketTraceEntry>& trace) {
    json arr = json::array();
    for (const auto& p : trace) {
        arr.push_back(json{
            {"index", p.index},
            {"dts_s", p.has_dts ? json(p.dts_s) : json(nullptr)},
            {"pts_s", p.has_pts ? json(p.pts_s) : json(nullptr)},
            {"keyframe", p.keyframe},
            {"size_bytes", p.size_bytes},
        });
    }
    return arr;
}

json frameTraceToJson(const std::vector<FrameTraceEntry>& trace) {
    json arr = json::array();
    for (const auto& f : trace) {
        arr.push_back(json{
            {"presentation_index", f.presentation_index},
            {"pts_s", f.pts_s},
            {"pict_type", f.pict_type},
        });
    }
    return arr;
}

json keyframeIndexToJson(const std::vector<KeyframeIndexEntry>& idx) {
    json arr = json::array();
    for (const auto& k : idx) {
        arr.push_back(json{
            {"index", k.index},
            {"pts_s", k.pts_s},
            {"gap_to_next_s", k.gap_to_next_s.has_value() ? json(*k.gap_to_next_s)
                                                            : json(nullptr)},
        });
    }
    return arr;
}

json frameRequestToJson(const FrameRequestResult& r) {
    json j{
        {"target_s", r.target_s},
        {"found", r.found},
        {"seek_keyframe_pts_s", r.seek_keyframe_pts_s},
        {"packets_processed", r.packets_processed},
        {"frames_processed", r.frames_processed},
        {"decode_time_ms", r.decode_time_ms},
        {"fallback", r.fallback.empty() ? json(nullptr) : json(r.fallback)},
    };
    if (r.found) {
        j["selected_pts_s"] = r.selected_pts_s;
        j["timing_error_s"] = r.timing_error_s;
        j["next_pts_s"] = r.has_next_pts ? json(r.next_pts_s) : json(nullptr);
        j["duration_s"] = r.has_duration ? json(r.duration_s) : json(nullptr);
        j["error"] = nullptr;
    } else {
        j["selected_pts_s"] = nullptr;
        j["timing_error_s"] = nullptr;
        j["next_pts_s"] = nullptr;
        j["duration_s"] = nullptr;
        j["error"] = r.error;
    }
    return j;
}

}  // namespace

json buildReport(const std::string& input_path, const std::vector<double>& targets,
                  const MediaInspector& inspector,
                  const std::vector<FrameRequestResult>& frame_requests) {
    json report;
    report["tool"] = "media-core";
    report["version"] = "0.1.0";
    report["input"] = input_path;
    report["generated_at"] = isoTimestampNow();
    report["requested_targets_s"] = targets;
    report["selection_rule"] = MediaInspector::selectionRuleText();

    report["stream_summary"] = summaryToJson(inspector.streamSummary());
    report["packet_trace"] = packetTraceToJson(inspector.packetTrace());
    report["frame_trace"] = frameTraceToJson(inspector.frameTrace());
    report["keyframe_index"] = keyframeIndexToJson(inspector.keyframeIndex());

    json requests_json = json::array();
    for (const auto& r : frame_requests) requests_json.push_back(frameRequestToJson(r));
    report["frame_requests"] = requests_json;

    json errors_json = json::array();
    for (const auto& e : inspector.errors()) {
        errors_json.push_back(json{{"stage", e.stage}, {"message", e.message}});
    }
    report["errors"] = errors_json;

    return report;
}

}  // namespace mediacore
