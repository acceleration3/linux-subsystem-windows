#pragma once

#include <dxgiformat.h>

#include <cstdint>
#include <vector>

namespace lsw::suites {
class BcCodec {
public:
    [[nodiscard]] static bool has_reference(DXGI_FORMAT format);

    [[nodiscard]] static int family(DXGI_FORMAT format);

    [[nodiscard]] static DXGI_FORMAT unorm_format(int family);

    [[nodiscard]] static DXGI_FORMAT srgb_format(int family);

    [[nodiscard]] static DXGI_FORMAT typeless_format(int family);

    [[nodiscard]] static int block_bytes(int family) { return family == 1 || family == 4 ? 8 : 16; }

    static void decode(int family, bool is_signed, const std::uint8_t* block, float texels[16][4]);

    [[nodiscard]] static std::vector<std::uint8_t> blocks(int family, std::uint64_t seed, int count);

    [[nodiscard]] static double tolerance(int family, bool endpoint);

    [[nodiscard]] static std::uint32_t blocks_across(std::uint32_t texels) { return texels == 0 ? 0 : (texels + 3) / 4; }
};
}  // namespace lsw::suites
