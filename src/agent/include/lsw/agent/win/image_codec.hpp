#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lsw::agent::win {
class ImageCodec {
public:
    ImageCodec() = delete;

    static constexpr std::size_t kMaxDibBytes = 64U * 1024U * 1024U;

    [[nodiscard]] static std::vector<std::uint8_t> dib_to_png(const std::uint8_t* dib,
                                                              std::size_t size);
    [[nodiscard]] static std::vector<std::uint8_t> png_to_dib(const std::vector<std::uint8_t>& png);
};

}
