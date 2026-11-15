#pragma once

#include <windows.h>

#include <string>

namespace lsw::suites {
struct PresentResult {
    bool ok = false;
    bool occluded = false;
    bool stale = false;
    HRESULT hr = S_OK;
    std::string error;
};
}  // namespace lsw::suites
