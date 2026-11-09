#pragma once

#include "render/common/image.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lsw::render {
class Png {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode(const Image& image);
    [[nodiscard]] static bool write(const std::filesystem::path& path, const Image& image);

private:
    [[nodiscard]] static std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc);
    static void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data);
};
}  // namespace lsw::render
