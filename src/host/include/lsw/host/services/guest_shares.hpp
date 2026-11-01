#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <string>

namespace lsw::host::link { class AgentRpcClient; }
namespace lsw::host::vm { struct DomainDescription; }

namespace lsw::host::services {

// The shared folders, the Windows half: installed and mounted.
//
// The domain gives Windows one virtio-fs device per shared folder, tagged
// with its drive letter (DomainXmlBuilder). Windows mounts them with
// lsw-shares.ps1 (src/guest/virtiofs), which LSW ships beside virtio-win's
// viofs driver and virtiofs.exe and the WinFsp installer
// (platform::GuestVirtiofsPath): it installs those three, starts one
// virtiofs instance per share under WinFsp's launcher, which makes each
// drive for every session, and registers a task that mounts the saved
// list again whenever Windows starts.
//
// A new install has the tree from the payload ISO and installs it at
// SetupComplete. This brings any guest to the tree this LSW ships and
// mounts the shares its machine has now, once per boot, from GuestUpkeep
// - so a guest set up by a release without shared folders gets them, and
// a share added or removed on the settings page (a restart) is mounted or
// unmounted on the boot that follows:
//   1. the files of the shipped tree are compared with the guest's copy in
//      C:\ProgramData\LSW\virtiofs (the agent's file_hashes)
//   2. those that differ are served from a GuestFileServer, and a script
//      run as SYSTEM (run_script) downloads them and runs
//      ``lsw-shares.ps1 -Install -Shares <the machine's>``
//   3. its end (setup_done) is waited for, and its exit code named.
// A guest with no shares and none installed is left as it is.
//
// Blocking, for GuestUpkeep's worker thread. Every step is logged here,
// and the guest's own steps in C:\ProgramData\LSW\shares.log.
class GuestShares {
public:
    // One share as Windows mounts it.
    struct Mount {
        QString tag;    // the virtio-fs device's
        QString drive;  // "Z:"
        QString name;   // what Explorer calls the drive: the folder's name
    };

    struct Outcome {
        bool ok = true;
        // A sentence: what was done, or why it was not.
        QString message;
    };

    // Where the guest keeps its copy of the tree, and its log.
    static constexpr const char* kGuestDir = "C:\\ProgramData\\LSW\\virtiofs";
    static constexpr const char* kGuestLog = "C:\\ProgramData\\LSW\\shares.log";

    // The mounts the shares of ``definition`` make. Each tag is its drive
    // letter, lower-cased (domain_inputs_from_settings), so the drive is
    // the tag upper-cased; a tag that is not a single letter is no share
    // of LSW's and is left out.
    [[nodiscard]] static QVector<Mount> mounts_of(const vm::DomainDescription& definition);

    // lsw-shares.ps1's -Shares for ``mounts``: tag=drive=name between commas,
    // the name in base64 of its UTF-8, or "none".
    [[nodiscard]] static QString shares_argument(const QVector<Mount>& mounts);

    // ``mounts`` as a log line or a message says them: "Y: Games, Z: Music".
    [[nodiscard]] static QString describe(const QVector<Mount>& mounts);

    // The batch file run in the guest: fetch ``fetch`` (names in the tree)
    // from 10.0.2.2:``port`` into kGuestDir, then install and mount.
    [[nodiscard]] static std::string script(unsigned short port, const QStringList& fetch,
                                            const QVector<Mount>& mounts);

    // What the script's exit code means, as a sentence; lsw-shares.ps1
    // documents the codes.
    [[nodiscard]] static QString explain(int exit_code);

    explicit GuestShares(link::AgentRpcClient& rpc) noexcept : rpc_(rpc) {}

    // Install what differs and mount ``mounts``, unmounting any other.
    // Up to ten minutes; WinFsp's installer is most of it, once.
    [[nodiscard]] Outcome run(const QVector<Mount>& mounts);

private:
    link::AgentRpcClient& rpc_;
};

}  // namespace lsw::host::services
