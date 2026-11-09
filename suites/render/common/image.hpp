#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lsw::render {
enum class PixelKind : std::uint32_t {
    Rgba8Unorm = 0,
    Bgra8Unorm = 1,
    Rgba16Float = 2,
    Rgba32Float = 3,
    R32Float = 4,
    Depth32Float = 5,
    Rgba32Uint = 6,
    R8Uint = 7,
};

class PixelInfo {
public:
    [[nodiscard]] static std::size_t bytes(PixelKind kind);
    [[nodiscard]] static unsigned channels(PixelKind kind);
    [[nodiscard]] static bool is_float(PixelKind kind);
    [[nodiscard]] static bool is_integer(PixelKind kind);
    [[nodiscard]] static const char* name(PixelKind kind);
    [[nodiscard]] static float half_to_float(std::uint16_t half);
    [[nodiscard]] static std::uint16_t float_to_half(float value);
};

class Image {
public:
    Image() = default;
    Image(std::uint32_t width, std::uint32_t height, PixelKind kind);

    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] PixelKind kind() const { return kind_; }
    [[nodiscard]] bool empty() const { return bytes_.empty(); }
    [[nodiscard]] std::size_t stride() const { return static_cast<std::size_t>(width_) * PixelInfo::bytes(kind_); }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    [[nodiscard]] std::vector<std::uint8_t>& bytes() { return bytes_; }
    [[nodiscard]] std::uint8_t* row(std::uint32_t y) { return bytes_.data() + stride() * y; }
    [[nodiscard]] const std::uint8_t* row(std::uint32_t y) const { return bytes_.data() + stride() * y; }

    [[nodiscard]] double channel(std::uint32_t x, std::uint32_t y, unsigned index) const;
    void set_channel(std::uint32_t x, std::uint32_t y, unsigned index, double value);

    [[nodiscard]] std::uint32_t rgba8(std::uint32_t x, std::uint32_t y) const;
    void set_rgba8(std::uint32_t x, std::uint32_t y, std::uint32_t rgba);

    [[nodiscard]] Image to_rgba8() const;
    [[nodiscard]] Image copy_region(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) const;
    void flip_vertical();

    [[nodiscard]] static Image solid(std::uint32_t width, std::uint32_t height, std::uint32_t rgba);

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    PixelKind kind_ = PixelKind::Rgba8Unorm;
    std::vector<std::uint8_t> bytes_;
};
}  // namespace lsw::render
