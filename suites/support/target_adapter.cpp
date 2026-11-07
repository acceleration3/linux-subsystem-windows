#include "support/target_adapter.hpp"

#include <cstdlib>

namespace lsw::suites {

bool TargetAdapter::pick(AvAdapter& out, std::string& error) {
    if (std::getenv("LSW_SUITES_DEV_ANY") != nullptr) {
        std::vector<AvAdapter> adapters;
        if (!AvAdapters::all(adapters, error)) {
            return false;
        }
        for (AvAdapter& adapter : adapters) {
            if (!adapter.software) {
                out = std::move(adapter);
                return true;
            }
        }
        error = "no hardware adapter";
        return false;
    }
    std::vector<AvAdapter> adapters;
    if (!AvAdapters::accel(adapters, error)) {
        return false;
    }
    out = std::move(adapters.front());
    return true;
}

}  // namespace lsw::suites
