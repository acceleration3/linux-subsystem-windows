#include "support/process_sampler.hpp"

#include <windows.h>
#include <psapi.h>

namespace lsw::suites {

ProcessSample ProcessSampler::take() {
    ProcessSample sample;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                               sizeof(counters)) != FALSE) {
        sample.private_bytes = counters.PrivateUsage;
    }
    DWORD handles = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &handles);
    sample.handles = handles;
    return sample;
}

std::uint64_t ProcessSampler::available_physical() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return ::GlobalMemoryStatusEx(&status) != FALSE ? status.ullAvailPhys : 0;
}

}  // namespace lsw::suites
