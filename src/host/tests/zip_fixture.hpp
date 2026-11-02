#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <zlib.h>

#include <cstdint>
#include <utility>

class ZipFixture {
public:
    ZipFixture() = delete;

    [[nodiscard]] static QByteArray build(const QVector<std::pair<QString, QByteArray>>& entries,
                                          bool compress = true) {
        QByteArray out;
        QByteArray directory;
        for (const auto& [name, content] : entries) {
            const QByteArray name_bytes = name.toUtf8();
            QByteArray packed = content;
            std::uint16_t method = 0;
            if (compress) {
                z_stream stream{};
                deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                             Z_DEFAULT_STRATEGY);
                packed.resize(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(content.size()))));
                stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(content.constData()));
                stream.avail_in = static_cast<uInt>(content.size());
                stream.next_out = reinterpret_cast<Bytef*>(packed.data());
                stream.avail_out = static_cast<uInt>(packed.size());
                deflate(&stream, Z_FINISH);
                packed.resize(static_cast<qsizetype>(stream.total_out));
                deflateEnd(&stream);
                method = 8;
            }
            const auto crc = static_cast<std::uint32_t>(
                crc32(0, reinterpret_cast<const Bytef*>(content.constData()), static_cast<uInt>(content.size())));
            const std::uint32_t offset = static_cast<std::uint32_t>(out.size());

            put32(out, 0x04034b50);
            put16(out, 20);
            put16(out, 0);
            put16(out, method);
            put32(out, 0);
            put32(out, crc);
            put32(out, static_cast<std::uint32_t>(packed.size()));
            put32(out, static_cast<std::uint32_t>(content.size()));
            put16(out, static_cast<std::uint16_t>(name_bytes.size()));
            put16(out, 0);
            out += name_bytes;
            out += packed;

            put32(directory, 0x02014b50);
            put16(directory, 20);
            put16(directory, 20);
            put16(directory, 0);
            put16(directory, method);
            put32(directory, 0);
            put32(directory, crc);
            put32(directory, static_cast<std::uint32_t>(packed.size()));
            put32(directory, static_cast<std::uint32_t>(content.size()));
            put16(directory, static_cast<std::uint16_t>(name_bytes.size()));
            put16(directory, 0);
            put16(directory, 0);
            put16(directory, 0);
            put16(directory, 0);
            put32(directory, 0);
            put32(directory, offset);
            directory += name_bytes;
        }
        const std::uint32_t directory_at = static_cast<std::uint32_t>(out.size());
        out += directory;
        put32(out, 0x06054b50);
        put16(out, 0);
        put16(out, 0);
        put16(out, static_cast<std::uint16_t>(entries.size()));
        put16(out, static_cast<std::uint16_t>(entries.size()));
        put32(out, static_cast<std::uint32_t>(directory.size()));
        put32(out, directory_at);
        put16(out, 0);
        return out;
    }

private:
    static void put16(QByteArray& out, std::uint16_t v) {
        out.append(static_cast<char>(v & 0xff));
        out.append(static_cast<char>(v >> 8));
    }
    static void put32(QByteArray& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v & 0xffff));
        put16(out, static_cast<std::uint16_t>(v >> 16));
    }
};
