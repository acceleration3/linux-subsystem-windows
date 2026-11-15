#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lsw::suites {
class PngWriter {
public:
    [[nodiscard]] static std::vector<std::uint8_t> encode_bgra(const void* pixels,
                                                               std::uint32_t width,
                                                               std::uint32_t height,
                                                               std::uint32_t stride);

    [[nodiscard]] static bool write_bgra(const std::filesystem::path& path, const void* pixels,
                                         std::uint32_t width, std::uint32_t height,
                                         std::uint32_t stride);

private:
    [[nodiscard]] static std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                                             std::uint32_t crc = 0);

    static void chunk(std::vector<std::uint8_t>& out, const char* type,
                      const std::vector<std::uint8_t>& data);
};
}  // namespace lsw::suites
