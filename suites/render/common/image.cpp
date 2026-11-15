#include "render/common/image.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::render {

namespace {

enum class Element { U8, U16, F16, F32, U32 };

struct KindRow {
    const char* name;
    unsigned channels;
    Element element;
    ChannelClass klass;
    bool swapped;
};

const KindRow kRows[] = {
    {"rgba8", 4, Element::U8, ChannelClass::Unorm8, false},
    {"bgra8", 4, Element::U8, ChannelClass::Unorm8, true},
    {"rgba16f", 4, Element::F16, ChannelClass::Float, false},
    {"rgba32f", 4, Element::F32, ChannelClass::Float, false},
    {"r32f", 1, Element::F32, ChannelClass::Float, false},
    {"depth32f", 1, Element::F32, ChannelClass::Float, false},
    {"rgba32ui", 4, Element::U32, ChannelClass::Integer, false},
    {"r8ui", 1, Element::U8, ChannelClass::Integer, false},
    {"r8", 1, Element::U8, ChannelClass::Unorm8, false},
    {"rg8", 2, Element::U8, ChannelClass::Unorm8, false},
    {"r16f", 1, Element::F16, ChannelClass::Float, false},
    {"rg16f", 2, Element::F16, ChannelClass::Float, false},
    {"r16", 1, Element::U16, ChannelClass::Unorm16, false},
    {"r16ui", 1, Element::U16, ChannelClass::Integer, false},
    {"rg32f", 2, Element::F32, ChannelClass::Float, false},
    {"rgba16", 4, Element::U16, ChannelClass::Unorm16, false},
    {"rgba8ui", 4, Element::U8, ChannelClass::Integer, false},
    {"rgba16ui", 4, Element::U16, ChannelClass::Integer, false},
};

const KindRow& kind_row(PixelKind kind) { return kRows[static_cast<std::size_t>(kind)]; }

std::size_t element_bytes(Element element) {
    switch (element) {
        case Element::U8: return 1;
        case Element::U16:
        case Element::F16: return 2;
        default: return 4;
    }
}

}  // namespace

bool PixelInfo::valid(std::uint32_t kind) { return kind < static_cast<std::uint32_t>(PixelKind::Count); }

ChannelClass PixelInfo::channel_class(PixelKind kind) { return kind_row(kind).klass; }

std::size_t PixelInfo::bytes(PixelKind kind) { return kind_row(kind).channels * element_bytes(kind_row(kind).element); }

unsigned PixelInfo::channels(PixelKind kind) { return kind_row(kind).channels; }

bool PixelInfo::is_float(PixelKind kind) { return kind_row(kind).klass == ChannelClass::Float; }

bool PixelInfo::is_integer(PixelKind kind) { return kind_row(kind).klass == ChannelClass::Integer; }

const char* PixelInfo::name(PixelKind kind) { return kind_row(kind).name; }

float PixelInfo::half_to_float(std::uint16_t half) {
    const std::uint32_t sign = (static_cast<std::uint32_t>(half) & 0x8000u) << 16;
    std::uint32_t exponent = (half >> 10) & 0x1fu;
    std::uint32_t mantissa = half & 0x3ffu;
    std::uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

std::uint16_t PixelInfo::float_to_half(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7fffffu;
    if (((bits >> 23) & 0xffu) == 0xffu) {
        return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa != 0 ? 0x200u : 0u));
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000u;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t half = mantissa >> shift;
        if (((mantissa >> (shift - 1)) & 1u) != 0) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    if ((mantissa & 0x1000u) != 0) {
        ++half;
    }
    return static_cast<std::uint16_t>(half);
}

Image::Image(std::uint32_t width, std::uint32_t height, PixelKind kind)
    : width_(width), height_(height), kind_(kind), bytes_(static_cast<std::size_t>(width) * height * PixelInfo::bytes(kind)) {}

double Image::channel(std::uint32_t x, std::uint32_t y, unsigned index) const {
    const KindRow& r = kind_row(kind_);
    if (index >= r.channels) {
        return index == 3 ? (r.klass == ChannelClass::Integer ? 1.0 : (r.klass == ChannelClass::Unorm16 ? 65535.0 : (r.klass == ChannelClass::Unorm8 ? 255.0 : 1.0))) : 0.0;
    }
    const std::uint8_t* pixel = row(y) + static_cast<std::size_t>(x) * PixelInfo::bytes(kind_);
    if (r.swapped) {
        static constexpr unsigned swap[4] = {2, 1, 0, 3};
        index = swap[index];
    }
    switch (r.element) {
        case Element::U8:
            return pixel[index];
        case Element::U16: {
            std::uint16_t value;
            std::memcpy(&value, pixel + index * 2, 2);
            return value;
        }
        case Element::F16: {
            std::uint16_t half;
            std::memcpy(&half, pixel + index * 2, 2);
            return PixelInfo::half_to_float(half);
        }
        case Element::F32: {
            float value;
            std::memcpy(&value, pixel + index * 4, 4);
            return value;
        }
        case Element::U32: {
            std::uint32_t value;
            std::memcpy(&value, pixel + index * 4, 4);
            return value;
        }
    }
    return 0;
}

void Image::set_channel(std::uint32_t x, std::uint32_t y, unsigned index, double value) {
    const KindRow& r = kind_row(kind_);
    if (index >= r.channels) {
        return;
    }
    std::uint8_t* pixel = row(y) + static_cast<std::size_t>(x) * PixelInfo::bytes(kind_);
    if (r.swapped) {
        static constexpr unsigned swap[4] = {2, 1, 0, 3};
        index = swap[index];
    }
    switch (r.element) {
        case Element::U8:
            pixel[index] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
            break;
        case Element::U16: {
            const std::uint16_t v = static_cast<std::uint16_t>(std::clamp(value, 0.0, 65535.0));
            std::memcpy(pixel + index * 2, &v, 2);
            break;
        }
        case Element::F16: {
            const std::uint16_t half = PixelInfo::float_to_half(static_cast<float>(value));
            std::memcpy(pixel + index * 2, &half, 2);
            break;
        }
        case Element::F32: {
            const float f = static_cast<float>(value);
            std::memcpy(pixel + index * 4, &f, 4);
            break;
        }
        case Element::U32: {
            const std::uint32_t u = static_cast<std::uint32_t>(std::clamp(value, 0.0, 4294967295.0));
            std::memcpy(pixel + index * 4, &u, 4);
            break;
        }
    }
}

std::uint32_t Image::rgba8(std::uint32_t x, std::uint32_t y) const {
    std::uint32_t out = 0;
    for (unsigned c = 0; c < 4; ++c) {
        out |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(channel(x, y, c))) << (8 * c);
    }
    return out;
}

void Image::set_rgba8(std::uint32_t x, std::uint32_t y, std::uint32_t rgba) {
    for (unsigned c = 0; c < 4; ++c) {
        set_channel(x, y, c, static_cast<double>((rgba >> (8 * c)) & 0xffu));
    }
}

Image Image::to_rgba8() const {
    Image out(width_, height_, PixelKind::Rgba8Unorm);
    const ChannelClass klass = PixelInfo::channel_class(kind_);
    const unsigned channels = PixelInfo::channels(kind_);
    for (std::uint32_t y = 0; y < height_; ++y) {
        for (std::uint32_t x = 0; x < width_; ++x) {
            double v[4];
            for (unsigned c = 0; c < 4; ++c) {
                const double raw = c < channels ? channel(x, y, c) : (c == 3 ? 1.0 : 0.0);
                switch (klass) {
                    case ChannelClass::Unorm8:
                        v[c] = c < channels ? raw : (c == 3 ? 255.0 : 0.0);
                        break;
                    case ChannelClass::Unorm16:
                        v[c] = c < channels ? raw / 257.0 : (c == 3 ? 255.0 : 0.0);
                        break;
                    case ChannelClass::Integer:
                        v[c] = c < channels ? std::min(raw, 255.0) : (c == 3 ? 255.0 : 0.0);
                        break;
                    case ChannelClass::Float:
                        v[c] = std::isnan(raw) ? 0.0 : std::clamp(raw, 0.0, 1.0) * 255.0 + 0.5;
                        break;
                }
            }
            if (channels == 1) {
                v[1] = v[2] = v[0];
                v[3] = 255.0;
            }
            out.set_rgba8(x, y,
                          static_cast<std::uint32_t>(v[0]) | (static_cast<std::uint32_t>(v[1]) << 8) |
                              (static_cast<std::uint32_t>(v[2]) << 16) | (static_cast<std::uint32_t>(v[3]) << 24));
        }
    }
    return out;
}

Image Image::copy_region(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) const {
    Image out(width, height, kind_);
    const std::size_t pixel = PixelInfo::bytes(kind_);
    for (std::uint32_t r = 0; r < height; ++r) {
        std::memcpy(out.row(r), row(y + r) + static_cast<std::size_t>(x) * pixel, static_cast<std::size_t>(width) * pixel);
    }
    return out;
}

void Image::flip_vertical() {
    std::vector<std::uint8_t> scratch(stride());
    for (std::uint32_t y = 0; y < height_ / 2; ++y) {
        std::uint8_t* a = row(y);
        std::uint8_t* b = row(height_ - 1 - y);
        std::memcpy(scratch.data(), a, scratch.size());
        std::memcpy(a, b, scratch.size());
        std::memcpy(b, scratch.data(), scratch.size());
    }
}

Image Image::solid(std::uint32_t width, std::uint32_t height, std::uint32_t rgba) {
    Image out(width, height, PixelKind::Rgba8Unorm);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            out.set_rgba8(x, y, rgba);
        }
    }
    return out;
}

}  // namespace lsw::render
