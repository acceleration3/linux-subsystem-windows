#include "support/png_writer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace lsw::suites {

namespace {

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

}  // namespace

std::uint32_t PngWriter::crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    static const auto table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

void PngWriter::chunk(std::vector<std::uint8_t>& out, const char* type,
                      const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

std::vector<std::uint8_t> PngWriter::encode_bgra(const void* pixels, std::uint32_t width,
                                                 std::uint32_t height, std::uint32_t stride) {
    const auto* source = static_cast<const std::uint8_t*>(pixels);
    std::vector<std::uint8_t> raw;
    raw.reserve((static_cast<std::size_t>(width) * 4 + 1) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = source + static_cast<std::size_t>(y) * stride;
        for (std::uint32_t x = 0; x < width; ++x) {
            raw.push_back(row[x * 4 + 2]);
            raw.push_back(row[x * 4 + 1]);
            raw.push_back(row[x * 4 + 0]);
            raw.push_back(row[x * 4 + 3]);
        }
    }
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t value : raw) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t length = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + length >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(length & 0xff));
        zlib.push_back(static_cast<std::uint8_t>(length >> 8));
        zlib.push_back(static_cast<std::uint8_t>(~length & 0xff));
        zlib.push_back(static_cast<std::uint8_t>((~length >> 8) & 0xff));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                    raw.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        if (last) {
            break;
        }
    }
    put32(zlib, (b << 16) | a);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<std::uint8_t> header;
    put32(header, width);
    put32(header, height);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", zlib);
    chunk(png, "IEND", {});
    return png;
}

bool PngWriter::write_bgra(const std::filesystem::path& path, const void* pixels,
                           std::uint32_t width, std::uint32_t height, std::uint32_t stride) {
    const std::vector<std::uint8_t> png = encode_bgra(pixels, width, height, stride);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
}

}  // namespace lsw::suites
