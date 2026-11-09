#include "core/options.hpp"

#include "core/check_filter.hpp"

#include <cstdlib>
#include <fstream>

namespace lsw::suites {

namespace {

bool positive(const std::string& text, int& out) {
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < 1 || value > 1000000) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

}  // namespace

std::string Options::usage() {
    return "usage: lsw-suites [--list] [--suite NAME[,NAME]] [--filter GLOB[,-GLOB]] [--exclude-file PATH]\n"
           "                  [--tag TAG] [--phase all|main|pacing] [--jobs N] [--timeout S] [--repeat N] [--shuffle SEED]\n"
           "                  [--json FILE] [--artifacts DIR] [--skip-apis API[,API]] [--refs DIR]\n"
           "                  [--dump-captures] [--device-under-test av|warp]\n";
}

std::optional<Options> Options::parse(int argc, char** argv, std::string& error) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") {
            options.list = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            continue;
        }
        if (arg == "--dump-captures") {
            options.dump_captures = true;
            continue;
        }
        if (i + 1 >= argc) {
            error = "unknown option or missing value: " + arg;
            return std::nullopt;
        }
        const std::string value = argv[++i];
        int number = 0;
        if (arg == "--suite") {
            options.suites = CheckFilter::split(value);
        } else if (arg == "--filter") {
            options.filter = value;
        } else if (arg == "--exclude-file") {
            options.exclude_file = value;
        } else if (arg == "--tag") {
            options.tag = value;
        } else if (arg == "--phase") {
            if (value != "all" && value != "main" && value != "pacing") {
                error = "--phase needs all, main or pacing, got " + value;
                return std::nullopt;
            }
            options.phase = value;
        } else if (arg == "--json") {
            options.json = value;
        } else if (arg == "--artifacts") {
            options.artifacts = value;
        } else if (arg == "--skip-apis") {
            options.skip_apis = value;
        } else if (arg == "--refs") {
            options.refs = value;
        } else if (arg == "--device-under-test") {
            if (value != "av" && value != "warp") {
                error = "--device-under-test needs av or warp, got " + value;
                return std::nullopt;
            }
            options.device_under_test = value;
        } else if (arg == "--run-one") {
            options.run_one = value;
        } else if (arg == "--jobs" || arg == "--timeout" || arg == "--repeat") {
            if (!positive(value, number)) {
                error = arg + " needs a positive number, got " + value;
                return std::nullopt;
            }
            (arg == "--jobs" ? options.jobs
                             : arg == "--timeout" ? options.timeout_seconds : options.repeat) = number;
        } else if (arg == "--shuffle") {
            options.shuffle = static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 10));
        } else {
            error = "unknown option: " + arg;
            return std::nullopt;
        }
    }
    if (!options.exclude_file.empty()) {
        std::ifstream file(options.exclude_file);
        if (!file) {
            error = "--exclude-file cannot be read: " + options.exclude_file.string();
            return std::nullopt;
        }
        std::string line;
        while (std::getline(file, line)) {
            const std::size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                continue;
            }
            const std::size_t last = line.find_last_not_of(" \t\r\n");
            options.filter += std::string(options.filter.empty() ? "" : ",") + "-" + line.substr(first, last - first + 1);
        }
    }
    if (options.suites.empty()) {
        error = "--suite needs at least one name";
        return std::nullopt;
    }
    return options;
}

}  // namespace lsw::suites
