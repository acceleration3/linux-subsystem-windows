#pragma once

#include "support/gl_api.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
struct GlFetchRequest {
    std::string sampler;
    std::string expression;
    unsigned target = 0;
    unsigned texture = 0;
    int width = 0;
    int height = 0;
    int first = 0;
    int second = 0;
};

class GlFetch {
public:
    [[nodiscard]] static bool run(const GlApi& api, const GlFetchRequest& request, std::vector<float>& out, std::string& error);
};
}  // namespace lsw::suites
