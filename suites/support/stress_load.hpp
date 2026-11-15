#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct StressResult {
    std::uint64_t bytes = 0;
    double seconds = 0;
};

class StressLoad {
public:
    [[nodiscard]] static Outcome roundtrips(Context& context, double seconds, const std::vector<std::uint64_t>& sizes,
                                            StressResult* result = nullptr);

    [[nodiscard]] static Outcome render(Context& context, double seconds);

    [[nodiscard]] static Outcome compute(Context& context, double seconds);

    [[nodiscard]] static Outcome hog(Context& context, double seconds);

};
}  // namespace lsw::suites
