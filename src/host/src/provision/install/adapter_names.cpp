#include "lsw/host/provision/install/adapter_names.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <utility>

namespace lsw::host::provision::install {

namespace {

// The capset's name field less its terminator (accel-virt's
// GpuIdentity::name_bytes - 1).
constexpr qsizetype kNameBytes = 47;

// An AMD integrated GPU that AMD's table does not name. The PCI ID
// database is no help there: its bracket lists whatever a device ID was
// sold as ("Granite Ridge [Radeon 610M]" for a desktop Ryzen 9000's,
// whose GPU was never called that), and AMD's own Windows driver names
// these "AMD Radeon(TM) Graphics".
constexpr const char* kAmdIntegrated = "AMD Radeon(TM) Graphics";

// The vendor as its own Windows driver prefixes a product, and the word
// the PCI ID database may already start a product with ("Intel Graphics"),
// which the prefix replaces rather than repeats.
struct Vendor {
    const char* prefix;
    const char* word;
};

std::optional<Vendor> vendor_of(quint32 vendor) {
    switch (vendor) {
    case 0x1002:
        return Vendor{"AMD", "AMD"};
    case 0x8086:
        return Vendor{"Intel(R)", "Intel"};
    case 0x10de:
        return Vendor{"NVIDIA", "NVIDIA"};
    default:
        return std::nullopt;
    }
}

}  // namespace

AdapterNames::AdapterNames() = default;

AdapterNames::AdapterNames(Sources sources) : sources_(std::move(sources)) {}

QString AdapterNames::render(const QVector<platform::HostGpu>& gpus) const {
    QString out;
    for (const auto& gpu : gpus) {
        if (gpu.cpu) {
            continue;
        }
        const PciIdentity id = identity(gpu);
        out += QStringLiteral("%1\t%2\t%3\n")
                   .arg(id.vendor, 4, 16, QLatin1Char('0'))
                   .arg(id.device, 4, 16, QLatin1Char('0'))
                   .arg(name(gpu));
    }
    return out;
}

QString AdapterNames::name(const platform::HostGpu& gpu) const {
    const PciIdentity id = identity(gpu);
    platform::HostGpu named = gpu;
    if (auto amd = amdgpu_name(id)) {
        named.name = *amd;
    } else if (gpu.integrated && id.vendor == 0x1002) {
        named.name = QLatin1String(kAmdIntegrated);
    } else if (auto pci = gpu.integrated ? pci_ids_name(id) : std::nullopt) {
        named.name = *pci;
    }
    return fitted(platform::HostGpus::adapter_name(named));
}

AdapterNames::PciIdentity AdapterNames::identity(const platform::HostGpu& gpu) const {
    if (gpu.pci_address.isEmpty()) {
        return {};
    }
    const auto vendor = sysfs_number(gpu.pci_address, "vendor", 0xffff);
    const auto device = sysfs_number(gpu.pci_address, "device", 0xffff);
    const auto revision = sysfs_number(gpu.pci_address, "revision", 0xff);
    if (!vendor || !device || !revision) {
        return {};
    }
    return {*vendor, *device, static_cast<int>(*revision)};
}

std::optional<quint32> AdapterNames::sysfs_number(const QString& address, const char* attribute,
                                                  quint32 limit) const {
    QFile file{QDir(sources_.sysfs_pci).filePath(address + QLatin1Char('/') +
                                                 QLatin1String(attribute))};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray text = file.readAll().trimmed();
    if (!text.startsWith("0x") && !text.startsWith("0X")) {
        return std::nullopt;
    }
    bool ok = false;
    const quint64 value = text.mid(2).toULongLong(&ok, 16);
    if (!ok || value > limit) {
        return std::nullopt;
    }
    return static_cast<quint32>(value);
}

std::optional<QString> AdapterNames::amdgpu_name(const PciIdentity& id) const {
    if (id.vendor != 0x1002 || id.revision < 0) {
        return std::nullopt;
    }
    QFile file{sources_.amdgpu_ids};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    // "<device>,\t<revision>,\t<name>" a line, hex without 0x; comments
    // and the version line do not parse as two numbers.
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        const QStringList fields = line.split(QLatin1Char(','));
        if (fields.size() < 3) {
            continue;
        }
        bool device_ok = false;
        bool revision_ok = false;
        const uint device = fields.at(0).trimmed().toUInt(&device_ok, 16);
        const uint revision = fields.at(1).trimmed().toUInt(&revision_ok, 16);
        if (device_ok && revision_ok && device == id.device &&
            revision == static_cast<uint>(id.revision)) {
            // The name may itself hold commas: everything after the
            // second field.
            return fields.mid(2).join(QLatin1Char(',')).trimmed();
        }
    }
    return std::nullopt;
}

std::optional<QString> AdapterNames::pci_ids_name(const PciIdentity& id) const {
    const auto vendor = vendor_of(id.vendor);
    if (!vendor) {
        return std::nullopt;
    }
    const QString vendor_line = QStringLiteral("%1  ").arg(id.vendor, 4, 16, QLatin1Char('0'));
    const QString device_line = QStringLiteral("\t%1  ").arg(id.device, 4, 16, QLatin1Char('0'));
    // The product's bracketed marketing name where it has one: "Granite
    // Ridge [Radeon Graphics]" is "Radeon Graphics".
    static const QRegularExpression bracketed(QStringLiteral("\\[([^\\]]+)\\][^\\[]*$"));
    for (const QString& database : sources_.pci_ids) {
        QFile file{database};
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        bool in_vendor = false;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).remove(QLatin1Char('\n'));
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('\t')) &&
                !line.startsWith(QLatin1Char('#'))) {
                in_vendor = line.startsWith(vendor_line);
                continue;
            }
            if (in_vendor && line.startsWith(device_line)) {
                const QString product = line.mid(device_line.size());
                const QRegularExpressionMatch m = bracketed.match(product);
                QString picked = m.hasMatch() ? m.captured(1) : product;
                const QString word = QLatin1String(vendor->word) + QLatin1Char(' ');
                if (picked.startsWith(word)) {
                    picked.remove(0, word.size());
                }
                return QStringLiteral("%1 %2").arg(QLatin1String(vendor->prefix), picked);
            }
        }
    }
    return std::nullopt;
}

QString AdapterNames::fitted(const QString& name) {
    QByteArray bytes = name.toUtf8();
    if (bytes.size() <= kNameBytes) {
        return name;
    }
    qsizetype end = kNameBytes;
    // Back off a continuation byte (10xxxxxx) to the start of its
    // character, which is then dropped whole.
    while (end > 0 && (static_cast<unsigned char>(bytes.at(end)) & 0xc0U) == 0x80U) {
        --end;
    }
    bytes.truncate(end);
    return QString::fromUtf8(bytes);
}

}  // namespace lsw::host::provision::install
