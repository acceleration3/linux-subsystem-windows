#include "lsw/host/vm/domain_description.hpp"

#include <QFile>
#include <QXmlStreamReader>

namespace lsw::host::vm {

namespace {

constexpr QLatin1StringView kGpuDriver{"virtio-gpu-av-pci"};

qint64 in_mib(qint64 value, const QString& unit) {
    const QString u = unit.trimmed();
    constexpr qint64 kKi = 1024;
    constexpr qint64 kKb = 1000;
    if (u.isEmpty() || u == QLatin1StringView("KiB") || u == QLatin1StringView("k")) {
        return value / kKi;
    }
    if (u == QLatin1StringView("b") || u == QLatin1StringView("bytes")) {
        return value / (kKi * kKi);
    }
    if (u == QLatin1StringView("KB")) {
        return value * kKb / (kKi * kKi);
    }
    if (u == QLatin1StringView("MiB") || u == QLatin1StringView("M")) {
        return value;
    }
    if (u == QLatin1StringView("MB")) {
        return value * kKb * kKb / (kKi * kKi);
    }
    if (u == QLatin1StringView("GiB") || u == QLatin1StringView("G")) {
        return value * kKi;
    }
    if (u == QLatin1StringView("GB")) {
        return value * kKb * kKb * kKb / (kKi * kKi);
    }
    if (u == QLatin1StringView("TiB") || u == QLatin1StringView("T")) {
        return value * kKi * kKi;
    }
    return 0;
}

DomainDescription::GpuDevice gpu_from(const QStringList& options) {
    DomainDescription::GpuDevice gpu;
    gpu.host_gpu = QStringLiteral("auto");
    for (const QString& option : options.mid(1)) {
        const qsizetype eq = option.indexOf(QLatin1Char('='));
        const QString key = eq < 0 ? option : option.left(eq);
        const QString value = eq < 0 ? QString{} : option.mid(eq + 1);
        if (key == QLatin1StringView("id")) {
            gpu.id = value;
        } else if (key == QLatin1StringView("host-gpu")) {
            gpu.host_gpu = value;
        } else if (key == QLatin1StringView("display")) {
            gpu.display = !(value == QLatin1StringView("off") || value == QLatin1StringView("false")
                            || value == QLatin1StringView("no"));
        } else if (key == QLatin1StringView("hostmem")) {
            gpu.hostmem_mib = DomainDescription::size_mib(value);
        } else if (key == QLatin1StringView("vram-reserve")) {
            gpu.vram_reserve_mib = qMax(0, value.toInt());
        } else if (key == QLatin1StringView("modes")) {
            gpu.modes = value;
        } else if (key == QLatin1StringView("refresh")) {
            bool ok = false;
            const quint32 millihz = value.toUInt(&ok);
            if (ok) {
                gpu.refresh_millihz = millihz;
            }
        }
    }
    return gpu;
}

}

const DomainDescription::GpuDevice* DomainDescription::display() const noexcept {
    for (const auto& gpu : gpus) {
        if (gpu.display) {
            return &gpu;
        }
    }
    return nullptr;
}

QStringList DomainDescription::split_options(const QString& line) {
    QStringList out;
    QString current;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c != QLatin1Char(',')) {
            current += c;
            continue;
        }
        if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char(',')) {
            current += c;
            ++i;
            continue;
        }
        out << current;
        current.clear();
    }
    out << current;
    return out;
}

int DomainDescription::size_mib(const QString& value) {
    const QString v = value.trimmed();
    if (v.isEmpty()) {
        return 0;
    }
    const QChar suffix = v.back().toUpper();
    const bool has_suffix = suffix.isLetter();
    bool ok = false;
    const qint64 n = (has_suffix ? v.chopped(1) : v).toLongLong(&ok);
    if (!ok || n < 0) {
        return 0;
    }
    if (!has_suffix) {
        return static_cast<int>(n / (1024 * 1024));
    }
    switch (suffix.unicode()) {
    case 'K':
        return static_cast<int>(n / 1024);
    case 'M':
        return static_cast<int>(n);
    case 'G':
        return static_cast<int>(n * 1024);
    case 'T':
        return static_cast<int>(n * 1024 * 1024);
    default:
        return 0;
    }
}

std::optional<DomainDescription> DomainDescription::parse(const QString& xml) {
    QXmlStreamReader reader(xml);
    DomainDescription out;
    bool saw_domain = false;
    bool in_devices = false;
    bool in_disk = false;
    bool in_commandline = false;
    bool in_video = false;
    bool disk_done = false;
    bool in_filesystem = false;
    bool filesystem_virtiofs = false;
    Share share;
    bool in_usb_hostdev = false;
    bool in_hostdev_source = false;
    UsbDevice usb;
    QString previous_arg;

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            const auto name = reader.name();
            if (name == QLatin1StringView("devices")) {
                in_devices = false;
            } else if (name == QLatin1StringView("disk") && in_disk) {
                in_disk = false;
                disk_done = true;
            } else if (name == QLatin1StringView("commandline")) {
                in_commandline = false;
            } else if (name == QLatin1StringView("video")) {
                in_video = false;
            } else if (name == QLatin1StringView("source") && in_usb_hostdev) {
                in_hostdev_source = false;
            } else if (name == QLatin1StringView("hostdev") && in_usb_hostdev) {
                in_usb_hostdev = false;
                out.usb_devices.push_back(usb);
            } else if (name == QLatin1StringView("filesystem") && in_filesystem) {
                in_filesystem = false;
                if (filesystem_virtiofs && !share.tag.isEmpty()) {
                    out.shares.push_back(share);
                }
            }
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto name = reader.name();
        const auto attrs = reader.attributes();
        if (name == QLatin1StringView("domain") && !saw_domain) {
            saw_domain = true;
        } else if (!saw_domain) {
            return std::nullopt;
        } else if (name == QLatin1StringView("vcpu") && !in_devices) {
            out.vcpus = reader.readElementText().trimmed().toInt();
        } else if (name == QLatin1StringView("memory") && !in_devices) {
            const QString unit = attrs.value(QLatin1StringView("unit")).toString();
            out.memory_mib = in_mib(reader.readElementText().trimmed().toLongLong(), unit);
        } else if (name == QLatin1StringView("devices")) {
            in_devices = true;
        } else if (in_devices && name == QLatin1StringView("disk") && !disk_done
                   && attrs.value(QLatin1StringView("device")) == QLatin1StringView("disk")) {
            in_disk = true;
        } else if (in_disk && name == QLatin1StringView("source")) {
            out.disk_path = attrs.value(QLatin1StringView("file")).toString();
        } else if (in_disk && name == QLatin1StringView("target")) {
            out.disk_bus = attrs.value(QLatin1StringView("bus")).toString();
        } else if (in_devices && name == QLatin1StringView("hostdev")
                   && attrs.value(QLatin1StringView("type")) == QLatin1StringView("usb")) {
            in_usb_hostdev = true;
            usb = {};
        } else if (in_usb_hostdev && name == QLatin1StringView("source")) {
            in_hostdev_source = true;
        } else if (in_hostdev_source && name == QLatin1StringView("vendor")) {
            usb.vendor = attrs.value(QLatin1StringView("id")).toString().toInt(nullptr, 0);
        } else if (in_hostdev_source && name == QLatin1StringView("product")) {
            usb.product = attrs.value(QLatin1StringView("id")).toString().toInt(nullptr, 0);
        } else if (in_hostdev_source && name == QLatin1StringView("address")) {
            usb.bus = attrs.value(QLatin1StringView("bus")).toString().toInt();
            usb.device = attrs.value(QLatin1StringView("device")).toString().toInt();
        } else if (in_devices && name == QLatin1StringView("filesystem")) {
            in_filesystem = true;
            filesystem_virtiofs = false;
            share = {};
        } else if (in_filesystem && name == QLatin1StringView("driver")) {
            filesystem_virtiofs = attrs.value(QLatin1StringView("type")) == QLatin1StringView("virtiofs");
        } else if (in_filesystem && name == QLatin1StringView("source")) {
            share.host_path = attrs.value(QLatin1StringView("dir")).toString();
        } else if (in_filesystem && name == QLatin1StringView("target")) {
            share.tag = attrs.value(QLatin1StringView("dir")).toString();
        } else if (in_filesystem && name == QLatin1StringView("alias")) {
            const QString alias = attrs.value(QLatin1StringView("name")).toString();
            if (alias.startsWith(QLatin1String(DomainShare::kAliasPrefix))) {
                share.drive = alias.mid(qstrlen(DomainShare::kAliasPrefix));
            }
        } else if (in_filesystem && name == QLatin1StringView("readonly")) {
            share.read_only = true;
        } else if (in_devices && name == QLatin1StringView("video")) {
            in_video = true;
        } else if (in_video && name == QLatin1StringView("model")) {
            if (attrs.value(QLatin1StringView("type")) == QLatin1StringView("vga")) {
                out.installing = true;
            }
        } else if (name == QLatin1StringView("commandline")) {
            in_commandline = true;
        } else if (in_commandline && name == QLatin1StringView("arg")) {
            const QString value = attrs.value(QLatin1StringView("value")).toString();
            if (previous_arg == QLatin1StringView("-device")) {
                const QStringList options = split_options(value);
                if (!options.isEmpty() && options.first() == kGpuDriver) {
                    out.gpus.push_back(gpu_from(options));
                }
            }
            previous_arg = value;
        }
    }
    if (reader.hasError() || !saw_domain) {
        return std::nullopt;
    }
    return out;
}

std::optional<quint64> DiskImage::qcow2_virtual_size(const QByteArray& header) {
    constexpr qsizetype kSizeAt = 24;
    if (header.size() < kSizeAt + 8 || !header.startsWith(QByteArrayLiteral("QFI\xfb"))) {
        return std::nullopt;
    }
    quint64 size = 0;
    for (qsizetype i = 0; i < 8; ++i) {
        size = (size << 8) | static_cast<quint8>(header.at(kSizeAt + i));
    }
    return size;
}

std::optional<quint64> DiskImage::virtual_size(const QString& path) {
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    if (const auto size = qcow2_virtual_size(file.read(32))) {
        return size;
    }
    return static_cast<quint64>(file.size());
}

}
