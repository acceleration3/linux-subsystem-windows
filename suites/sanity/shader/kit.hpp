#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "support/api_policy.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <string>
#include <vector>

namespace lsw::suites::sanity::shader {
class Kit {
public:
    [[nodiscard]] static bool enabled(const char* api) { return !ApiPolicy::is_skipped(api); }

    [[nodiscard]] static Outcome finish(const std::vector<std::string>& skips, const Problems& problems, bool exercised) {
        if (!problems.empty()) {
            return problems.outcome();
        }
        if (!exercised) {
            std::string reason;
            for (const std::string& item : skips) {
                reason += (reason.empty() ? "" : "; ") + item;
            }
            return Context::skip(reason.empty() ? "no API of this row is enabled" : reason);
        }
        return Outcome::ok();
    }

    static void merge(const char* api, const Outcome& outcome, Problems& problems, std::vector<std::string>& skips, bool& exercised) {
        if (outcome.status() == Status::Fail) {
            problems.add(std::string(api) + ": " + outcome.reason());
        } else if (outcome.status() == Status::Skip) {
            skips.push_back(std::string(api) + ": " + outcome.reason());
        } else {
            exercised = true;
        }
    }
};
}  // namespace lsw::suites::sanity::shader
