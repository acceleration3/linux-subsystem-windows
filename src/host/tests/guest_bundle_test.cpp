#include "lsw/host/services/guest_bundle.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cstdio>
#include <map>
#include <string>

using lsw::host::services::GuestBundle;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

std::string sha256(const std::string& text) {
    return QCryptographicHash::hash(QByteArray::fromStdString(text), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

class Tree {
public:
    Tree() {
        write("agent.exe", "agent");
        write("update.ps1", "update script");
        write("drivers/provision.ps1", "provision");
        write("drivers/package/avgpu.sys", "sys");
        write("drivers/package/avgpu.inf", "inf");
        write("drivers/loaders/x64/OpenCL.dll", "cl64");
        write("drivers/loaders/x86/OpenCL.dll", "cl32");
        write("mirror.exe", "mirror");
        write("vk64.dll", "vk64");
        write("vk32.dll", "vk32");
        write("virtiofs/lsw-shares.ps1", "shares script");
        write("virtiofs/winfsp.msi", "winfsp");
        point_all();
    }

    void write(const QString& relative, const QByteArray& bytes) {
        const QString path = dir_.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file{path};
        file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write(bytes);
    }

    void remove(const QString& relative) { QFile::remove(dir_.filePath(relative)); }
    void remove_dir(const QString& relative) { QDir{dir_.filePath(relative)}.removeRecursively(); }

    void point_all() {
        qputenv("LSW_AGENT_EXE", path("agent.exe"));
        qputenv("LSW_GUEST_UPDATE_SCRIPT", path("update.ps1"));
        qputenv("LSW_AV_DRIVERS", path("drivers"));
        qputenv("LSW_REMOTEWIN_GUEST", path("mirror.exe"));
        qputenv("LSW_VULKAN_LOADER", path("vk64.dll"));
        qputenv("LSW_VULKAN_LOADER_X86", path("vk32.dll"));
        qputenv("LSW_GUEST_VIRTIOFS", path("virtiofs"));
    }

    [[nodiscard]] QByteArray path(const QString& relative) const { return dir_.filePath(relative).toUtf8(); }

private:
    QTemporaryDir dir_;
};

using Contents = std::shared_ptr<const GuestBundle::Contents>;

Contents collect(const QString& names = QStringLiteral("10de\t2684\tRTX\t32.0.1.2\n"), const std::string& shares = "none") {
    return GuestBundle::collect(names, shares);
}

bool has(const Contents& c, const std::string& guest_path) {
    for (const auto& f : c->files) {
        if (f.guest_path == guest_path) {
            return true;
        }
    }
    return false;
}

}

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};
    Tree tree;

    const Contents base = collect();
    check(base->error.isEmpty(), "a complete tree makes a bundle");

    {
        static const QRegularExpression line{QStringLiteral("^[0-9a-f]{64}  [^\\n]+$")};
        const QString manifest = QString::fromStdString(base->manifest);
        const QStringList lines = manifest.split('\n');
        bool all_match = lines.size() >= 2;
        for (int i = 0; i + 1 < lines.size(); ++i) {
            all_match = all_match && line.match(lines[i]).hasMatch();
        }
        check(all_match, "manifest lines are exactly <64 lowercase hex>, two spaces, <guest path>");
        check(manifest.endsWith('\n') && lines.last().isEmpty(), "the manifest ends in a newline, with no blank line");
        check(!manifest.contains(QStringLiteral("\r")), "the manifest has no carriage return");
        check(lines.size() - 1 == base->files.size(), "one manifest line per file");

        bool sorted = true;
        for (int i = 1; i < base->files.size(); ++i) {
            sorted = sorted && base->files[i - 1].guest_path < base->files[i].guest_path;
        }
        check(sorted, "files are sorted by guest path, none twice");
        QStringList paths_in_manifest;
        for (int i = 0; i + 1 < lines.size(); ++i) {
            paths_in_manifest << lines[i].mid(66);
        }
        QStringList paths_of_files;
        for (const auto& f : base->files) {
            paths_of_files << QString::fromStdString(f.guest_path);
        }
        check(paths_in_manifest == paths_of_files, "the manifest lists the files in the files' order");
        bool hashes = true;
        for (const auto& f : base->files) {
            hashes = hashes && f.sha256 == sha256(f.bytes.toStdString())
                     && base->manifest.find(f.sha256 + "  " + f.guest_path + "\n") != std::string::npos;
        }
        check(hashes, "each line's hash is the SHA-256 of the file's bytes");
        check(base->release == sha256(base->manifest) && base->release.size() == 64,
              "the release is the SHA-256 hex of the manifest text");
    }

    check(has(base, "C:\\ProgramData\\LSW\\lsw-agent.exe") && has(base, "C:\\ProgramData\\LSW\\remotewin-guest.exe")
              && has(base, "C:\\ProgramData\\LSW\\lsw-guest-update.ps1") && has(base, "C:\\av\\provision.ps1")
              && has(base, "C:\\av\\package\\avgpu.sys") && has(base, "C:\\av\\package\\avgpu.inf")
              && has(base, "C:\\av\\loaders\\x64\\OpenCL.dll") && has(base, "C:\\av\\loaders\\x86\\OpenCL.dll")
              && has(base, "C:\\av\\loaders\\x64\\vulkan-1.dll") && has(base, "C:\\av\\loaders\\x86\\vulkan-1.dll")
              && has(base, "C:\\ProgramData\\LSW\\virtiofs\\lsw-shares.ps1")
              && has(base, "C:\\ProgramData\\LSW\\virtiofs\\winfsp.msi"),
          "every file is at its guest path");
    check(has(base, "C:\\av\\adapters.txt") && has(base, GuestBundle::kSharesGuestPath),
          "adapters.txt and the mount list are in the bundle");
    check(std::string{GuestBundle::kSharesGuestPath} == "C:\\ProgramData\\LSW\\update\\shares.txt",
          "the mount list's pseudo path is under the update directory");

    {
        const Contents again = collect();
        check(again->release == base->release && again->manifest == base->manifest,
              "the same inputs give the same release (built twice)");
    }
    {
        tree.write("drivers/package/avgpu.sys", "sYs");
        const Contents changed = collect();
        check(changed->release != base->release, "one changed byte in a driver file gives another release");
        tree.write("drivers/package/avgpu.sys", "sys");
        tree.write("agent.exe", "agenu");
        check(collect()->release != base->release, "one changed byte in the agent gives another release");
        tree.write("agent.exe", "agent");
        tree.write("virtiofs/winfsp.msi", "winfsq");
        check(collect()->release != base->release, "one changed byte in a shared-folder file gives another release");
        tree.write("virtiofs/winfsp.msi", "winfsp");
        check(collect()->release == base->release, "putting the bytes back puts the release back");
    }
    check(collect(QStringLiteral("10de\t2684\tRTX\t32.0.1.2\n"), "d=D:=SG9zdA==")->release != base->release,
          "a different mount list gives a different release");
    check(collect(QStringLiteral("10de\t2684\tRTX\t32.0.1.2\n"), "none")->release == base->release,
          "the same mount list gives the same");
    check(collect(QStringLiteral("10de\t2684\tRTX\t32.0.1.3\n"))->release != base->release,
          "a different driver version in adapters.txt gives a different release");
    {
        const QString ab = QStringLiteral("1002\t7550\tAMD\t1.2.3.4\n10de\t2684\tRTX\t32.0.1.2\n");
        const QString ba = QStringLiteral("10de\t2684\tRTX\t32.0.1.2\n1002\t7550\tAMD\t1.2.3.4\n");
        const Contents a = collect(ab);
        const Contents b = collect(ba);
        check(a->release == b->release, "the order of adapters.txt's lines from the host does not change the release");
        for (const auto& f : a->files) {
            if (f.guest_path == "C:\\av\\adapters.txt") {
                check(f.bytes == ab.toUtf8(), "adapters.txt is its lines sorted, one newline each");
            }
        }
    }
    check(!has(collect(QString{}), "C:\\av\\adapters.txt"), "no adapter names, no adapters.txt");

    check(!base->version.empty() && base->manifest.find(base->version) == std::string::npos
              && base->release == sha256(base->manifest),
          "the version is in the contents, and in neither the manifest nor the release");
    check(base->shares == "none", "the mount list is carried in the contents");

    {
        qputenv("LSW_AGENT_EXE", tree.path("nonexistent.exe"));
        const Contents c = collect();
        check(c->error.contains(QStringLiteral("lsw-guest-agent.exe")), "a missing agent is an error naming it");
        tree.point_all();
        qputenv("LSW_GUEST_UPDATE_SCRIPT", tree.path("nonexistent.ps1"));
        check(collect()->error.contains(QStringLiteral("lsw-guest-update.ps1")), "a missing update script is an error naming it");
        tree.point_all();
        qputenv("LSW_AV_DRIVERS", tree.path("nonexistent"));
        check(collect()->error.contains(QStringLiteral("driver package")), "a missing driver tree is an error");
        tree.point_all();
        tree.remove("drivers/provision.ps1");
        check(collect()->error.contains(QStringLiteral("provision.ps1")), "a driver tree without provision.ps1 is an error naming it");
        tree.write("drivers/provision.ps1", "provision");
        tree.remove_dir("drivers/package");
        check(collect()->error.contains(QStringLiteral("package/")), "a driver tree without package/ is an error naming it");
        tree.write("drivers/package/avgpu.sys", "sys");
        tree.write("drivers/package/avgpu.inf", "inf");
        check(collect()->error.isEmpty() && collect()->release == base->release, "restoring the tree restores the bundle");
    }

    {
        qputenv("LSW_REMOTEWIN_GUEST", tree.path("nonexistent.exe"));
        qputenv("LSW_VULKAN_LOADER", tree.path("nonexistent.dll"));
        qputenv("LSW_VULKAN_LOADER_X86", tree.path("nonexistent.dll"));
        qputenv("LSW_GUEST_VIRTIOFS", tree.path("nonexistent"));
        tree.remove_dir("drivers/loaders");
        const Contents c = collect();
        check(c->error.isEmpty(), "missing mirror, loaders and shared-folder files do not error");
        check(!has(c, "C:\\ProgramData\\LSW\\remotewin-guest.exe"), "a missing mirror is left out of the manifest");
        check(!has(c, "C:\\av\\loaders\\x64\\vulkan-1.dll") && !has(c, "C:\\av\\loaders\\x86\\vulkan-1.dll"),
              "missing Vulkan loaders are left out");
        bool no_virtiofs = true;
        for (const auto& f : c->files) {
            no_virtiofs = no_virtiofs && f.guest_path.find("\\virtiofs\\") == std::string::npos;
        }
        check(no_virtiofs, "missing shared-folder files are left out");
        check(has(c, "C:\\ProgramData\\LSW\\lsw-agent.exe") && has(c, "C:\\av\\package\\avgpu.sys"),
              "the required files are still there");
        check(c->release != base->release && c->release == sha256(c->manifest), "and the release is that of what is there");
        tree.point_all();
    }

    {
        tree.write("drivers/package/100%.dll", "x");
        const Contents pct = collect();
        check(!pct->error.isEmpty() && pct->error.contains(QStringLiteral("%")), "a guest path with % is refused, naming it");
        tree.remove("drivers/package/100%.dll");
        tree.write("drivers/package/a\"b.dll", "x");
        const Contents quote = collect();
        check(!quote->error.isEmpty() && quote->error.contains(QStringLiteral("\"")), "a guest path with a double quote is refused");
        tree.remove("drivers/package/a\"b.dll");
        check(collect()->error.isEmpty(), "and a tree without them is fine");
    }

    check(GuestBundle::staged_path("C:\\av\\a.sys") == "C\\av\\a.sys", "staged_path drops the drive colon");
    check(GuestBundle::staged_path("C:\\ProgramData\\LSW\\a:b.txt") == "C\\ProgramData\\LSW\\a:b.txt",
          "staged_path drops only the drive's colon");
    check(GuestBundle::staged_path("relative\\a.sys") == "relative\\a.sys" && GuestBundle::staged_path("") == ""
              && GuestBundle::staged_path("C") == "C",
          "staged_path leaves a path with no drive alone");

    {
        using Mount = GuestBundle::Mount;
        check(GuestBundle::shares_argument({}) == QStringLiteral("none"), "no mounts is 'none'");
        const QString arg = GuestBundle::shares_argument(
            {Mount{"d", "D:", "Documents"}, Mount{"e", "E:", "Müsik"}});
        check(arg == QStringLiteral("d=D:=%1,e=E:=%2")
                         .arg(QString::fromLatin1(QByteArray{"Documents"}.toBase64()),
                              QString::fromLatin1(QByteArray{"M\xc3\xbcsik"}.toBase64())),
              "a mount is tag=drive=base64(name) and mounts are comma-joined");
        lsw::host::vm::DomainDescription definition;
        definition.shares = {{"d", "/home/u/Documents", false},
                             {"xx", "/tmp/two", false},
                             {"1", "/tmp/digit", false},
                             {"z", "/", false}};
        const auto mounts = GuestBundle::mounts_of(definition);
        check(mounts.size() == 2 && mounts[0].tag == "d" && mounts[0].drive == "D:" && mounts[0].name == "Documents",
              "a share's tag is its drive letter, upper-cased with a colon, named by the folder");
        check(mounts.size() == 2 && mounts[1].drive == "Z:" && mounts[1].name == "Host",
              "the host's root is called Host");
        check(mounts.size() == 2, "a tag that is not a single letter is no share of LSW's");
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
