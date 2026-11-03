#include "lsw/host/services/guest_bundle.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/platform/accel_virt_paths.hpp"
#include "lsw/host/platform/agent_exe_path.hpp"
#include "lsw/host/platform/guest_update_script.hpp"
#include "lsw/host/platform/guest_virtiofs_path.hpp"
#include "lsw/host/platform/host_gpus.hpp"
#include "lsw/host/platform/mirror_exe_paths.hpp"
#include "lsw/host/services/adapter_names.hpp"
#include "lsw/host/services/driver_catalog.hpp"
#include "lsw/host/services/guest_swap_script.hpp"
#include "lsw/host/services/host_share_layout.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include <map>
#include <utility>

namespace lsw::host::services {

namespace {
constexpr const char* kGuestUpdateScript = "C:\\ProgramData\\LSW\\lsw-guest-update.ps1";
constexpr const char* kGuestVirtiofsDir = "C:\\ProgramData\\LSW\\virtiofs\\";
constexpr const char* kGuestAv = "C:\\av\\";

class Gathering {
public:
    bool add_file(const std::string& guest_path, const QString& host_path, QString* error) {
        if (guest_path.find_first_of("%\"") != std::string::npos) {
            *error = QStringLiteral("%1 cannot be a path in Windows").arg(QString::fromStdString(guest_path));
            return false;
        }
        QFile file{host_path};
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("cannot read %1: %2").arg(host_path, file.errorString());
            return false;
        }
        files_[guest_path] = file.readAll();
        return true;
    }

    void add_bytes(const std::string& guest_path, QByteArray bytes) {
        files_[guest_path] = std::move(bytes);
    }

    bool add_dir(const std::string& guest_dir, const QString& dir, QString* error) {
        const QDir d{dir};
        for (const QString& name : d.entryList(QDir::Files, QDir::Name)) {
            if (!add_file(guest_dir + name.toStdString(), d.filePath(name), error)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::map<std::string, QByteArray>& files() { return files_; }

private:
    std::map<std::string, QByteArray> files_;
};

[[nodiscard]] std::string sha256_hex(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

}

GuestBundle::GuestBundle(config::Settings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

namespace {
void add_mounts(QVector<GuestBundle::Mount>& mounts, const QVector<vm::DomainShare>& shares) {
    for (const auto& share : shares) {
        if (share.drive.size() != 1 || !share.drive.front().isLetter()) {
            LSW_WARN("shares: '{}' ({}) has no drive letter; Windows does not mount it",
                     share.tag.toStdString(), share.host_path.toStdString());
            continue;
        }
        mounts.push_back({share.tag, share.drive.toUpper() + QLatin1Char(':'), share.tag});
    }
}

}

QVector<GuestBundle::Mount> GuestBundle::mounts_of(const vm::DomainDescription& definition) {
    QVector<Mount> mounts;
    add_mounts(mounts, definition.shares);
    return mounts;
}

QVector<GuestBundle::Mount> GuestBundle::mounts_of(const config::ManagerSettings& settings) {
    QVector<Mount> mounts;
    add_mounts(mounts, HostShareLayout::assign(settings.host_shares()));
    return mounts;
}

std::string GuestBundle::staged_path(const std::string& guest_path) {
    std::string out = guest_path;
    if (out.size() > 1 && out[1] == ':') {
        out.erase(1, 1);
    }
    return out;
}

QString GuestBundle::shares_argument(const QVector<Mount>& mounts) {
    QStringList items;
    for (const auto& mount : mounts) {
        items << mount.drive.left(1) + QLatin1Char(':') + QString::fromLatin1(mount.tag.toUtf8().toBase64())
                     + QLatin1Char(':') + QString::fromLatin1(mount.name.toUtf8().toBase64());
    }
    return items.isEmpty() ? QStringLiteral("none") : items.join(QLatin1Char(','));
}

void GuestBundle::build(const QVector<Mount>& mounts) {
    if (building_) {
        return;
    }
    building_ = true;
    const std::string shares = shares_argument(mounts).toStdString();
    (void)QtConcurrent::run([] {
        return AdapterNames{}.render(platform::HostGpus::enumerate());
    }).then(this, [this, shares](const QString& names) {
        const auto finish = [this, shares](const QString& adapter_names) {
            (void)QtConcurrent::run([adapter_names, shares] { return collect(adapter_names, shares); })
                .then(this, [this](const std::shared_ptr<const Contents>& contents) {
                    building_ = false;
                    Q_EMIT built(contents);
                });
        };
        if (names.isEmpty()) {
            LSW_WARN("update: no host GPU found to name; the guest's adapter takes the "
                     "driver's static model name");
            finish(names);
            return;
        }
        auto* const catalog = new DriverCatalog({}, settings_.driver_versions_path(), this);
        connect(catalog, &DriverCatalog::completed, this, [catalog, finish](const QString& versioned) {
            catalog->deleteLater();
            finish(versioned);
        });
        catalog->lookup(names);
    });
}

std::shared_ptr<const GuestBundle::Contents> GuestBundle::collect(const QString& adapter_names,
                                                                  const std::string& shares) {
    auto contents = std::make_shared<Contents>();
    contents->version = LSW_VERSION;
    Gathering gathering;
    QString error;
    const auto fail = [&](const QString& why) {
        contents->error = why;
        return std::shared_ptr<const Contents>{contents};
    };

    const QString agent = platform::AgentExePath::locate();
    if (agent.isEmpty()) {
        return fail(QStringLiteral("cannot find lsw-guest-agent.exe; set $LSW_AGENT_EXE or run "
                                   "scripts/build-agent.sh"));
    }
    if (!gathering.add_file(GuestSwapScript::kGuestAgent, agent, &error)) {
        return fail(error);
    }
    const QString script = platform::GuestUpdateScript::locate();
    if (script.isEmpty()) {
        return fail(QStringLiteral("cannot find lsw-guest-update.ps1; set "
                                   "$LSW_GUEST_UPDATE_SCRIPT or build the manager"));
    }
    if (!gathering.add_file(kGuestUpdateScript, script, &error)) {
        return fail(error);
    }
    const QString tree = platform::AccelVirtPaths::driver_tree();
    if (tree.isEmpty()) {
        return fail(QStringLiteral("the accel-virt driver package was not found; build it or "
                                   "set $LSW_AV_DRIVERS"));
    }
    const QDir av{tree};
    if (!av.exists(QStringLiteral("provision.ps1")) || !av.exists(QStringLiteral("package"))) {
        return fail(QStringLiteral("%1 has no provision.ps1 and package/").arg(tree));
    }
    if (!gathering.add_file(std::string{kGuestAv} + "provision.ps1", av.filePath(QStringLiteral("provision.ps1")), &error)
        || !gathering.add_dir(std::string{kGuestAv} + "package\\", av.filePath(QStringLiteral("package")), &error)) {
        return fail(error);
    }
    for (const char* arch : {"x64", "x86"}) {
        const QString dir = av.filePath(QStringLiteral("loaders/%1").arg(QLatin1StringView(arch)));
        if (QDir{dir}.exists()
            && !gathering.add_dir(std::string{kGuestAv} + "loaders\\" + arch + "\\", dir, &error)) {
            return fail(error);
        }
    }

    const QString mirror = platform::MirrorExePaths::guest();
    if (mirror.isEmpty()) {
        LSW_WARN("update: no guest window mirror found; the guest's is left as it is. Set "
                 "LSW_REMOTEWIN_GUEST or run scripts/build-agent.sh");
    } else if (!gathering.add_file(GuestSwapScript::kGuestMirror, mirror, &error)) {
        LSW_WARN("update: the guest window mirror: {}", error.toStdString());
    }
    for (const bool x86 : {false, true}) {
        const QString loader = platform::AccelVirtPaths::vulkan_loader(x86);
        const char* const bits = x86 ? "32" : "64";
        if (loader.isEmpty()) {
            LSW_WARN("update: no {}-bit Vulkan loader built; the guest keeps what Windows has. "
                     "Run scripts/build-agent.sh",
                     bits);
        } else if (!gathering.add_file(std::string{kGuestAv} + "loaders\\" + (x86 ? "x86" : "x64")
                                           + "\\vulkan-1.dll",
                                       loader, &error)) {
            LSW_WARN("update: the {}-bit Vulkan loader: {}", bits, error.toStdString());
        }
    }
    const QString virtiofs = platform::GuestVirtiofsPath::locate();
    if (virtiofs.isEmpty()) {
        LSW_WARN("update: no shared-folder guest files found (share/lsw/guest/virtiofs, or "
                 "$LSW_GUEST_VIRTIOFS); the guest gets none");
    } else if (!gathering.add_dir(kGuestVirtiofsDir, virtiofs, &error)) {
        LSW_WARN("update: the shared-folder guest files: {}", error.toStdString());
    }

    if (!adapter_names.isEmpty()) {
        QStringList lines = adapter_names.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        lines.sort();
        gathering.add_bytes(std::string{kGuestAv} + "adapters.txt",
                            (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    }
    gathering.add_bytes(kSharesGuestPath, QByteArray::fromStdString(shares));

    const std::string adapters_path = std::string{kGuestAv} + "adapters.txt";
    std::string release_lines;
    std::string config_lines;
    for (auto& [path, bytes] : gathering.files()) {
        std::string hash = sha256_hex(bytes);
        const std::string line = hash + "  " + path + "\n";
        contents->manifest += line;
        (path == kSharesGuestPath || path == adapters_path ? config_lines : release_lines) += line;
        contents->files.push_back({path, std::move(bytes), std::move(hash)});
    }
    contents->release = sha256_hex(QByteArray::fromStdString(release_lines));
    contents->config = sha256_hex(QByteArray::fromStdString(config_lines));
    contents->shares = shares;
    return contents;
}

}
