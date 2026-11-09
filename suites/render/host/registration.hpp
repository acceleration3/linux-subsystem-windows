#pragma once

#include "core/check.hpp"
#include "core/context.hpp"
#include "core/suite.hpp"
#include "render/host/guest_types.hpp"

#include <functional>
#include <string>

namespace lsw::suites::render {
class CaseRegistration {
public:
    static void add(Suite& suite, const std::string& api, const std::string& api_tag, const rr::CaseSpec& spec,
                    std::function<Outcome(Context&)> run);
};
}  // namespace lsw::suites::render
