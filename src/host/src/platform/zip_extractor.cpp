#include "lsw/host/platform/zip_extractor.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <zlib.h>

#include <cstdint>
#include <cstring>

namespace lsw::host::platform {

namespace {

constexpr std::uint32_t kEndSignature = 0x06054b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr qsizetype kEndSize = 22;
constexpr qsizetype kCentralSize = 46;
constexpr qsizetype kLocalSize = 30;
constexpr qsizetype kMaxComment = 65535;
constexpr std::uint16_t kStored = 0;
constexpr std::uint16_t kDeflated = 8;

std::uint32_t u16(const QByteArray& bytes, qsizetype at) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.constData()) + at;
    return static_cast<std::uint32_t>(p[0] | (p[1] << 8));
}

std::uint32_t u32(const QByteArray& bytes, qsizetype at) {
    return u16(bytes, at) | (u16(bytes, at + 2) << 16);
}

bool inflate_raw(const QByteArray& packed, QByteArray& out, std::uint32_t size) {
    out.resize(static_cast<qsizetype>(size));
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(packed.constData()));
    stream.avail_in = static_cast<uInt>(packed.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&stream, Z_FINISH);
    const bool whole = rc == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    return whole || (size == 0 && rc == Z_STREAM_END);
}

QString safe_name(QString name) {
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.startsWith(QLatin1Char('/'))) {
        return {};
    }
    for (const QString& part : name.split(QLatin1Char('/'))) {
        if (part == QLatin1String("..")) {
            return {};
        }
    }
    return name;
}

}

bool ZipExtractor::extract(const QString& zip_path, const QString& out_dir, QString& error) {
    QFile file{zip_path};
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot read %1: %2").arg(zip_path, file.errorString());
        return false;
    }
    const QByteArray zip = file.readAll();
    file.close();

    qsizetype end = -1;
    const qsizetype lowest = qMax<qsizetype>(0, zip.size() - kEndSize - kMaxComment);
    for (qsizetype at = zip.size() - kEndSize; at >= lowest; --at) {
        if (u32(zip, at) == kEndSignature) {
            end = at;
            break;
        }
    }
    if (end < 0) {
        error = QStringLiteral("%1 is not a zip file").arg(zip_path);
        return false;
    }
    const std::uint32_t count = u16(zip, end + 10);
    qsizetype at = static_cast<qsizetype>(u32(zip, end + 16));

    QDir{}.mkpath(out_dir);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + kCentralSize > zip.size() || u32(zip, at) != kCentralSignature) {
            error = QStringLiteral("the zip directory is damaged");
            return false;
        }
        const std::uint16_t method = static_cast<std::uint16_t>(u16(zip, at + 10));
        const std::uint32_t crc = u32(zip, at + 16);
        const std::uint32_t packed_size = u32(zip, at + 20);
        const std::uint32_t size = u32(zip, at + 24);
        const qsizetype name_len = u16(zip, at + 28);
        const qsizetype extra_len = u16(zip, at + 30);
        const qsizetype comment_len = u16(zip, at + 32);
        const qsizetype local = static_cast<qsizetype>(u32(zip, at + 42));
        const QString name = safe_name(QString::fromUtf8(zip.mid(at + kCentralSize, name_len)));
        at += kCentralSize + name_len + extra_len + comment_len;

        if (name.isEmpty()) {
            error = QStringLiteral("the zip holds a path outside its folder");
            return false;
        }
        if (name.endsWith(QLatin1Char('/'))) {
            QDir{}.mkpath(QDir{out_dir}.filePath(name));
            continue;
        }
        if (local + kLocalSize > zip.size() || u32(zip, local) != kLocalSignature) {
            error = QStringLiteral("%1 has no data in the zip").arg(name);
            return false;
        }
        const qsizetype data = local + kLocalSize + u16(zip, local + 26) + u16(zip, local + 28);
        if (data + static_cast<qsizetype>(packed_size) > zip.size()) {
            error = QStringLiteral("%1 is cut short in the zip").arg(name);
            return false;
        }
        const QByteArray packed = zip.mid(data, static_cast<qsizetype>(packed_size));
        QByteArray content;
        if (method == kStored) {
            content = packed;
        } else if (method != kDeflated || !inflate_raw(packed, content, size)) {
            error = QStringLiteral("%1 cannot be unpacked").arg(name);
            return false;
        }
        const auto sum = static_cast<std::uint32_t>(
            crc32(0, reinterpret_cast<const Bytef*>(content.constData()), static_cast<uInt>(content.size())));
        if (sum != crc) {
            error = QStringLiteral("%1 is damaged in the zip").arg(name);
            return false;
        }

        const QString target = QDir{out_dir}.filePath(name);
        QDir{}.mkpath(QFileInfo{target}.absolutePath());
        QFile out{target};
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(content) != content.size()) {
            error = QStringLiteral("cannot write %1: %2").arg(target, out.errorString());
            return false;
        }
    }
    return true;
}

}
