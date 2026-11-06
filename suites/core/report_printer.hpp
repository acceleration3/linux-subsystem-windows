#pragma once

#include "core/check_result.hpp"

#include <cstdio>
#include <string>

namespace lsw::suites {
class ReportPrinter {
public:
    explicit ReportPrinter(std::string exe_name) : exe_(std::move(exe_name)) {}

    void print(const CheckResult& result) const;

    void summary(int passed, int failed, int skipped) const;

private:
    std::string exe_;
};
}  // namespace lsw::suites
