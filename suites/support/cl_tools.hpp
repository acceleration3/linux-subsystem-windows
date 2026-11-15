#pragma once

#include "support/cl_api.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace lsw::suites {
struct ClVersion {
    int major = 0;
    int minor = 0;

    [[nodiscard]] bool at_least(int wanted_major, int wanted_minor) const {
        return major > wanted_major || (major == wanted_major && minor >= wanted_minor);
    }

    [[nodiscard]] bool operator<(const ClVersion& other) const {
        return major != other.major ? major < other.major : minor < other.minor;
    }

    [[nodiscard]] std::string text() const { return std::to_string(major) + "." + std::to_string(minor); }
};

class ClTools {
public:
    [[nodiscard]] static std::string error_name(cl_int code);

    [[nodiscard]] static bool parse_version(const std::string& text, const std::string& prefix, ClVersion& out,
                                            std::string* rest = nullptr);

    [[nodiscard]] static std::vector<std::string> split(const std::string& text);

    [[nodiscard]] static bool has_word(const std::string& list, const std::string& word);

    [[nodiscard]] static bool code_in(cl_int code, std::initializer_list<cl_int> accepted);

    [[nodiscard]] static std::string accepted_text(std::initializer_list<cl_int> accepted);

    [[nodiscard]] static std::string text_of(const std::vector<char>& blob);

    [[nodiscard]] static std::string bytes_text(std::uint64_t bytes);
};
}  // namespace lsw::suites
