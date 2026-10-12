// The overlay a Windows ISO boots through without "press any key": the
// UEFI boot image the catalog names is the no-prompt one in it, and
// the ISO itself is not written. Over a small ISO laid out the way
// Windows media is (a BIOS entry, then a UEFI section), with 7-Zip and
// qemu-img doing what they do for the real thing.

#include "lsw/host/platform/tool_process.hpp"
#include "lsw/host/provision/install/no_prompt_media.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#define LIBISOFS_WITHOUT_LIBBURN 1
#include <libisofs/libisofs.h>

#include <cstdio>
#include <cstdlib>

using lsw::host::provision::install::NoPromptMedia;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

constexpr int kSector = 2048;
constexpr qint64 kImage = 4 * kSector;

bool put(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file{path};
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray read(const QString& path) {
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Windows media's boot layout over ``tree``: etfsboot.com for BIOS in
// the initial entry, ``uefi_image`` in a section of its own.
bool write_iso(const QString& tree, const char* uefi_image, const QString& out_path) {
    IsoImage* image = nullptr;
    if (iso_init() < 0 || iso_image_new("TEST", &image) < 0) {
        return false;
    }
    ElToritoBootImage* bios = nullptr;
    ElToritoBootImage* uefi = nullptr;
    IsoWriteOpts* opts = nullptr;
    burn_source* src = nullptr;
    const bool built =
        iso_tree_add_dir_rec(image, iso_image_get_root(image), tree.toUtf8().constData()) >= 0
        && iso_image_set_boot_image(image, "boot/etfsboot.com", ELTORITO_NO_EMUL, "boot/boot.cat",
                                    &bios) >= 0
        && iso_image_add_boot_image(image, uefi_image, ELTORITO_NO_EMUL, 0, &uefi) >= 0
        && iso_write_opts_new(&opts, 2) >= 0;
    if (built) {
        el_torito_set_boot_platform_id(uefi, 0xEF);
        iso_write_opts_set_joliet(opts, 1);
    }
    if (!built || iso_image_create_burn_source(image, opts, &src) < 0) {
        iso_image_unref(image);
        return false;
    }
    iso_write_opts_free(opts);
    QFile out{out_path};
    bool ok = out.open(QIODevice::WriteOnly);
    char buf[kSector];
    for (int got = 0; ok && (got = src->read_xt(src, reinterpret_cast<unsigned char*>(buf), kSector)) > 0;) {
        ok = out.write(buf, got) == got;
    }
    src->free_data(src);
    std::free(src);
    iso_image_unref(image);
    return ok;
}

QByteArray sha(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

}  // namespace

int main() {
    QTemporaryDir dir;
    const QString tree = dir.filePath(QStringLiteral("tree"));
    const QByteArray prompt(kImage, 'P');
    const QByteArray no_prompt(kImage, 'N');
    put(tree + QStringLiteral("/boot/etfsboot.com"), QByteArray(kSector, 'B'));
    put(tree + QStringLiteral("/efi/microsoft/boot/efisys.bin"), prompt);
    put(tree + QStringLiteral("/efi/microsoft/boot/efisys_noprompt.bin"), no_prompt);
    put(tree + QStringLiteral("/sources/boot.wim"), QByteArray(kSector, 'W'));

    const QString iso = dir.filePath(QStringLiteral("windows.iso"));
    const QString overlay = dir.filePath(QStringLiteral("install.qcow2"));
    const QString flat = dir.filePath(QStringLiteral("flat.raw"));
    check(write_iso(tree, "efi/microsoft/boot/efisys.bin", iso), "the fixture ISO is written");
    const QByteArray before = read(iso);
    const qint64 at = before.indexOf(prompt);
    check(at > 0 && at % kSector == 0, "the prompt image is on the ISO, on a sector");

    QString why;
    check(NoPromptMedia::make(iso, overlay, &why), "the overlay is made");
    if (!why.isEmpty()) {
        std::printf("     %s\n", qPrintable(why));
    }
    QString convert_error;
    check(lsw::host::platform::run_tool(
              QStringLiteral("qemu-img"),
              {QStringLiteral("convert"), QStringLiteral("-O"), QStringLiteral("raw"), overlay, flat},
              30'000, nullptr, &convert_error),
          "the overlay reads back");
    const QByteArray after = read(flat);
    check(after.size() == before.size(), "the overlay is the ISO's size");
    check(after.mid(at, kImage) == no_prompt, "the sectors the catalog boots hold the no-prompt image");
    check(after.left(at) == before.left(at) && after.mid(at + kImage) == before.mid(at + kImage),
          "nothing else differs");
    check(sha(read(iso)) == sha(before), "the ISO itself is not written");

    // Media that already boots the no-prompt image: made all the same,
    // since the domain always boots the overlay, and nothing changes.
    const QString rebuilt = dir.filePath(QStringLiteral("rebuilt.iso"));
    check(write_iso(tree, "efi/microsoft/boot/efisys_noprompt.bin", rebuilt) &&
              NoPromptMedia::make(rebuilt, overlay, &why),
          "media already without the prompt still gets its overlay");

    // Media without the no-prompt image is refused, with a reason.
    QFile::remove(tree + QStringLiteral("/efi/microsoft/boot/efisys_noprompt.bin"));
    const QString older = dir.filePath(QStringLiteral("older.iso"));
    why.clear();
    check(write_iso(tree, "efi/microsoft/boot/efisys.bin", older)
              && !NoPromptMedia::make(older, overlay, &why) && why.contains(QStringLiteral("efisys_noprompt")),
          "media without efisys_noprompt.bin is refused and says so");
    return g_failures == 0 ? 0 : 1;
}
