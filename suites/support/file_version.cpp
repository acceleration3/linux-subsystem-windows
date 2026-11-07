#include "support/file_version.hpp"

#include <windows.h>

#include <vector>

namespace lsw::suites {

std::optional<std::uint64_t> FileVersion::read(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<char> data(size);
    if (::GetFileVersionInfoW(path.c_str(), 0, size, data.data()) == FALSE) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (::VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) == FALSE ||
        info == nullptr) {
        return std::nullopt;
    }
    return (static_cast<std::uint64_t>(info->dwFileVersionMS) << 32) | info->dwFileVersionLS;
}

std::string FileVersion::text(std::uint64_t version) {
    return std::to_string((version >> 48) & 0xffff) + "." + std::to_string((version >> 32) & 0xffff) +
           "." + std::to_string((version >> 16) & 0xffff) + "." + std::to_string(version & 0xffff);
}

std::optional<std::uint64_t> FileVersion::parse(const std::string& text) {
    std::uint64_t version = 0;
    std::size_t at = 0;
    for (int part = 0; part < 4; ++part) {
        if (at >= text.size() || text[at] < '0' || text[at] > '9') {
            return std::nullopt;
        }
        unsigned long value = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            value = value * 10 + static_cast<unsigned long>(text[at] - '0');
            ++at;
        }
        if (value > 0xffff) {
            return std::nullopt;
        }
        version = (version << 16) | value;
        if (part < 3) {
            if (at >= text.size() || text[at] != '.') {
                return std::nullopt;
            }
            ++at;
        }
    }
    return version;
}

}  // namespace lsw::suites
