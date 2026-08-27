// test_assertions.cpp
//
// Lightweight, dependency-free assertion runner (no gtest, to keep the
// dependency list short) that checks one media-core JSON report at a time
// against known ground truth for a named fixture type. Invoked once per
// fixture, so adding a third/fourth fixture later never requires changing
// this file's interface -- only adding a new `if (fixture_type == "...")`
// branch with its own checks.
//
// Usage:
//   media-core-tests <fixture_type> <report.json>
//
// Typical run (two invocations, one per required fixture):
//   ./scripts/generate_fixtures.sh
//   ./build/media-core fixtures/cfr_bframes.mp4 \
//       --targets 0.0,0.5,1.1,2.4999 --output /tmp/cfr_report.json
//   ./build/media-core fixtures/vfr_known_pts.mp4 \
//       --targets 0.2,0.75,1.9 --output /tmp/vfr_report.json
//   ./build/media-core-tests cfr /tmp/cfr_report.json
//   ./build/media-core-tests vfr /tmp/vfr_report.json
//
// This intentionally reads the JSON *report* rather than re-implementing
// decode logic, so it is testing exactly the artifact a downstream consumer
// (the JSON report) would see.
//
// WHY TWO DIFFERENT FIXTURE TYPES EXIST AT ALL: a bug that only manifests
// on variable-frame-rate content (e.g. the CFR/VFR classifier always
// returning "CFR" regardless of input) would be invisible if this suite
// only ever ran against the CFR fixture -- that fixture is *supposed* to
// report CFR, so a broken always-CFR classifier would pass by accident.
// The VFR fixture and CFR fixture each exist to catch a different class of
// bug; neither is redundant with the other.
#include <cmath>
#include <fstream>
#include <iostream>
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

// ---------------------------------------------------------------------------
// Per-fixture-type check sets. Each function only assumes the shape of the
// fixture it's named after; add a new function + dispatch branch to cover
// an additional fixture without touching any existing one.
// ---------------------------------------------------------------------------

// Fixture: cfr_bframes.mp4 -- 30fps, 2 B-frames, GOP size 30 (see
// scripts/generate_fixtures.sh). Frame interval is exactly 1/30s.
void checkCfrFixture(const json& report) {
    const double fps = 30.0;
    const double tol = halfFrameTolerance(fps);

    check(report["stream_summary"]["cfr_vfr_verdict"] == "CFR",
          "CFR fixture: verdict is CFR");

    check(!report["frame_requests"].empty(), "CFR fixture: frame_requests present");

    // Assertion: seeking to T=1.1s should select a frame within half a
    // frame interval of 1.1s -- catches a coarse "off by one keyframe" or
    // "wrong rounding direction" seek/selection bug.
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

    // Assertion: presentation-order frame_trace PTS values must be
    // non-decreasing -- catches a reordering/seek bug where decode-order
    // (DTS-ish) timestamps leak into the presentation-order trace instead
    // of being properly reordered via best_effort_timestamp.
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

// Fixture: vfr_known_pts.mp4 -- genuinely variable frame rate (see
// scripts/generate_fixtures.sh and fixtures/vfr_known_pts.ground_truth_pts.txt).
void checkVfrFixture(const json& report) {
    check(report["stream_summary"]["cfr_vfr_verdict"] == "VFR",
          "VFR fixture: verdict is VFR (catches false-CFR misclassification)");

    // Assertion: for VFR content, mean/stddev frame interval fields must
    // actually reflect variability -- a stddev of ~0 alongside a VFR
    // verdict would indicate the classifier and the reported statistics
    // have drifted out of sync with each other.
    double stddev = report["stream_summary"]["stddev_frame_interval_s"].get<double>();
    double mean = report["stream_summary"]["mean_frame_interval_s"].get<double>();
    check(mean > 0.0, "VFR fixture: mean_frame_interval_s is positive");
    check(stddev / std::max(mean, 1e-9) > 0.02,
          "VFR fixture: coefficient of variation exceeds the CFR/VFR "
          "threshold used to classify it (internal consistency check)");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <cfr|vfr> <report.json>\n";
        return 2;
    }

    std::string fixture_type = argv[1];
    json report = loadReport(argv[2]);

    if (fixture_type == "cfr") {
        checkCfrFixture(report);
    } else if (fixture_type == "vfr") {
        checkVfrFixture(report);
    } else {
        std::cerr << "Unknown fixture type '" << fixture_type
                  << "'; expected 'cfr' or 'vfr'\n";
        return 2;
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
