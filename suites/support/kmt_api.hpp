#pragma once

#include "support/dyn_library.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct KmtAdapter {
    std::uint32_t handle = 0;
    LUID luid{};
    std::uint32_t sources = 0;
};

struct KmtSegmentSizes {
    std::uint64_t dedicated_video = 0;
    std::uint64_t dedicated_system = 0;
    std::uint64_t shared_system = 0;
};

struct KmtAddress {
    std::uint32_t bus = 0;
    std::uint32_t device = 0;
    std::uint32_t function = 0;
};

class KmtApi {
public:
    static constexpr std::int32_t kStatusBufferTooSmall = static_cast<std::int32_t>(0xC0000023);

    [[nodiscard]] bool load(std::string& error);

    [[nodiscard]] bool enum_adapters(std::vector<KmtAdapter>& out, std::string& error) const;

    [[nodiscard]] std::int32_t open_from_luid(const LUID& luid, std::uint32_t& handle) const;

    [[nodiscard]] std::int32_t close(std::uint32_t handle) const;

    [[nodiscard]] std::int32_t query(std::uint32_t handle, std::uint32_t type, void* data,
                                     std::uint32_t size) const;

    [[nodiscard]] bool segment_sizes(const LUID& luid, KmtSegmentSizes& out) const;

    [[nodiscard]] bool address(const LUID& luid, KmtAddress& out) const;

    [[nodiscard]] bool adapter_string(const LUID& luid, std::wstring& out) const;

    [[nodiscard]] std::wstring umd_name(const LUID& luid, std::uint32_t version) const;

private:
    DynLibrary gdi_;
    std::int32_t(__stdcall* enum_)(void*) = nullptr;
    std::int32_t(__stdcall* close_)(void*) = nullptr;
    std::int32_t(__stdcall* open_luid_)(void*) = nullptr;
    std::int32_t(__stdcall* query_)(void*) = nullptr;
};
}  // namespace lsw::suites
