#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lsw::suites {
struct Options {
    bool list = false;
    bool help = false;
    std::vector<std::string> suites{"sanity"};
    std::string filter;
    std::filesystem::path exclude_file;
    std::string tag;
    std::string phase = "all";
    int jobs = 0;
    int timeout_seconds = 0;
    int repeat = 1;
    std::optional<unsigned> shuffle;
    std::filesystem::path json;
    std::filesystem::path artifacts = "lsw-suites-artifacts";
    std::string run_one;
    std::string skip_apis;
    std::filesystem::path refs;
    bool dump_captures = false;
    std::string device_under_test = "av";

    [[nodiscard]] static std::optional<Options> parse(int argc, char** argv, std::string& error);

    [[nodiscard]] static std::string usage();
};
}  // namespace lsw::suites
