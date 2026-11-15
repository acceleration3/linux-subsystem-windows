#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct AvOutput {
    std::string device_name;
    RECT rect{};
    bool attached = false;
    HMONITOR monitor = nullptr;
};

struct AvAdapter {
    std::string name;
    std::wstring raw_name;
    LUID luid{};
    std::uint32_t vendor = 0;
    std::uint32_t device = 0;
    std::uint32_t subsystem = 0;
    std::uint32_t revision = 0;
    std::uint64_t dedicated_video = 0;
    std::uint64_t dedicated_system = 0;
    std::uint64_t shared_system = 0;
    bool software = false;
    bool hybrid_discrete = false;
    bool hybrid_integrated = false;
    bool umd_version_known = false;
    std::uint64_t umd_version = 0;
    std::vector<AvOutput> outputs;

    [[nodiscard]] bool accel() const { return subsystem == 0x52561AF4; }

    [[nodiscard]] bool has_outputs() const { return !outputs.empty(); }


    [[nodiscard]] std::string luid_text() const;

    [[nodiscard]] std::uint64_t luid_key() const;
};

class AvAdapters {
public:
    [[nodiscard]] static bool all(std::vector<AvAdapter>& out, std::string& error);

    [[nodiscard]] static bool accel(std::vector<AvAdapter>& out, std::string& error);

    [[nodiscard]] static const AvAdapter* find(const std::vector<AvAdapter>& adapters,
                                               std::uint64_t luid_key);

    [[nodiscard]] static std::uint64_t key(const LUID& luid);

    [[nodiscard]] static IUnknown* open_adapter(const LUID& luid);
};
}  // namespace lsw::suites
