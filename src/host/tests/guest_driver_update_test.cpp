// A driver update's files and the staging that puts them in the guest:
// what is taken from the shipped tree, where each goes, and that a loader
// in the Windows directory is replaced only when it is setup's copy.

#include "lsw/host/manager/guest_driver_installer.hpp"
#include "lsw/host/manager/guest_driver_update.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using lsw::host::manager::GuestDriverInstaller;
using lsw::host::manager::GuestDriverUpdate;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

void touch(const QDir& root, const QString& path) {
    root.mkpath(QFileInfo(root.filePath(path)).path());
    QFile file{root.filePath(path)};
    (void)file.open(QIODevice::WriteOnly);
    file.write("x");
}

}  // namespace

int main() {
    QTemporaryDir dir;
    const QDir root{dir.path()};
    check(GuestDriverUpdate::files_of(root.path()).isEmpty(),
          "a tree without a package is nothing to update");

    touch(root, QStringLiteral("package/package.sha256"));
    touch(root, QStringLiteral("package/avgpu.sys"));
    touch(root, QStringLiteral("provision.ps1"));
    touch(root, QStringLiteral("loaders/x86/OpenCL.dll"));
    const auto files = GuestDriverUpdate::files_of(root.path());
    check(files.size() == 4, "the package, provision.ps1 and the loaders");
    check(files.size() == 4 && files[0].guest == QStringLiteral("C:\\av\\pristine\\package\\avgpu.sys") &&
              files[1].guest == QStringLiteral("C:\\av\\pristine\\package\\package.sha256") &&
              files[2].guest == QStringLiteral("C:\\av\\provision.ps1") &&
              files[3].guest == QStringLiteral("C:\\av\\loaders\\x86\\OpenCL.dll"),
          "each goes where setup put it");

    const QStringList uploaded = {
        QStringLiteral("C:\\ProgramData\\LSW\\uploads\\a\\avgpu.sys"),
        QStringLiteral("C:\\ProgramData\\LSW\\uploads\\b\\package.sha256"),
        QStringLiteral("C:\\ProgramData\\LSW\\uploads\\c\\provision.ps1"),
        QStringLiteral("C:\\ProgramData\\LSW\\uploads\\d\\OpenCL.dll"),
    };
    const QString staging = GuestDriverUpdate::staging(files, uploaded);
    check(staging.contains(QStringLiteral("rmdir /s /q C:\\av\\pristine\\package\r\n")),
          "the old package goes, so no file of it is left beside the new one");
    check(staging.contains(QStringLiteral(
              "copy /y \"C:\\ProgramData\\LSW\\uploads\\a\\avgpu.sys\" "
              "\"C:\\av\\pristine\\package\\avgpu.sys\" >nul || exit /b 4\r\n")),
          "an upload is copied to its place, and a copy that fails stops the update");
    check(staging.contains(QStringLiteral("rmdir /s /q \"C:\\ProgramData\\LSW\\uploads\\a\"\r\n")),
          "and its upload is cleaned up");
    const qsizetype compare = staging.indexOf(QStringLiteral(
        "fc /b \"C:\\Windows\\SysWOW64\\OpenCL.dll\" \"C:\\av\\loaders\\x86\\OpenCL.dll\""));
    const qsizetype replace = staging.indexOf(QStringLiteral(
        "copy /y \"C:\\ProgramData\\LSW\\uploads\\d\\OpenCL.dll\" \"C:\\av\\loaders\\x86\\OpenCL.dll\""));
    const qsizetype install = staging.indexOf(QStringLiteral(
        "if \"%OURS%\"==\"1\" copy /y \"C:\\av\\loaders\\x86\\OpenCL.dll\" "
        "\"C:\\Windows\\SysWOW64\\OpenCL.dll\""));
    check(compare >= 0 && replace > compare && install > replace,
          "a 32-bit loader in SysWOW64 is compared with setup's copy before that is replaced, "
          "and replaced only when it was setup's");

    const QString script = GuestDriverInstaller::script(staging);
    check(script.indexOf(staging) >= 0 &&
              script.indexOf(staging) < script.indexOf(QStringLiteral("-Stage sign")),
          "the installer re-signs and installs what the staging put in place");
    return g_failures == 0 ? 0 : 1;
}
