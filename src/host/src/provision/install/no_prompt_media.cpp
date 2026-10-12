#include "lsw/host/provision/install/no_prompt_media.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/platform/tool_process.hpp"
#include "lsw/host/provision/install/seven_zip.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtEndian>

#include <optional>

namespace lsw::host::provision::install {

namespace {

constexpr qint64 kSector = 2048;

// The Boot Record volume descriptor El Torito puts at sector 17, and
// the offset in it of the boot catalog's sector.
constexpr qint64 kBootRecordSector = 17;
constexpr int kCatalogPointer = 0x47;

// Catalog entries are 32 bytes. The first is the validation entry,
// which names the platform of the initial entry after it; the rest are
// section headers (0x90, or 0x91 for the last) each followed by its
// entries.
constexpr int kEntry = 32;
constexpr unsigned char kBootable = 0x88;
constexpr unsigned char kSectionMore = 0x90;
constexpr unsigned char kSectionLast = 0x91;
constexpr unsigned char kPlatformEfi = 0xEF;

constexpr auto kPromptImage = "efi/microsoft/boot/efisys.bin";
constexpr auto kNoPromptImage = "efi/microsoft/boot/efisys_noprompt.bin";

void set_error(QString* error_out, const QString& why) {
    if (error_out != nullptr) {
        *error_out = why;
    }
}

[[nodiscard]] QByteArray read_at(QFile& file, qint64 offset, qint64 length) {
    if (!file.seek(offset)) {
        return {};
    }
    return file.read(length);
}

// The sector the firmware loads the UEFI boot image from.
[[nodiscard]] std::optional<quint32> efi_image_sector(QFile& iso, QString* error_out) {
    const QByteArray record = read_at(iso, kBootRecordSector * kSector, kSector);
    if (record.size() != kSector || record[0] != 0 || record.mid(1, 5) != "CD001"
        || record.mid(7, 23) != "EL TORITO SPECIFICATION") {
        set_error(error_out, QStringLiteral("%1 is not bootable media: it has no El Torito "
                                            "boot record")
                                 .arg(iso.fileName()));
        return std::nullopt;
    }
    const auto catalog_sector = qFromLittleEndian<quint32>(record.constData() + kCatalogPointer);
    const QByteArray catalog = read_at(iso, catalog_sector * kSector, kSector);
    if (catalog.size() != kSector || catalog[0] != 1) {
        set_error(error_out, QStringLiteral("%1 has no valid El Torito boot catalog")
                                 .arg(iso.fileName()));
        return std::nullopt;
    }
    const auto rba = [&catalog](int entry) {
        return qFromLittleEndian<quint32>(catalog.constData() + entry + 8);
    };

    if (static_cast<unsigned char>(catalog[1]) == kPlatformEfi
        && static_cast<unsigned char>(catalog[kEntry]) == kBootable) {
        return rba(kEntry);
    }
    for (int at = 2 * kEntry; at + kEntry <= kSector;) {
        const auto header = static_cast<unsigned char>(catalog[at]);
        if (header != kSectionMore && header != kSectionLast) {
            break;
        }
        const bool efi = static_cast<unsigned char>(catalog[at + 1]) == kPlatformEfi;
        const int entries = qFromLittleEndian<quint16>(catalog.constData() + at + 2);
        at += kEntry;
        for (int i = 0; i < entries && at + kEntry <= kSector; ++i, at += kEntry) {
            if (efi && static_cast<unsigned char>(catalog[at]) == kBootable) {
                return rba(at);
            }
        }
        if (header == kSectionLast) {
            break;
        }
    }
    set_error(error_out, QStringLiteral("%1 has no UEFI boot image in its boot catalog")
                             .arg(iso.fileName()));
    return std::nullopt;
}

[[nodiscard]] QByteArray read_file(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

}  // namespace

bool NoPromptMedia::make(const QString& source_iso, const QString& overlay_path,
                         QString* error_out) {
    QFile iso{source_iso};
    if (!iso.open(QIODevice::ReadOnly)) {
        set_error(error_out, QStringLiteral("could not open %1").arg(source_iso));
        return false;
    }
    const auto sector = efi_image_sector(iso, error_out);
    if (!sector) {
        return false;
    }

    // qemu-io takes the file to write as a word of its own command line,
    // which it splits at spaces with no quoting; the temporary directory
    // is the one path here that is ours to choose.
    QTemporaryDir scratch{QDir(QDir::tempPath()).filePath(QStringLiteral("lsw-boot-XXXXXX"))};
    if (!scratch.isValid() || scratch.path().contains(QRegularExpression(QStringLiteral("\\s")))) {
        set_error(error_out, QStringLiteral("no temporary directory without spaces in its path "
                                            "to stage the boot image in (set TMPDIR)"));
        return false;
    }
    // By the names the medium itself gives them: the case is the
    // medium's, and 7-Zip extracts nothing, successfully, for a name
    // that is not there.
    const QStringList entries = seven_zip_entries(source_iso, error_out);
    const auto entry = [&entries](const char* wanted) {
        for (const QString& name : entries) {
            if (name.compare(QLatin1String(wanted), Qt::CaseInsensitive) == 0) {
                return name;
            }
        }
        return QString{};
    };
    const QString prompt_entry = entry(kPromptImage);
    const QString no_prompt_entry = entry(kNoPromptImage);
    if (prompt_entry.isEmpty() || no_prompt_entry.isEmpty()) {
        set_error(error_out, QStringLiteral("%1 has no %2, the boot image without the \"press "
                                            "any key\" prompt, or no %3 beside it")
                                 .arg(source_iso, QLatin1String(kNoPromptImage),
                                      QLatin1String(kPromptImage)));
        return false;
    }
    if (!run_seven_zip({QStringLiteral("e"), QStringLiteral("-y"), QStringLiteral("-bso0"),
                        QStringLiteral("-bsp0"), source_iso, prompt_entry, no_prompt_entry,
                        QStringLiteral("-o%1").arg(scratch.path())},
                       120'000, error_out)) {
        return false;
    }
    const QString no_prompt_path = scratch.filePath(QFileInfo(no_prompt_entry).fileName());
    const QByteArray prompt = read_file(scratch.filePath(QFileInfo(prompt_entry).fileName()));
    const QByteArray no_prompt = read_file(no_prompt_path);
    if (prompt.isEmpty() || no_prompt.isEmpty()) {
        set_error(error_out, QStringLiteral("the UEFI boot images did not come out of %1")
                                 .arg(source_iso));
        return false;
    }

    // Only ever over the image it replaces, and at its own size: the
    // catalog's sector count stays what it was, so a no-prompt image
    // larger than the prompt one would be loaded cut short.
    const qint64 offset = static_cast<qint64>(*sector) * kSector;
    const QByteArray booted = read_at(iso, offset, no_prompt.size());
    if (booted != prompt && booted != no_prompt) {
        set_error(error_out, QStringLiteral("the UEFI boot image of %1 is neither %2 nor %3; "
                                            "not replacing what is not known")
                                 .arg(source_iso, QLatin1String(kPromptImage),
                                      QLatin1String(kNoPromptImage)));
        return false;
    }
    if (prompt.size() != no_prompt.size()) {
        set_error(error_out, QStringLiteral("the two UEFI boot images of %1 differ in size")
                                 .arg(source_iso));
        return false;
    }

    // The old overlay belongs to the old machine, which is gone by now.
    QFile::remove(overlay_path);
    if (!platform::run_tool(QStringLiteral("qemu-img"),
                            {QStringLiteral("create"), QStringLiteral("-q"), QStringLiteral("-f"),
                             QStringLiteral("qcow2"), QStringLiteral("-F"), QStringLiteral("raw"),
                             QStringLiteral("-b"), QFileInfo(source_iso).absoluteFilePath(),
                             overlay_path},
                            30'000, nullptr, error_out)) {
        return false;
    }
    if (!platform::run_tool(QStringLiteral("qemu-io"),
                            {QStringLiteral("-f"), QStringLiteral("qcow2"), QStringLiteral("-c"),
                             QStringLiteral("write -s %1 %2 %3")
                                 .arg(no_prompt_path)
                                 .arg(offset)
                                 .arg(no_prompt.size()),
                             overlay_path},
                            30'000, nullptr, error_out)) {
        QFile::remove(overlay_path);
        return false;
    }
    LSW_INFO("install media: {} boots {} without a prompt (UEFI image at sector {})",
             overlay_path.toStdString(), source_iso.toStdString(), *sector);
    return true;
}

}  // namespace lsw::host::provision::install
