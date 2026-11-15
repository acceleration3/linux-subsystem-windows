#include "support/api_policy.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace lsw::suites {

std::vector<std::string> ApiPolicy::known() {
    return {"d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl"};
}

std::vector<std::string> ApiPolicy::skipped() {
    std::vector<std::string> out;
    const char* value = std::getenv(kVariable);
    if (value == nullptr) {
        return out;
    }
    std::string item;
    for (const char* at = value;; ++at) {
        if (*at == ',' || *at == '\0') {
            if (!item.empty()) {
                out.push_back(item);
            }
            item.clear();
            if (*at == '\0') {
                break;
            }
        } else if (*at != ' ') {
            item.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*at))));
        }
    }
    return out;
}

bool ApiPolicy::is_skipped(const std::string& api) {
    const std::vector<std::string> list = skipped();
    return std::find(list.begin(), list.end(), api) != list.end();
}

std::string ApiPolicy::first_enabled(const std::vector<std::string>& preferred) {
    for (const std::string& api : preferred) {
        if (!is_skipped(api)) {
            return api;
        }
    }
    return {};
}

std::vector<std::string> ApiPolicy::filter(const std::vector<std::string>& apis) {
    std::vector<std::string> out;
    for (const std::string& api : apis) {
        if (!is_skipped(api)) {
            out.push_back(api);
        }
    }
    return out;
}

}  // namespace lsw::suites
