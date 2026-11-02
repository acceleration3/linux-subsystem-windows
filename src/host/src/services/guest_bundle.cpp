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

// Where the update script and the payload ISO's tree put things.
constexpr const char* kGuestUpdateScript = "C:\\ProgramData\\LSW\\lsw-guest-update.ps1";
constexpr const char* kGuestVirtiofsDir = "C:\\ProgramData\\LSW\\virtiofs\\";
constexpr const char* kGuestAv = "C:\\av\\";

// The bundle as it is gathered: guest path to bytes.
class Gathering {
public:
    // False, with ``error`` set, when the file cannot be read.
    bool add_file(const std::string& guest_path, const QString& host_path, QString* error) {
        // The path crosses into a batch file that downloads it (GuestUpdate).
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

    // Every plain file of ``dir`` (not its subdirectories) under ``guest_dir``.
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

}  // namespace

GuestBundle::GuestBundle(config::Settings& settings, QObject* parent)
    : QObject(parent), settings_(settings) {}

namespace {

// The mount of the share with ``tag`` on ``host_path``, if the tag is one
// Windows can mount.
void add_mount(QVector<GuestBundle::Mount>& mounts, const QString& tag, const QString& host_path) {
    if (tag.size() != 1 || !tag.front().isLetter()) {
        LSW_WARN("shares: tag '{}' ({}) is not a drive letter; Windows does not mount it",
                 tag.toStdString(), host_path.toStdString());
        return;
    }
    // The root of the host has no name of its own to go by.
    const QString folder = QFileInfo(QDir::cleanPath(host_path)).fileName();
    mounts.push_back({tag, tag.toUpper() + QLatin1Char(':'), folder.isEmpty() ? QStringLiteral("Host") : folder});
}

}  // namespace

QVector<GuestBundle::Mount> GuestBundle::mounts_of(const vm::DomainDescription& definition) {
    QVector<Mount> mounts;
    for (const auto& share : definition.shares) {
        add_mount(mounts, share.tag, share.host_path);
    }
    return mounts;
}

QVector<GuestBundle::Mount> GuestBundle::mounts_of(const config::ManagerSettings& settings) {
    QVector<Mount> mounts;
    for (const auto& share : settings.host_shares()) {
        // The tag is the drive letter, lower-cased, as the definition has it.
        add_mount(mounts, share.drive.left(1).toLower(), share.host_path);
    }
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
        items << mount.tag + QLatin1Char('=') + mount.drive + QLatin1Char('=')
                     + QString::fromLatin1(mount.name.toUtf8().toBase64());
    }
    return items.isEmpty() ? QStringLiteral("none") : items.join(QLatin1Char(','));
}

void GuestBundle::build(const QVector<Mount>& mounts) {
    if (building_) {
        return;
    }
    building_ = true;
    const std::string shares = shares_argument(mounts).toStdString();
    // Enumerating the GPUs loads Vulkan, so it is not done on this thread.
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
        // The catalog, which keeps the last version it found for a GPU, so
        // that a search that fails does not change the file and with it the
        // release.
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

    // Required.
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

    // Optional: a missing one costs the guest that part, and says so.
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

    // Made here.
    if (!adapter_names.isEmpty()) {
        // Sorted so that the order the GPUs were enumerated in does not
        // change the release; provision.ps1 looks lines up by vendor and
        // device.
        QStringList lines = adapter_names.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        lines.sort();
        gathering.add_bytes(std::string{kGuestAv} + "adapters.txt",
                            (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    }
    gathering.add_bytes(kSharesGuestPath, QByteArray::fromStdString(shares));

    for (auto& [path, bytes] : gathering.files()) {
        std::string hash = sha256_hex(bytes);
        contents->manifest += hash + "  " + path + "\n";
        contents->files.push_back({path, std::move(bytes), std::move(hash)});
    }
    contents->release = sha256_hex(QByteArray::fromStdString(contents->manifest));
    contents->shares = shares;
    return contents;
}

}  // namespace lsw::host::services
