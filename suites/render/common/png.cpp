#include "render/common/png.hpp"

#include <algorithm>
#include <fstream>

namespace lsw::render {

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

}  // namespace

std::uint32_t Png::crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return ~crc;
}

void Png::chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size(), 0));
}

std::vector<std::uint8_t> Png::encode(const Image& source) {
    const Image image = source.kind() == PixelKind::Rgba8Unorm ? source : source.to_rgba8();
    std::vector<std::uint8_t> raw;
    raw.reserve((image.stride() + 1) * image.height());
    for (std::uint32_t y = 0; y < image.height(); ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), image.row(y), image.row(y) + image.stride());
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t offset = 0; offset < raw.size() || offset == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xff));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xff));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xff));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                 raw.begin() + static_cast<std::ptrdiff_t>(offset + n));
        for (std::size_t i = 0; i < n; ++i) {
            a = (a + raw[offset + i]) % 65521u;
            b = (b + a) % 65521u;
        }
        offset += n;
        if (last) {
            break;
        }
    }
    put32(z, (b << 16) | a);
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> header;
    put32(header, image.width());
    put32(header, image.height());
    header.push_back(8);
    header.push_back(6);
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);
    chunk(out, "IHDR", header);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

bool Png::write(const std::filesystem::path& path, const Image& image) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    const std::vector<std::uint8_t> bytes = encode(image);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

}  // namespace lsw::render
