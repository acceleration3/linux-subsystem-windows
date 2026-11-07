#pragma once

#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/problems.hpp"

#include <functional>

namespace lsw::suites {
class ApiLoop {
public:
    using Body = std::function<void(GpuBackend&, const std::string&, const AvAdapter&)>;

    static void run(Problems& problems, const std::vector<std::string>& apis, const Body& body) {
        for (const std::string& api : ApiPolicy::filter(apis)) {
            AvAdapter adapter;
            std::string error;
            std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
            if (backend == nullptr) {
                problems.add(error);
                continue;
            }
            body(*backend, api, adapter);
        }
    }
};
}  // namespace lsw::suites
