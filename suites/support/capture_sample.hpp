#pragma once

namespace lsw::suites {
struct CaptureSample {
    double time_ms = 0.0;
    long long arrival = 0;
    int raw = -1;
    unsigned accumulated = 0;
};
}  // namespace lsw::suites
