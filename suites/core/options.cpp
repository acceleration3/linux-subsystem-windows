#include "core/options.hpp"

#include "core/check_filter.hpp"

#include <cstdlib>

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
    return "usage: lsw-suites [--list] [--suite NAME[,NAME]] [--filter GLOB[,-GLOB]] [--tag TAG]\n"
           "                  [--jobs N] [--timeout S] [--repeat N] [--shuffle SEED]\n"
           "                  [--json FILE] [--artifacts DIR]\n";
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
        } else if (arg == "--tag") {
            options.tag = value;
        } else if (arg == "--json") {
            options.json = value;
        } else if (arg == "--artifacts") {
            options.artifacts = value;
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
    if (options.suites.empty()) {
        error = "--suite needs at least one name";
        return std::nullopt;
    }
    return options;
}

}  // namespace lsw::suites
