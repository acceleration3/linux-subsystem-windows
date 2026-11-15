#pragma once

#include "core/context.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <functional>
#include <string>

namespace lsw::suites {
class LeakLoop {
public:
    static constexpr std::uint64_t kPrivateTolerance = 8ull << 20;
    static constexpr std::uint32_t kHandleTolerance = 16;
    static constexpr std::uint64_t kGpuTolerance = 16ull << 20;

    static void run(Context& context, Problems& problems, const std::string& label, const LUID& luid, int count,
                    const std::function<bool(int, std::string&)>& churn);
};
}  // namespace lsw::suites
