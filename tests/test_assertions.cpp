// test_assertions.cpp
//
// Lightweight, dependency-free assertion runner (no gtest, to keep the
// dependency list short) that checks media-core's JSON reports against
// known ground truth for the two required conformance fixtures. Run after
// generating reports for both fixtures:
//
//   ./scripts/generate_fixtures.sh
//   ./build/media-core fixtures/cfr_bframes.mp4 \
//       --targets 0.0,0.5,1.1,2.4999 --output /tmp/cfr_report.json
//   ./build/media-core fixtures/vfr_known_pts.mp4 \
//       --targets 0.2,0.75,1.9 --output /tmp/vfr_report.json
//   ./build/media-core-tests /tmp/cfr_report.json /tmp/vfr_report.json
//
// This intentionally reads the JSON *reports* rather than re-implementing
// decode logic, so it is testing exactly the artifact a downstream consumer
// (the JSON report) would see.
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "json.hpp"

using json = nlohmann::json;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& description) {
    g_checks++;
    if (!condition) {
        g_failures++;
        std::cerr << "FAIL: " << description << "\n";
    } else {
        std::cout << "PASS: " << description << "\n";
    }
}

json loadReport(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Could not open report file: " << path << "\n";
        std::exit(2);
    }
    json j;
    f >> j;
    return j;
}

// Half a frame interval at the fixture's known frame rate is our tolerance:
// a correct implementation should land within this of the true PTS. This is
// the "plausible timestamp bug" tolerance referenced in the assessment.
double halfFrameTolerance(double fps) { return 0.5 / fps; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <cfr_report.json> <vfr_report.json>\n";
        return 2;
    }

    // -------------------------------------------------------------------
    // CFR fixture assertions
    // -------------------------------------------------------------------
    // Fixture generated at 30fps with 2 B-frames and GOP size 30 (see
    // scripts/generate_fixtures.sh). Frame interval is exactly 1/30s.
    {
        json report = loadReport(argv[1]);
        const double fps = 30.0;
        const double tol = halfFrameTolerance(fps);

        check(report["stream_summary"]["cfr_vfr_verdict"] == "CFR",
              "CFR fixture: verdict is CFR");

        check(!report["frame_requests"].empty(), "CFR fixture: frame_requests present");

        // Assertion 1: seeking to T=1.1s should select a frame within half a
        // frame interval of 1.1s -- catches a coarse "off by one keyframe"
        // or "wrong rounding direction" seek/selection bug.
        bool found_1_1 = false;
        for (const auto& r : report["frame_requests"]) {
            if (std::abs(r["target_s"].get<double>() - 1.1) < 1e-9) {
                found_1_1 = true;
                check(r["found"].get<bool>(), "CFR fixture: T=1.1s frame found");
                if (r["found"].get<bool>()) {
                    double err = std::abs(r["timing_error_s"].get<double>());
                    check(err <= tol, "CFR fixture: T=1.1s timing_error_s (" +
                                           std::to_string(err) +
                                           "s) within half-frame tolerance (" +
                                           std::to_string(tol) + "s)");
                }
            }
        }
        check(found_1_1, "CFR fixture: T=1.1s target was actually requested "
                          "(update fixture target list if this fails)");

        // Assertion 2: presentation-order frame_trace PTS values must be
        // non-decreasing -- catches a reordering/seek bug where decode-order
        // (DTS-ish) timestamps leak into the presentation-order trace
        // instead of being properly reordered via best_effort_timestamp.
        bool monotonic = true;
        double prev = -1e18;
        for (const auto& f : report["frame_trace"]) {
            double pts = f["pts_s"].get<double>();
            if (pts < prev - 1e-9) {
                monotonic = false;
                break;
            }
            prev = pts;
        }
        check(monotonic, "CFR fixture: frame_trace PTS values are monotonically "
                          "non-decreasing (presentation order)");
    }

    // -------------------------------------------------------------------
    // VFR fixture assertions
    // -------------------------------------------------------------------
    {
        json report = loadReport(argv[2]);

        check(report["stream_summary"]["cfr_vfr_verdict"] == "VFR",
              "VFR fixture: verdict is VFR (catches false-CFR misclassification)");

        // Assertion 3: for VFR content, mean/stddev frame interval fields
        // must actually reflect variability -- a stddev of ~0 alongside a
        // VFR verdict would indicate the classifier and the reported
        // statistics have drifted out of sync.
        double stddev = report["stream_summary"]["stddev_frame_interval_s"].get<double>();
        double mean = report["stream_summary"]["mean_frame_interval_s"].get<double>();
        check(mean > 0.0, "VFR fixture: mean_frame_interval_s is positive");
        check(stddev / std::max(mean, 1e-9) > 0.02,
              "VFR fixture: coefficient of variation exceeds the CFR/VFR "
              "threshold used to classify it (internal consistency check)");
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
