#pragma once

#include "support/dyn_library.hpp"

#include <cstdint>
#include <string>

namespace lsw::suites {
class KmtExtra {
public:
    static constexpr std::int32_t kStatusSuccess = 0;
    static constexpr std::int32_t kStatusInvalidParameter = static_cast<std::int32_t>(0xC000000D);

    [[nodiscard]] bool load(std::string& error);

    [[nodiscard]] std::int32_t wait_vblank(std::uint32_t adapter, std::uint32_t source) const;

    [[nodiscard]] std::int32_t scan_line(std::uint32_t adapter, std::uint32_t source, bool& in_vblank, std::uint32_t& line) const;

    [[nodiscard]] std::int32_t exclusive_ownership(std::uint32_t adapter, std::uint32_t source) const;

    [[nodiscard]] std::int32_t open_from_hdc(HDC dc, std::uint32_t& adapter, LUID& luid, std::uint32_t& source) const;

    [[nodiscard]] std::int32_t open_from_display_name(const std::wstring& name, std::uint32_t& adapter, LUID& luid, std::uint32_t& source) const;

    [[nodiscard]] std::int32_t escape(std::uint32_t adapter, std::uint32_t type, std::uint32_t flags, void* data, std::uint32_t size) const;

private:
    DynLibrary gdi_;
    std::int32_t(__stdcall* wait_)(void*) = nullptr;
    std::int32_t(__stdcall* scan_)(void*) = nullptr;
    std::int32_t(__stdcall* owner_)(void*) = nullptr;
    std::int32_t(__stdcall* hdc_)(void*) = nullptr;
    std::int32_t(__stdcall* name_)(void*) = nullptr;
    std::int32_t(__stdcall* escape_)(void*) = nullptr;
};
}  // namespace lsw::suites
