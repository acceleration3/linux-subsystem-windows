#pragma once

#include "core/check_result.hpp"
#include "core/json.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace lsw::suites {
class JsonReport {
public:
    JsonReport(std::string exe_name, std::string started);

    [[nodiscard]] static Json check_json(const CheckResult& result);

    [[nodiscard]] bool write(const std::filesystem::path& path,
                             const std::vector<CheckResult>& results, bool complete) const;

private:
    std::string exe_;
    std::string started_;
};
}  // namespace lsw::suites
