// media-core: a small command-line tool built directly on FFmpeg's
// libavformat/libavcodec APIs.
//
// Usage:
//   media-core <input> --targets 0.0,0.5,1.1,4.75 --output report.json
//                       [--trace-limit N]
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json_report.h"
#include "media_inspector.h"

namespace {

struct CliArgs {
    std::string input;
    std::string output = "report.json";
    std::vector<double> targets;
    int trace_limit = 64;
    bool ok = false;
    std::string error;
};

std::vector<double> parseTargets(const std::string& csv, std::string* err) {
    std::vector<double> result;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        try {
            size_t pos = 0;
            double v = std::stod(item, &pos);
            if (pos != item.size()) throw std::invalid_argument("trailing characters");
            result.push_back(v);
        } catch (const std::exception&) {
            *err = "Could not parse target time '" + item + "' as a number";
            return {};
        }
    }
    return result;
}

CliArgs parseArgs(int argc, char** argv) {
    CliArgs args;
    if (argc < 2) {
        args.error = "Missing required <input> argument";
        return args;
    }
    args.input = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                args.error = std::string("Missing value for ") + flag;
                return "";
            }
            return argv[++i];
        };

        if (arg == "--targets") {
            std::string val = needValue("--targets");
            if (!args.error.empty()) return args;
            std::string parse_err;
            args.targets = parseTargets(val, &parse_err);
            if (!parse_err.empty()) {
                args.error = parse_err;
                return args;
            }
        } else if (arg == "--output") {
            args.output = needValue("--output");
            if (!args.error.empty()) return args;
        } else if (arg == "--trace-limit") {
            std::string val = needValue("--trace-limit");
            if (!args.error.empty()) return args;
            try {
                args.trace_limit = std::stoi(val);
            } catch (const std::exception&) {
                args.error = "Could not parse --trace-limit as an integer";
                return args;
            }
        } else {
            args.error = "Unknown argument: " + arg;
            return args;
        }
    }

    if (args.targets.empty()) {
        args.error = "No --targets provided (e.g. --targets 0.0,0.5,1.1)";
        return args;
    }

    args.ok = true;
    return args;
}

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <input> --targets 0.0,0.5,1.1,4.75 --output report.json "
                 "[--trace-limit N]\n";
}

}  // namespace

int main(int argc, char** argv) {
    CliArgs args = parseArgs(argc, argv);
    if (!args.ok) {
        std::cerr << "error: " << args.error << "\n";
        printUsage(argv[0]);
        return 2;
    }

    mediacore::MediaInspector::Options opts;
    opts.trace_limit = args.trace_limit;
    mediacore::MediaInspector inspector(opts);

    int exit_code = 0;

    bool opened = inspector.open(args.input);
    std::vector<mediacore::FrameRequestResult> frame_requests;

    if (opened) {
        bool analyzed = inspector.analyze();
        if (analyzed) {
            for (double t : args.targets) {
                frame_requests.push_back(inspector.requestFrame(t));
            }
        } else {
            exit_code = 1;
        }
    } else {
        exit_code = 1;
    }

    // We always write a report, even on failure, so the JSON output is a
    // consistent artifact: check report["errors"] to see what went wrong.
    nlohmann::json report =
        mediacore::buildReport(args.input, args.targets, inspector, frame_requests);

    std::ofstream out(args.output);
    if (!out) {
        std::cerr << "error: could not open output file '" << args.output << "' for writing\n";
        return 1;
    }
    out << report.dump(2) << "\n";
    out.close();

    if (!inspector.errors().empty()) {
        std::cerr << "media-core: completed with " << inspector.errors().size()
                  << " error(s); see '" << args.output << "' errors[] for detail\n";
    }
    if (exit_code != 0) {
        std::cerr << "media-core: failed to analyze '" << args.input << "'\n";
    } else {
        std::cerr << "media-core: wrote report to '" << args.output << "'\n";
    }

    return exit_code;
}
