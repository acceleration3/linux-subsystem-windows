#include "lsw/host/services/guest_shares.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/link/event_subscription.hpp"
#include "lsw/host/platform/guest_virtiofs_path.hpp"
#include "lsw/host/services/guest_file_server.hpp"
#include "lsw/host/vm/domain_description.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::services {

namespace {

// WinFsp's installer once, then the wait for the devices and a mount per
// share: minutes at most. Past this the job is not coming back.
constexpr auto kJobWindow = std::chrono::minutes(10);

// lsw-shares.ps1's codes, and the script's own for a failed download.
constexpr int kDownloadFailed = 8;

// A file of the tree, as its name crosses into a batch file and a URL.
[[nodiscard]] bool plain_name(const QString& name) {
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9._-]+$"));
    return plain.match(name).hasMatch();
}

// A run_script job's end, as the reader thread hands it over.
class JobEnd {
public:
    struct End {
        bool ok = false;
        int exit_code = 0;
        std::string message;
    };

    // Every setup_done is kept until the job's id is known: a script that
    // ends fast reports its end before the reply that names it.
    void on_done(const nlohmann::json& frame) {
        const std::lock_guard lock{mutex_};
        ends_.emplace_back(frame.value("job_id", std::string{}),
                           End{frame.value("ok", false), frame.value("exit_code", 0),
                               frame.value("message", std::string{})});
        changed_.notify_all();
    }

    [[nodiscard]] std::optional<End> wait(const std::string& job,
                                          std::chrono::steady_clock::duration window) {
        std::unique_lock lock{mutex_};
        std::optional<End> found;
        changed_.wait_for(lock, window, [&] {
            for (const auto& [id, end] : ends_) {
                if (id == job) {
                    found = end;
                    return true;
                }
            }
            return false;
        });
        return found;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::pair<std::string, End>> ends_;
};

}  // namespace

QVector<GuestShares::Mount> GuestShares::mounts_of(const vm::DomainDescription& definition) {
    QVector<Mount> mounts;
    for (const auto& share : definition.shares) {
        if (share.tag.size() != 1 || !share.tag.front().isLetter()) {
            LSW_WARN("shares: tag '{}' ({}) is not a drive letter; Windows does not mount it",
                     share.tag.toStdString(), share.host_path.toStdString());
            continue;
        }
        // The root of the host has no name of its own to go by.
        const QString folder = QFileInfo(QDir::cleanPath(share.host_path)).fileName();
        mounts.push_back({share.tag, share.tag.toUpper() + QLatin1Char(':'),
                          folder.isEmpty() ? QStringLiteral("Host") : folder});
    }
    return mounts;
}

QString GuestShares::shares_argument(const QVector<Mount>& mounts) {
    QStringList items;
    for (const auto& mount : mounts) {
        items << mount.tag + QLatin1Char('=') + mount.drive + QLatin1Char('=')
                     + QString::fromLatin1(mount.name.toUtf8().toBase64());
    }
    return items.isEmpty() ? QStringLiteral("none") : items.join(QLatin1Char(','));
}

QString GuestShares::describe(const QVector<Mount>& mounts) {
    QStringList items;
    for (const auto& mount : mounts) {
        items << mount.drive + QLatin1Char(' ') + mount.name;
    }
    return items.isEmpty() ? QStringLiteral("none") : items.join(QStringLiteral(", "));
}

std::string GuestShares::script(unsigned short port, const QStringList& fetch,
                                const QVector<Mount>& mounts) {
    // Each file lands as .part and is moved over the old one only once it
    // is whole, so a failed download leaves the guest's copy as it was.
    QString text = QStringLiteral(
        "@echo off\r\n"
        "setlocal\r\n"
        "set DIR=%1\r\n"
        "set LOG=%2\r\n"
        "if not exist \"%DIR%\" md \"%DIR%\"\r\n")
        .arg(QLatin1StringView(kGuestDir), QLatin1StringView(kGuestLog));
    for (const QString& name : fetch) {
        text += QStringLiteral(
            "curl.exe --fail -s -S -L -o \"%DIR%\\%1.part\" http://10.0.2.2:%2/%1 >> \"%LOG%\" 2>&1\r\n"
            "if errorlevel 1 ( echo [manager] downloading %1 failed >> \"%LOG%\" & exit /b %3 )\r\n"
            "move /Y \"%DIR%\\%1.part\" \"%DIR%\\%1\" >nul\r\n"
            "if errorlevel 1 ( echo [manager] replacing %1 failed >> \"%LOG%\" & exit /b %3 )\r\n"
            "echo [manager] updated %1 >> \"%LOG%\"\r\n")
            .arg(name).arg(port).arg(kDownloadFailed);
    }
    text += QStringLiteral(
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%DIR%\\lsw-shares.ps1\" "
        "-Install -Shares %1\r\n"
        "exit /b %ERRORLEVEL%\r\n")
        .arg(shares_argument(mounts));
    return text.toStdString();
}

QString GuestShares::explain(int exit_code) {
    QString what;
    switch (exit_code) {
    case 1:
        what = QStringLiteral("LSW's shared-folder files in Windows are incomplete");
        break;
    case 2:
        what = QStringLiteral("Windows would not install the virtio-fs driver");
        break;
    case 3:
        what = QStringLiteral("WinFsp did not install in Windows");
        break;
    case 4:
        what = QStringLiteral("virtiofs could not be registered with WinFsp's launcher");
        break;
    case 5:
        what = QStringLiteral("a shared folder's device has no working driver in Windows");
        break;
    case 6:
        what = QStringLiteral("a shared folder did not mount; its drive letter may be taken "
                              "in Windows");
        break;
    case 7:
        what = QStringLiteral("Windows has to restart to finish setting up the shared folders");
        break;
    case kDownloadFailed:
        what = QStringLiteral("the shared-folder files could not be copied into Windows");
        break;
    default:
        what = QStringLiteral("setting up the shared folders failed (exit code %1)").arg(exit_code);
        break;
    }
    return QStringLiteral("%1. %2 in Windows says more.").arg(what, QLatin1StringView(kGuestLog));
}

GuestShares::Outcome GuestShares::run(const QVector<Mount>& mounts) {
    const QString tree = platform::GuestVirtiofsPath::locate();
    if (tree.isEmpty()) {
        if (mounts.isEmpty()) {
            return {true, QStringLiteral("no shared folders, and no guest half shipped here")};
        }
        return {false, QStringLiteral("This LSW has no guest half for shared folders "
                                      "(share/lsw/guest/virtiofs, or $LSW_GUEST_VIRTIOFS), so "
                                      "Windows cannot mount them.")};
    }

    // 1. The shipped tree against the guest's copy.
    GuestFileServer::Files shipped;
    nlohmann::json paths = nlohmann::json::array();
    const QDir dir{tree};
    for (const QString& name : dir.entryList(QDir::Files, QDir::Name)) {
        if (!plain_name(name)) {
            continue;
        }
        QFile file{dir.filePath(name)};
        if (!file.open(QIODevice::ReadOnly)) {
            return {false, QStringLiteral("Could not read %1: %2")
                               .arg(file.fileName(), file.errorString())};
        }
        shipped.emplace(name.toStdString(), file.readAll());
        paths.push_back(QStringLiteral("%1\\%2").arg(QLatin1StringView(kGuestDir), name).toStdString());
    }
    if (shipped.find("lsw-shares.ps1") == shipped.end()) {
        return {false, QStringLiteral("%1 has no lsw-shares.ps1").arg(tree)};
    }
    const auto hashes = rpc_.request(linkproto::cmd::kFileHashes, {{"paths", paths}});
    if (!hashes.ok) {
        return {false, QStringLiteral("The agent did not say which shared-folder files Windows "
                                      "has: %1").arg(QString::fromStdString(hashes.error))};
    }
    const auto found = hashes.body.value("hashes", nlohmann::json::object());
    QStringList fetch;
    bool installed = false;
    for (const auto& [name, bytes] : shipped) {
        const std::string path =
            QStringLiteral("%1\\%2").arg(QLatin1StringView(kGuestDir), QString::fromStdString(name))
                .toStdString();
        const std::string in_guest =
            found.contains(path) && found[path].is_string() ? found[path].get<std::string>()
                                                            : std::string{};
        if (name == "lsw-shares.ps1" && !in_guest.empty()) {
            installed = true;
        }
        const std::string want =
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
        if (in_guest != want) {
            fetch << QString::fromStdString(name);
        }
    }
    if (mounts.isEmpty() && !installed) {
        return {true, QStringLiteral("no shared folders, and none set up in Windows")};
    }
    LSW_INFO("shares: mounting {} in Windows; {}", describe(mounts).toStdString(),
             fetch.isEmpty() ? std::string{"its files are current"}
                             : "copying " + fetch.join(QStringLiteral(", ")).toStdString());

    // 2. Serve what differs, and run the script that fetches and installs.
    GuestFileServer::Files served;
    for (const QString& name : fetch) {
        served.emplace(name.toStdString(), shipped.at(name.toStdString()));
    }
    GuestFileServer server{std::move(served)};
    if (!fetch.isEmpty() && !server.start()) {
        return {false, QStringLiteral("Could not serve the shared-folder files to Windows: %1")
                           .arg(QString::fromStdString(server.error()))};
    }

    JobEnd ends;
    const link::EventSubscription done_sub = rpc_.subscribe(
        linkproto::topic::kSetupDone, [&ends](const nlohmann::json& frame) { ends.on_done(frame); });
    const auto ack = rpc_.request(
        linkproto::cmd::kRunScript,
        {{"name", "shared folders"}, {"script", script(server.port(), fetch, mounts)}});
    const std::string job = ack.body.value("job_id", std::string{});
    if (!ack.ok || job.empty()) {
        return {false, QStringLiteral("The agent would not set up the shared folders: %1")
                           .arg(QString::fromStdString(ack.error))};
    }

    // 3. Its end.
    const auto end = ends.wait(job, kJobWindow);
    if (!end) {
        return {false, QStringLiteral("Setting up the shared folders did not finish in ten "
                                      "minutes. %1 in Windows says how far it got.")
                           .arg(QLatin1StringView(kGuestLog))};
    }
    LSW_INFO("shares: job {} ended ok={} exit={} ({} download(s) served)", job, end->ok,
             end->exit_code, server.downloads_served());
    if (end->exit_code != 0) {
        return {false, explain(end->exit_code)};
    }
    if (!end->ok) {
        return {false, QStringLiteral("Setting up the shared folders failed: %1")
                           .arg(QString::fromStdString(end->message))};
    }
    return {true, mounts.isEmpty()
                      ? QStringLiteral("no shared folders; none mounted in Windows")
                      : QStringLiteral("mounted in Windows: %1").arg(describe(mounts))};
}

}  // namespace lsw::host::services
