#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace lsw::suites {
class FileVersion {
public:
    [[nodiscard]] static std::optional<std::uint64_t> read(const std::wstring& path);

    [[nodiscard]] static std::string text(std::uint64_t version);

    [[nodiscard]] static std::optional<std::uint64_t> parse(const std::string& text);
};
}  // namespace lsw::suites
