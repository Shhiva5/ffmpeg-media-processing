#pragma once

#include <string>
#include <vector>

#include "json.hpp"
#include "media_inspector.h"

namespace mediacore {

// Assembles the full report as an nlohmann::json object. Kept separate from
// MediaInspector so the core analysis code has zero dependency on the JSON
// library (easier to reuse the core in a context that wants a different
// serialization, e.g. an in-process API).
nlohmann::json buildReport(const std::string& input_path,
                            const std::vector<double>& targets,
                            const MediaInspector& inspector,
                            const std::vector<FrameRequestResult>& frame_requests);

}  // namespace mediacore
