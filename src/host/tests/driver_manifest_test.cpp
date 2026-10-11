// A driver package's package.sha256, as the manager compares the one it
// ships with the guest's: which files changed, and what is not a manifest.

#include "lsw/host/services/driver_manifest.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using lsw::host::services::DriverManifest;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

const QString kA = QStringLiteral("a").repeated(64);
const QString kB = QStringLiteral("b").repeated(64);
const QString kC = QStringLiteral("C").repeated(64);

}  // namespace

int main() {
    const DriverManifest shipped{QStringLiteral("%1  avgpu.sys\n%2  avgpu.inf\n%1  avumd.dll\n")
                                     .arg(kA, kB)};
    const DriverManifest same{QStringLiteral("%2  avgpu.inf\r\n%1 *AVGPU.SYS\r\n%1  avumd.dll\r\n")
                                  .arg(kA, kB)};
    check(shipped.differences(same).isEmpty(),
          "line endings, order, binary mode and the case of names are no difference");

    const DriverManifest guest{QStringLiteral("%1  avgpu.sys\n%2  avgpu.inf\n%1  avcl.dll\n")
                                   .arg(kA, kC)};
    check(shipped.differences(guest) ==
              QStringList({QStringLiteral("avcl.dll"), QStringLiteral("avgpu.inf"),
                           QStringLiteral("avumd.dll")}),
          "a changed hash, a file only one side has, either side: each named once, sorted");
    check(shipped.differences(guest) == guest.differences(shipped), "the comparison is symmetric");

    const DriverManifest junk{QStringLiteral("not a manifest\n%1\nabc  short.dll\n").arg(kA)};
    check(junk.empty(), "lines that are not a hash and a name are skipped");
    check(DriverManifest{}.differences(shipped).size() == 3,
          "against an empty manifest, every file is a difference");

    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("package.sha256"));
    QFile file{path};
    check(file.open(QIODevice::WriteOnly) &&
              file.write(QStringLiteral("%1  avgpu.sys\n").arg(kA).toUtf8()) > 0,
          "a manifest file is written");
    file.close();
    check(DriverManifest::read(path).differences(DriverManifest{
              QStringLiteral("%1  avgpu.sys").arg(kA)}).isEmpty(),
          "read gives the file's manifest");
    check(DriverManifest::read(QDir(dir.path()).filePath(QStringLiteral("missing"))).empty(),
          "a file that is not there reads as empty");

    QTemporaryDir tree;
    const QString script = QDir(tree.path()).filePath(QStringLiteral("provision.ps1"));
    QFile out{script};
    check(out.open(QIODevice::WriteOnly) && out.write("abc") == 3, "a file to add is written");
    out.close();
    DriverManifest shipped_tree{QStringLiteral("%1  avgpu.sys\n").arg(kA)};
    shipped_tree.add(QStringLiteral("provision.ps1"), script);
    shipped_tree.add(QStringLiteral("loaders/x64/vulkan-1.dll"),
                     QDir(tree.path()).filePath(QStringLiteral("missing.dll")));
    // sha256("abc"), as the agent reports the guest's copy.
    const QString abc =
        QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check(shipped_tree
              .differences(DriverManifest{
                  QStringLiteral("%1  avgpu.sys\n%2  PROVISION.PS1\n").arg(kA, abc)})
              .isEmpty(),
          "a file added is named and hashed as the guest's is, and one not there is not added");
    return g_failures == 0 ? 0 : 1;
}
